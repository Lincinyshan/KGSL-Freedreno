#include "kgsl_priv.h"
#include "util/os_mman.h"

#include "util/simple_mtx.h"

/* KGSL 没有内核级隐式同步（不像 msm 的 drm 有 implicit sync）：
 * 没有 fence 记录的 BO 如果直接释放，GPU 可能还在读写它，导致
 *   GPU PAGE FAULT（UCHE write translation fault）+ 画面花屏。
 *
 * 做法：把这类 BO 挂到 pending 列表，等它“最后一次提交的时间戳”
 * 完成后（非阻塞探测，timeout=0）再真正释放，不阻塞渲染线程。 */
#define KGSL_PENDING_MAX 8192

struct kgsl_pending_bo {
    struct fd_bo *bo;
    struct fd_device *dev;
    uint32_t ctx;
    uint64_t ts;
};

static struct kgsl_pending_bo g_pending[KGSL_PENDING_MAX];
static int g_pending_n;
static simple_mtx_t g_pending_lock = SIMPLE_MTX_INITIALIZER;

/* GPU 用完的 BO 在这里退役：优先放回 BO cache（保住命中率），
 * 实在不能缓存才真正释放。
 * 注意：不能持有 g_pending_lock 调用 fd_bo_cache_free()，因为它会顺带
 * 清理过期条目并 destroy 别的 BO，那条路径会再次来拿这把锁。 */
/* fd_bo_cache_free() 会顺带清理过期条目、进而 destroy 别的 BO，可能再次
 * 回到这里（递归）。用线程局部标记把这条递归链掐断，直接释放即可。 */
static __thread bool tls_in_retire;

static void
kgsl_pending_retire(struct fd_bo *bo)
{
    if (!tls_in_retire) {
        tls_in_retire = true;
        if (bo->bo_reuse != NO_CACHE &&
            fd_bo_cache_free(bo->bo_reuse == RING_CACHE ? &bo->dev->ring_cache
                                                       : &bo->dev->bo_cache, bo) == 0) {
            tls_in_retire = false;
            return;
        }
        tls_in_retire = false;
    }

    fd_bo_fini_common(bo);
}

/* 调用时必须持有 g_pending_lock：只把退役的 BO 摘出来，不做释放动作。 */
static int
kgsl_pending_collect_locked(struct fd_bo **out, int max)
{
    int n = 0;

    /* 队列按时间戳单调递增：只探测队首，队首没完成后面的也一定没完成 */
    while (g_pending_n > 0 && n < max) {
        struct kgsl_pending_bo *e = &g_pending[0];
        if (kgsl_wait_ts(e->dev, e->ctx, e->ts, 0) != 0)
            break;

        out[n++] = e->bo;
        g_pending_n--;
        if (g_pending_n > 0)
            memmove(&g_pending[0], &g_pending[1],
                    (size_t)g_pending_n * sizeof(g_pending[0]));
    }

    return n;
}

static void
kgsl_pending_flush(void)
{
    struct fd_bo *retired[32];
    int n;

    simple_mtx_lock(&g_pending_lock);
    n = kgsl_pending_collect_locked(retired, ARRAY_SIZE(retired));
    simple_mtx_unlock(&g_pending_lock);

    for (int i = 0; i < n; i++)
        kgsl_pending_retire(retired[i]);
}

static bool
kgsl_pending_add(struct fd_bo *bo, struct fd_device *dev, uint32_t ctx, uint64_t ts)
{
    bool ok;

    kgsl_pending_flush();

    simple_mtx_lock(&g_pending_lock);
    if (g_pending_n >= KGSL_PENDING_MAX) {
        /* 队列满：宁可阻塞等队首那次提交完成，也不能提前释放 BO */
        struct kgsl_pending_bo head = g_pending[0];
        struct fd_bo *retired[32];
        int n;

        (void)kgsl_wait_ts(head.dev, head.ctx, head.ts, 1000);
        n = kgsl_pending_collect_locked(retired, ARRAY_SIZE(retired));
        simple_mtx_unlock(&g_pending_lock);
        for (int i = 0; i < n; i++)
            kgsl_pending_retire(retired[i]);
        simple_mtx_lock(&g_pending_lock);
    }

    ok = g_pending_n < KGSL_PENDING_MAX;
    if (ok) {
        g_pending[g_pending_n++] = (struct kgsl_pending_bo){
            .bo = bo, .dev = dev, .ctx = ctx, .ts = ts,
        };
    }
    simple_mtx_unlock(&g_pending_lock);

    return ok;
}

static uint64_t
kgsl_bo_iova(struct fd_bo *bo)
{
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    return kgsl_bo->iova;
}

static void
kgsl_bo_set_name(struct fd_bo *bo, const char *fmt, va_list ap)
{
    /* This function is a no op for KGSL */
    return;
}

static int
kgsl_bo_offset(struct fd_bo *bo, uint64_t *offset)
{
    /* from tu_kgsl.c - offset for mmap needs to be */
    /* the returned alloc id shifted over 12 */
    *offset = bo->handle << 12;
    return 0;
}

static int
kgsl_bo_madvise(struct fd_bo *bo, int willneed)
{
    /* KGSL does not support this, so simply return willneed */
    return willneed;
}

static void
kgsl_bo_cache_op(struct fd_bo *bo, unsigned int op)
{
    struct kgsl_gpumem_sync_cache req = {
        .id = bo->handle,
        .op = op,
    };

    kgsl_pipe_safe_ioctl(bo->dev->fd, IOCTL_KGSL_GPUMEM_SYNC_CACHE, &req);
}

static int
kgsl_bo_cpu_prep(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op)
{
    /* KGSL 没有隐式同步：先把这块 BO 上挂着 fence 的提交等完，CPU 才能碰它。
     * freedreno 靠这个返回值判断「能不能直接 map / 要不要先 flush+stall」，
     * 之前永远返回 0 等于告诉它「随便写」，于是 CPU 会盖掉 GPU 还在读的
     * BO（staging 上传、贴图重传），表现出来就是像素级闪烁。
     * NOSYNC 只是非阻塞探测，超时给 0。 */
    for (uint32_t i = 0; i < bo->nr_fences; i++) {
        struct fd_fence *fence = bo->fences[i];
        int ret = fence->pipe->funcs->wait(fence->pipe, fence,
                                           (op & FD_BO_PREP_NOSYNC) ? 0 : ~0);
        if (ret)
            return ret;
    }

    if (op & FD_BO_PREP_NOSYNC)
        return 0;

    /* CPU 写完要交给 GPU 读 / GPU 写完要给 CPU 读：KGSL 需要显式 cache 操作 */
    if (op & FD_BO_PREP_FLUSH)
        kgsl_bo_cache_op(bo, KGSL_GPUMEM_CACHE_CLEAN);
    else if (op & FD_BO_PREP_READ)
        kgsl_bo_cache_op(bo, KGSL_GPUMEM_CACHE_INV);

    /* write-combine 写缓冲：确保 CPU 的写真的落到内存 */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    return 0;
}

void
kgsl_bo_close_handle(struct fd_bo *bo)
{
    struct kgsl_gpumem_free_id req = {
        .id = bo->handle
    };

    kgsl_pipe_safe_ioctl(bo->dev->fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
}

static void
kgsl_bo_destroy(struct fd_bo *bo)
{
    /* KGSL will immediately delete the BO when we close
     * the handle, so wait on all fences to ensure
     * the GPU is done using it before we destory it
     */
    for (uint32_t i = 0; i < bo->nr_fences; i++) {
        struct fd_pipe *pipe = bo->fences[i]->pipe;
        pipe->funcs->wait(pipe, bo->fences[i], ~0);
    }

    /* 没有 fence 记录的 BO（或 fence 等待没覆盖到的）：挂起，等 GPU 时间戳过。 */
    if (bo->nr_fences == 0) {
        struct kgsl_device *kdev = to_kgsl_device(bo->dev);
        if (kdev->last_submit_ts) {
            if (kgsl_pending_add(bo, bo->dev, kdev->last_submit_ctx,
                                 kdev->last_submit_ts))
                return;
            /* 挂起队列实在放不下：同步等这次提交结束，绝不能提前释放 */
            (void)kgsl_wait_ts(bo->dev, kdev->last_submit_ctx,
                               kdev->last_submit_ts, 1000);
        }
    } else {
        kgsl_pending_flush();
    }

    fd_bo_fini_common(bo);
}

static void *
kgsl_bo_map(struct fd_bo *bo)
{
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    if (!bo->map) {
        if (kgsl_bo->bo_type == KGSL_BO_IMPORT) {
            /* in `fd_bo_map` if it tries to mmap this BO. mmap logic is copied from
             * https://android.googlesource.com/platform/hardware/libhardware/+/master/modules/gralloc/mapper.cpp#44
             */
            bo->map = os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_bo->import_fd, 0);
        } else {
            uint64_t offset;
            int ret;

            ret = bo->funcs->offset(bo, &offset);
            if (ret) {
                return NULL;
            }

            bo->map = os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    bo->dev->fd, offset);
            if (bo->map == MAP_FAILED) {
                ERROR_MSG("mmap failed: %s", strerror(errno));
                bo->map = NULL;
            }
        }

        if (bo->map == MAP_FAILED) {
            ERROR_MSG("mmap failed: %s", strerror(errno));
            bo->map = NULL;
        }
    }
    return bo->map;
}

static const struct fd_bo_funcs bo_funcs = {
    .iova = kgsl_bo_iova,
    .set_name = kgsl_bo_set_name,
    .offset = kgsl_bo_offset,
    .map = kgsl_bo_map,
    .madvise = kgsl_bo_madvise,
    .cpu_prep = kgsl_bo_cpu_prep,
    .destroy = kgsl_bo_destroy,
};

/* Size is not used by KGSL */
struct fd_bo *
kgsl_bo_from_handle(struct fd_device *dev, uint32_t size, uint32_t handle)
{
    struct fd_bo *bo;
    int ret;
    struct kgsl_gpuobj_info req = {
        .id = handle,
    };

    ret = kgsl_pipe_safe_ioctl(dev->fd,
            IOCTL_KGSL_GPUOBJ_INFO, &req);

    if (ret) {
        ERROR_MSG("Failed to get handle info (%s)", strerror(errno));
        return NULL;
    }

    struct kgsl_bo *kgsl_bo = calloc(1, sizeof(*kgsl_bo));
    if (!kgsl_bo)
        return NULL;

    bo = &kgsl_bo->base;
    bo->dev = dev;
    bo->size = req.size;
    bo->handle = req.id;
    bo->funcs = &bo_funcs;

    kgsl_bo->iova = req.gpuaddr;

    fd_bo_init_common(bo, dev);

    return bo;
}

struct fd_bo *
kgsl_bo_from_dmabuf(struct fd_device *dev, int fd)
{
    struct fd_bo *bo;
    struct kgsl_gpuobj_import_dma_buf import_dmabuf = {
        .fd = fd,
    };
    struct kgsl_gpuobj_import req = {
        .priv = (uintptr_t)&import_dmabuf,
        .priv_len = sizeof(import_dmabuf),
        .flags = 0,
        .type = KGSL_USER_MEM_TYPE_DMABUF,
    };
    int ret;

    ret = kgsl_pipe_safe_ioctl(dev->fd,
            IOCTL_KGSL_GPUOBJ_IMPORT, &req);

    if (ret) {
        ERROR_MSG("Failed to import dma-buf (%s)", strerror(errno));
        return NULL;
    }

    bo = fd_bo_from_handle(dev, req.id, 0);

    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    kgsl_bo->bo_type = KGSL_BO_IMPORT;
    kgsl_bo->import_fd = fd;

    (void)kgsl_bo;

    return bo;
}

struct fd_bo *
kgsl_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags)
{
    int ret;
    struct fd_bo *bo;
    struct kgsl_gpumem_alloc_id req = {
        .size = size,
    };

    if (flags & FD_BO_GPUREADONLY)
        req.flags |= KGSL_MEMFLAGS_GPUREADONLY;

    /* 对齐 msm 语义：默认 write-combine（msm 用 MSM_BO_WC），
     * 只有 CACHED_COHERENT 的 BO 才申请 IOCOHERENT。
     * 不设的话 KGSL 默认 write-back 可缓存，CPU 写会留在缓存里，
     * GPU（如 UBWC 压缩纹理的 blit 上传）会读到旧数据 —— 时有时无的花屏来源。 */
    if (flags & FD_BO_CACHED_COHERENT)
        req.flags |= KGSL_MEMFLAGS_IOCOHERENT;
    else
        req.flags |= KGSL_CACHEMODE_WRITECOMBINE << KGSL_CACHEMODE_SHIFT;

    ret = kgsl_pipe_safe_ioctl(dev->fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &req);

    if (ret) {
        ERROR_MSG("GPUMEM_ALLOC_ID failed (%s)", strerror(errno));
        return NULL;
    }

    bo = kgsl_bo_from_handle(dev, size, req.id);

    if (!bo) {
        ERROR_MSG("Failed to allocate buffer object");
        return NULL;
    }

    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    kgsl_bo->bo_type = KGSL_BO_NATIVE;

    (void)kgsl_bo;

    return bo;
}
