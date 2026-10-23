/* egl_bridge_shim.c — 插件里的 "libEGL"，把 FCL 的 egl/gl 请求转发给真正的
 * Mesa libEGL，并负责先按绝对路径预加载 libgallium_dri.so。
 *
 * 背景：本 Mesa 的 libEGL 直接 NEEDED libgallium_dri.so，而插件目录不在
 * FCL 进程的 linker namespace 搜索路径里；所以由一个不依赖任何插件内库的
 * 小 shim 先 dlopen 绝对路径把 DRI 模块载入同一命名空间，再加载真正的 libEGL。
 */
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

   /* 预加载本目录下的依赖，让真正的 libEGL 的 NEEDED 按 SONAME 命中。
    * libhardware/libcutils 不是 public.libraries，App 命名空间里看不到，
    * 所以随插件带上 Mesa 的 android_stub 版本（只用于 hw_get_module /
    * property_get，gralloc 的 basic info 走 native_handle 的 fd，不依赖它）。 */
   static const char *const preloads[] = {
      "libgallium_dri.so",
   };
   for (unsigned i = 0; i < sizeof(preloads) / sizeof(preloads[0]); i++) {
      snprintf(path, sizeof(path), "%s/%s", g_dir, preloads[i]);
      if (!dlopen(path, RTLD_GLOBAL | RTLD_LAZY))
         fprintf(stderr, "[fcl-egl] preload %s failed: %s\n", path, dlerror());
   }

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

__attribute__((visibility("default"))) void *
eglGetProcAddress(const char *name)
{
   load_real();
   if (!g_real_getproc)
      return NULL;
   if (!strcmp(name, "eglGetProcAddress"))
      return (void *)eglGetProcAddress;
   return g_real_getproc(name);
}

/* LWJGL 3.3.5 之前用 glXGetProcAddress（FCL 的 glxshim 也是这么转发的）。 */
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
