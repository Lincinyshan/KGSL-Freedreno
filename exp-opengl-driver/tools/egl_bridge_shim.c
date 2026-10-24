/* egl_bridge_shim.c — 插件里的 "libEGL"
 *
 * 作用：
 *  1) 先按绝对路径预加载 libgallium_dri.so（插件目录不在 FCL 进程的
 *     linker namespace 搜索路径里，真 libEGL 的 NEEDED 只能靠预加载命中）；
 *  2) 加载真正的 Mesa libEGL（libEGL_mesa_gl.so）；
 *  3) 导出**完整 EGL 入口**并转发给它。
 *
 * 为什么必须导出完整 ABI：FCL 自己的 egl_loader 只 dlsym eglGetProcAddress，
 * 但新版本（MC 26.3 / LWJGL 3.4.1 / SDL）会直接 dlsym eglGetDisplay 等常规
 * 入口。缺任何一个，游戏就会报
 *   "OpenGL is not supported: Could not retrieve EGL function eglGetDisplay"
 * 然后静默回退 Vulkan。
 */
#define _GNU_SOURCE /* dladdr / Dl_info */
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* 自备 EGL 类型（不引 EGL 头，避免与转发原型冲突；aarch64 ABI 一致） */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLImage;
typedef void *EGLSync;
typedef void *EGLClientBuffer;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;
typedef void *EGLNativePixmapType;
typedef int EGLBoolean;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef long EGLAttrib;
typedef unsigned long long EGLTime;

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_IMAGE ((EGLImage)0)
#define EGL_NO_SYNC ((EGLSync)0)

typedef void *(*PFN_getproc)(const char *);

static void *g_real_egl;
static PFN_getproc g_real_getproc;
static char g_dir[PATH_MAX];

static void
self_dir(void)
{
   Dl_info info;

   if (g_dir[0])
      return;
   if (dladdr((void *)self_dir, &info) && info.dli_fname) {
      snprintf(g_dir, sizeof(g_dir), "%s", info.dli_fname);
      char *slash = strrchr(g_dir, '/');
      if (slash)
         *slash = '\0';
   } else {
      snprintf(g_dir, sizeof(g_dir), ".");
   }
}

static void
load_real(void)
{
   char path[PATH_MAX + 64];

   if (g_real_egl)
      return;

   self_dir();

   snprintf(path, sizeof(path), "%s/libgallium_dri.so", g_dir);
   if (!dlopen(path, RTLD_GLOBAL | RTLD_LAZY))
      fprintf(stderr, "[fcl-egl] preload %s failed: %s\n", path, dlerror());

   snprintf(path, sizeof(path), "%s/libEGL_mesa_gl.so", g_dir);
   g_real_egl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!g_real_egl) {
      fprintf(stderr, "[fcl-egl] dlopen %s failed: %s\n", path, dlerror());
      return;
   }
   g_real_getproc = (PFN_getproc)dlsym(g_real_egl, "eglGetProcAddress");
   if (!g_real_getproc)
      fprintf(stderr, "[fcl-egl] no eglGetProcAddress in %s\n", path);
}

/* 真库里的符号：先 dlsym，再退回 eglGetProcAddress */
static void *
real_sym(const char *name)
{
   void *p;

   load_real();
   if (!g_real_egl)
      return NULL;
   p = dlsym(g_real_egl, name);
   if (!p && g_real_getproc)
      p = g_real_getproc(name);
   return p;
}

#define FWD(ret, name, params, args)                                    \
   __attribute__((visibility("default"))) ret name params               \
   {                                                                    \
      typedef ret (*fn_t) params;                                       \
      static fn_t fn;                                                   \
      if (!fn)                                                          \
         fn = (fn_t)real_sym(#name);                                    \
      if (!fn)                                                          \
         return (ret)0;                                                 \
      return fn args;                                                   \
   }

#define FWDV(name, params, args)                                        \
   __attribute__((visibility("default"))) void name params              \
   {                                                                    \
      typedef void (*fn_t) params;                                      \
      static fn_t fn;                                                   \
      if (!fn)                                                          \
         fn = (fn_t)real_sym(#name);                                    \
      if (fn)                                                           \
         fn args;                                                       \
   }

__attribute__((visibility("default"))) void *
eglGetProcAddress(const char *name)
{
   load_real();
   if (!g_real_getproc)
      return NULL;
   if (!strcmp(name, "eglGetProcAddress"))
      return (void *)eglGetProcAddress;
   void *p = g_real_getproc(name);
   return p;
}

/* LWJGL 3.3.5 之前用 glXGetProcAddress（FCL 的 glxshim 也是这么转发的） */
__attribute__((visibility("default"))) void *
glXGetProcAddress(const char *name)
{
   return eglGetProcAddress(name);
}

__attribute__((visibility("default"))) void *
glXGetProcAddressARB(const char *name)
{
   return eglGetProcAddress(name);
}

/* ---------------- EGL 1.0 - 1.5 核心入口 ---------------- */

FWD(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType id), (id))
FWD(EGLBoolean, eglInitialize, (EGLDisplay d, EGLint *maj, EGLint *min), (d, maj, min))
FWD(EGLBoolean, eglTerminate, (EGLDisplay d), (d))
FWD(const char *, eglQueryString, (EGLDisplay d, EGLint name), (d, name))
FWD(EGLBoolean, eglGetConfigs, (EGLDisplay d, EGLConfig *cfgs, EGLint n, EGLint *num), (d, cfgs, n, num))
FWD(EGLBoolean, eglChooseConfig, (EGLDisplay d, const EGLint *attr, EGLConfig *cfgs, EGLint n, EGLint *num), (d, attr, cfgs, n, num))
FWD(EGLBoolean, eglGetConfigAttrib, (EGLDisplay d, EGLConfig c, EGLint a, EGLint *v), (d, c, a, v))
FWD(EGLSurface, eglCreateWindowSurface, (EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *attr), (d, c, w, attr))
FWD(EGLSurface, eglCreatePbufferSurface, (EGLDisplay d, EGLConfig c, const EGLint *attr), (d, c, attr))
FWD(EGLSurface, eglCreatePixmapSurface, (EGLDisplay d, EGLConfig c, EGLNativePixmapType p, const EGLint *attr), (d, c, p, attr))
FWD(EGLBoolean, eglDestroySurface, (EGLDisplay d, EGLSurface s), (d, s))
FWD(EGLBoolean, eglQuerySurface, (EGLDisplay d, EGLSurface s, EGLint a, EGLint *v), (d, s, a, v))
FWD(EGLBoolean, eglBindAPI, (EGLenum api), (api))
FWD(EGLenum, eglQueryAPI, (void), ())
FWD(EGLBoolean, eglWaitClient, (void), ())
FWD(EGLBoolean, eglReleaseThread, (void), ())
FWD(EGLBoolean, eglWaitGL, (void), ())
FWD(EGLBoolean, eglWaitNative, (EGLint engine), (engine))
FWD(EGLSurface, eglCreatePbufferFromClientBuffer, (EGLDisplay d, EGLenum type, EGLClientBuffer buf, EGLConfig c, const EGLint *attr), (d, type, buf, c, attr))
FWD(EGLBoolean, eglBindTexImage, (EGLDisplay d, EGLSurface s, EGLint buf), (d, s, buf))
FWD(EGLBoolean, eglReleaseTexImage, (EGLDisplay d, EGLSurface s, EGLint buf), (d, s, buf))
FWD(EGLBoolean, eglSurfaceAttrib, (EGLDisplay d, EGLSurface s, EGLint a, EGLint v), (d, s, a, v))
FWD(EGLBoolean, eglSwapInterval, (EGLDisplay d, EGLint i), (d, i))
FWD(EGLContext, eglCreateContext, (EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attr), (d, c, share, attr))
FWD(EGLBoolean, eglDestroyContext, (EGLDisplay d, EGLContext c), (d, c))
FWD(EGLBoolean, eglMakeCurrent, (EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c), (d, draw, read, c))
FWD(EGLContext, eglGetCurrentContext, (void), ())
FWD(EGLSurface, eglGetCurrentSurface, (EGLint readdraw), (readdraw))
FWD(EGLDisplay, eglGetCurrentDisplay, (void), ())
FWD(EGLBoolean, eglQueryContext, (EGLDisplay d, EGLContext c, EGLint a, EGLint *v), (d, c, a, v))
FWD(EGLBoolean, eglSwapBuffers, (EGLDisplay d, EGLSurface s), (d, s))
FWD(EGLBoolean, eglCopyBuffers, (EGLDisplay d, EGLSurface s, EGLNativePixmapType t), (d, s, t))
FWD(EGLImage, eglCreateImage, (EGLDisplay d, EGLContext c, EGLenum t, EGLClientBuffer b, const EGLAttrib *attr), (d, c, t, b, attr))
FWD(EGLBoolean, eglDestroyImage, (EGLDisplay d, EGLImage i), (d, i))
FWD(EGLSync, eglCreateSync, (EGLDisplay d, EGLenum t, const EGLAttrib *attr), (d, t, attr))
FWD(EGLBoolean, eglDestroySync, (EGLDisplay d, EGLSync s), (d, s))
FWD(EGLint, eglClientWaitSync, (EGLDisplay d, EGLSync s, EGLint flags, EGLTime t), (d, s, flags, t))
FWD(EGLBoolean, eglWaitSync, (EGLDisplay d, EGLSync s, EGLint flags), (d, s, flags))
FWD(EGLBoolean, eglGetSyncAttrib, (EGLDisplay d, EGLSync s, EGLint a, EGLAttrib *v), (d, s, a, v))

/* ---------------- EGL 1.5 平台入口 ---------------- */

FWD(EGLDisplay, eglGetPlatformDisplay, (EGLenum p, void *nd, const EGLAttrib *attr), (p, nd, attr))
FWD(EGLSurface, eglCreatePlatformWindowSurface, (EGLDisplay d, EGLConfig c, void *w, const EGLAttrib *attr), (d, c, w, attr))
FWD(EGLSurface, eglCreatePlatformPixmapSurface, (EGLDisplay d, EGLConfig c, void *p, const EGLAttrib *attr), (d, c, p, attr))

/* ---------------- 常见 EXT / KHR 别名（LWJGL 会直接 dlsym） ------------- */

FWD(EGLDisplay, eglGetPlatformDisplayEXT, (EGLenum p, void *nd, const EGLint *attr), (p, nd, attr))
FWD(EGLSurface, eglCreatePlatformWindowSurfaceEXT, (EGLDisplay d, EGLConfig c, void *w, const EGLint *attr), (d, c, w, attr))
FWD(EGLSurface, eglCreatePlatformPixmapSurfaceEXT, (EGLDisplay d, EGLConfig c, void *p, const EGLint *attr), (d, c, p, attr))
FWD(EGLBoolean, eglSwapBuffersWithDamageEXT, (EGLDisplay d, EGLSurface s, EGLint *rects, EGLint n), (d, s, rects, n))
FWD(EGLBoolean, eglSwapBuffersWithDamageKHR, (EGLDisplay d, EGLSurface s, EGLint *rects, EGLint n), (d, s, rects, n))
FWD(EGLImage, eglCreateImageKHR, (EGLDisplay d, EGLContext c, EGLenum t, EGLClientBuffer b, const EGLint *attr), (d, c, t, b, attr))
FWD(EGLBoolean, eglDestroyImageKHR, (EGLDisplay d, EGLImage i), (d, i))
FWD(EGLSync, eglCreateSyncKHR, (EGLDisplay d, EGLenum t, const EGLint *attr), (d, t, attr))
FWD(EGLBoolean, eglDestroySyncKHR, (EGLDisplay d, EGLSync s), (d, s))
FWD(EGLint, eglClientWaitSyncKHR, (EGLDisplay d, EGLSync s, EGLint flags, EGLTime t), (d, s, flags, t))
FWD(EGLBoolean, eglWaitSyncKHR, (EGLDisplay d, EGLSync s, EGLint flags), (d, s, flags))
FWD(EGLBoolean, eglGetSyncAttribKHR, (EGLDisplay d, EGLSync s, EGLint a, EGLint *v), (d, s, a, v))
