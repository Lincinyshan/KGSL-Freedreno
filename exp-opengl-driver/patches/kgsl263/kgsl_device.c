#include "kgsl_priv.h"

static const struct fd_device_funcs funcs = {
    .bo_new = kgsl_bo_new,
    .pipe_new = kgsl_pipe_new,
    .bo_from_handle = kgsl_bo_from_handle,
    .bo_from_dmabuf = kgsl_bo_from_dmabuf,
    .bo_close_handle = kgsl_bo_close_handle,
    .destroy = kgsl_device_destroy,
};

struct fd_device *
kgsl_device_new(int fd)
{
    struct kgsl_device *kgsl_dev;
    struct fd_device *dev;
    struct kgsl_devinfo info;

    /* Try to read the device info to detect if the FD is really KGSL */
    int prop_ret = kgsl_get_prop(fd, KGSL_PROP_DEVICE_INFO, &info, sizeof(info));
    if (prop_ret)
        return NULL;

    kgsl_dev = calloc(1, sizeof(*kgsl_dev));
    if (!kgsl_dev)
      return NULL;

    dev = &kgsl_dev->base;
    dev->funcs = &funcs;
    dev->fd = fd;
    dev->version = FD_VERSION_ROBUSTNESS;

    /* async submit_queue used for softpin deffered submits */
    util_queue_init(&dev->submit_queue, "sq", 8, 1, 0, NULL);

    dev->bo_size = sizeof(struct kgsl_bo);

    /* KGSL 无隐式同步：BO 不能一释放就进 cache 复用（见 try_recycle）。
     * A/B 开关：FCL_BO_CACHE=1 恢复旧行为（释放即入 cache、立即复用）。 */
    dev->no_recycle = getenv("FCL_BO_CACHE") == NULL;

    return dev;
}

static void
kgsl_device_destroy(struct fd_device *dev)
{
}
