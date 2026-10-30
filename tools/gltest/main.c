/* 独立复现：在普通 App 窗口（NativeActivity，SurfaceView 型）上
 * 用我们自己的 libEGL + libgallium_dri 跑一遍 EGL 窗口面流程。
 * 目的：区分「我们的 EGL 用法有问题」和「FCL 的 TextureView 窗口有问题」。
 */
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 这些入口不在 NDK 公开头文件里，但 libnativewindow 导出；运行时 dlsym */
struct ANativeWindowBuffer;
typedef int32_t (*PFN_query)(const ANativeWindow *, int, int32_t *);
typedef int (*PFN_dequeue)(ANativeWindow *, struct ANativeWindowBuffer **, int *);
typedef int (*PFN_cancel)(ANativeWindow *, struct ANativeWindowBuffer *, int);
#define Q_DEFAULT_WIDTH 1
#define Q_DEFAULT_HEIGHT 2
#define Q_MIN_UNDEQUEUED 4

#define TAG "GLTEST"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLSurface (*PFN_eglCreateWindowSurface)(EGLDisplay, EGLConfig, void *, const EGLint *);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*PFN_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLint (*PFN_eglGetError)(void);
typedef const char *(*PFN_eglQueryString)(EGLDisplay, EGLint);
typedef EGLBoolean (*PFN_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef EGLDisplay (*PFN_eglGetPlatformDisplay)(EGLenum, void *, const EGLint *);
typedef void *(*PFN_getproc)(const char *);

static char g_libdir[512];

static void self_dir(void) {
   Dl_info info;
   if (dladdr((void *)ANativeActivity_onCreate, &info) && info.dli_fname) {
      snprintf(g_libdir, sizeof(g_libdir), "%s", info.dli_fname);
      char *slash = strrchr(g_libdir, '/');
      if (slash) *slash = 0;
   }
}

static void *find_proc(void *handle, PFN_getproc getproc, const char *name) {
   void *p = dlsym(handle, name);
   if (!p && getproc) p = getproc(name);
   return p;
}

static void run_test(ANativeWindow *win) {
   /* 与 MC 的环境一致：freedreno 自身声明的桌面 GL 版本低于 4.6，
    * MC 是通过 MESA_GL_VERSION_OVERRIDE=4.6 才拿到 4.6 core 的。 */
   setenv("MESA_GL_VERSION_OVERRIDE", "4.6", 0);
   setenv("FCL_TEXDBG", "1", 0);
   setenv("MESA_GLSL_VERSION_OVERRIDE", "460", 0);
   setenv("allow_higher_compat_version", "true", 0);
   int32_t w = 0, h = 0, fmt = 0, minund = -1;
   void *nwlib = dlopen("libnativewindow.so", RTLD_LAZY);
   PFN_query q = nwlib ? (PFN_query)dlsym(nwlib, "ANativeWindow_query") : NULL;
   PFN_dequeue deq = nwlib ? (PFN_dequeue)dlsym(nwlib, "ANativeWindow_dequeueBuffer") : NULL;
   PFN_cancel cancel = nwlib ? (PFN_cancel)dlsym(nwlib, "ANativeWindow_cancelBuffer") : NULL;
   LOGI("libnativewindow=%p query=%p dequeue=%p cancel=%p", nwlib, (void *)q, (void *)deq, (void *)cancel);
   if (q) {
      q(win, Q_DEFAULT_WIDTH, &w);
      q(win, Q_DEFAULT_HEIGHT, &h);
      q(win, Q_MIN_UNDEQUEUED, &minund);
   }
   fmt = ANativeWindow_getFormat(win);
   LOGI("window=%p default=%dx%d fmt=0x%x minund=%d real=%dx%d", win, w, h, fmt, minund,
        ANativeWindow_getWidth(win), ANativeWindow_getHeight(win));

   struct ANativeWindowBuffer *nb = NULL;
   int fence = -1;
   if (deq) {
      int derr = deq(win, &nb, &fence);
      LOGI("probe dequeueBuffer (before connect) -> %d (%s)", derr, derr ? strerror(-derr) : "ok");
      if (derr == 0 && cancel)
         cancel(win, nb, fence);

      /* Android 的 Surface 生产端必须先 api_connect 才能 dequeue；
       * 这一步平时由厂商 libEGL 代做。perform 在结构体偏移 0x98
       * （由 libnativewindow 的 ANativeWindow_lock thunk 反汇编得到）。 */
      typedef int (*PFN_perform)(ANativeWindow *, int, ...);
      typedef int (*PFN_setUsage)(ANativeWindow *, uint64_t);
      PFN_perform perform = *(PFN_perform *)((char *)win + 0x98);
      PFN_setUsage set_usage = nwlib ? (PFN_setUsage)dlsym(nwlib, "ANativeWindow_setUsage") : NULL;
      LOGI("perform=%p setUsage=%p", (void *)perform, (void *)set_usage);
      /* 现在交给 Mesa 自己去做 setUsage + api_connect（验证修复） */
      (void)perform; (void)set_usage;
      nb = NULL; fence = -1;
      derr = deq(win, &nb, &fence);
      LOGI("probe dequeueBuffer (after connect) -> %d (%s)", derr, derr ? strerror(-derr) : "ok");
      if (derr == 0 && cancel) {
         cancel(win, nb, fence);
         LOGI("cancel ok");
      }
   }

   char path[600];
   snprintf(path, sizeof(path), "%s/libEGL_mesa_gl.so", g_libdir);
   LOGI("dlopen %s", path);
   void *egl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!egl) { LOGI("dlopen failed: %s", dlerror()); return; }

   PFN_getproc getproc = (PFN_getproc)dlsym(egl, "eglGetProcAddress");
   PFN_eglInitialize p_init = (PFN_eglInitialize)find_proc(egl, getproc, "eglInitialize");
   PFN_eglChooseConfig p_choose = (PFN_eglChooseConfig)find_proc(egl, getproc, "eglChooseConfig");
   PFN_eglCreateWindowSurface p_win = (PFN_eglCreateWindowSurface)find_proc(egl, getproc, "eglCreateWindowSurface");
   PFN_eglCreateContext p_ctx = (PFN_eglCreateContext)find_proc(egl, getproc, "eglCreateContext");
   PFN_eglMakeCurrent p_make = (PFN_eglMakeCurrent)find_proc(egl, getproc, "eglMakeCurrent");
   PFN_eglSwapBuffers p_swap = (PFN_eglSwapBuffers)find_proc(egl, getproc, "eglSwapBuffers");
   PFN_eglGetError p_err = (PFN_eglGetError)find_proc(egl, getproc, "eglGetError");
   PFN_eglQueryString p_qs = (PFN_eglQueryString)find_proc(egl, getproc, "eglQueryString");
   PFN_eglGetConfigAttrib p_cfgattr = (PFN_eglGetConfigAttrib)find_proc(egl, getproc, "eglGetConfigAttrib");
   PFN_eglBindAPI p_bind = (PFN_eglBindAPI)find_proc(egl, getproc, "eglBindAPI");
   PFN_eglGetPlatformDisplay p_gpd =
      (PFN_eglGetPlatformDisplay)find_proc(egl, getproc, "eglGetPlatformDisplay");

   LOGI("symbols: init=%p choose=%p win=%p ctx=%p make=%p swap=%p gpd=%p",
        (void *)p_init, (void *)p_choose, (void *)p_win, (void *)p_ctx,
        (void *)p_make, (void *)p_swap, (void *)p_gpd);
   if (!p_init || !p_choose || !p_win || !p_ctx || !p_make || !p_swap || !p_gpd) {
      LOGI("missing EGL symbols");
      return;
   }

   EGLDisplay dpy = p_gpd(EGL_PLATFORM_ANDROID_KHR, EGL_DEFAULT_DISPLAY, NULL);
   LOGI("display=%p", (void *)dpy);
   EGLint major = 0, minor = 0;
   if (!p_init(dpy, &major, &minor)) { LOGI("eglInitialize failed 0x%x", p_err()); return; }
   LOGI("EGL %d.%d apis=%s", major, minor, p_qs(dpy, EGL_CLIENT_APIS));
   /* FCL: 桌面 GL 必须先 bind API，再 chooseConfig，否则拿到的 config 不带 EGL_OPENGL_BIT */
   if (p_bind) p_bind(EGL_OPENGL_API); /* FCL: 先绑 API 再选 config */

   const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, 0x8 /*EGL_OPENGL_BIT*/,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
   };
   const EGLint cfg_attr_es[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
   };
   EGLConfig cfg = NULL; EGLint n = 0;
   if (!p_choose(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
      LOGI("chooseConfig(EGL_OPENGL_BIT) failed 0x%x, retry ES2", p_err());
      if (p_bind) p_bind(EGL_OPENGL_ES_API);
      if (!p_choose(dpy, cfg_attr_es, &cfg, 1, &n) || n < 1) { LOGI("chooseConfig failed 0x%x", p_err()); return; }
   }
   EGLint d = 0; p_cfgattr(dpy, cfg, EGL_DEPTH_SIZE, &d);
   LOGI("config ok depth=%d", d);

   EGLSurface surf = p_win(dpy, cfg, win, NULL);
   LOGI("window surface=%p err=0x%x", (void *)surf, surf ? 0 : p_err());
   if (!surf) return;

   /* 用桌面 GL（立即模式），方便画三角形复现几何路径 */
   if (p_bind) p_bind(EGL_OPENGL_API);
   /* 请求与 MC 一致的 4.6 Core；失败则退回默认上下文 */
   const EGLint ctx46[] = {
      0x3098 /*EGL_CONTEXT_MAJOR_VERSION_KHR*/, 4,
      0x30FB /*EGL_CONTEXT_MINOR_VERSION_KHR*/, 6,
      0x30FD /*EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR*/, 0x1 /*CORE*/,
      EGL_NONE
   };
   EGLContext ctx = p_ctx(dpy, cfg, EGL_NO_CONTEXT, ctx46);
   LOGI("context(4.6 core)=%p err=0x%x", (void *)ctx, ctx ? 0 : p_err());
   if (!ctx)
      ctx = p_ctx(dpy, cfg, EGL_NO_CONTEXT, NULL);
   LOGI("context(desktop GL)=%p err=0x%x", (void *)ctx, ctx ? 0 : p_err());
   if (!ctx) return;

   EGLBoolean mc = p_make(dpy, surf, surf, ctx);
   LOGI("makeCurrent=%d err=0x%x", mc, mc ? 0 : p_err());
   if (!mc) return;

   typedef void (*PFN_glClearColor)(float, float, float, float);
   typedef void (*PFN_glClear)(unsigned);
   typedef const unsigned char *(*PFN_glGetString)(unsigned);
   PFN_glClearColor clear_color = (PFN_glClearColor)find_proc(egl, getproc, "glClearColor");
   PFN_glClear clear = (PFN_glClear)find_proc(egl, getproc, "glClear");
   PFN_glGetString get_string = (PFN_glGetString)(void *)find_proc(egl, getproc, "glGetString");
   LOGI("glClearColor=%p glClear=%p", (void *)clear_color, (void *)clear);
   if (get_string)
      LOGI("GL_VERSION=%s GL_RENDERER=%s", get_string(0x1F02), get_string(0x1F01));

   typedef void (*PFN_glEnable)(unsigned);
   typedef void (*PFN_glBegin)(unsigned);
   typedef void (*PFN_glEnd)(void);
   typedef void (*PFN_glVertex3f)(float, float, float);
   typedef void (*PFN_glColor3f)(float, float, float);
   typedef void (*PFN_glViewport)(int, int, int, int);
   typedef unsigned (*PFN_glGetError)(void);
   PFN_glEnable enable = (PFN_glEnable)find_proc(egl, getproc, "glEnable");
   PFN_glBegin begin = (PFN_glBegin)find_proc(egl, getproc, "glBegin");
   PFN_glEnd end = (PFN_glEnd)find_proc(egl, getproc, "glEnd");
   PFN_glVertex3f vertex3f = (PFN_glVertex3f)find_proc(egl, getproc, "glVertex3f");
   PFN_glColor3f color3f = (PFN_glColor3f)find_proc(egl, getproc, "glColor3f");
   PFN_glViewport viewport = (PFN_glViewport)find_proc(egl, getproc, "glViewport");
   PFN_glGetError get_error = (PFN_glGetError)find_proc(egl, getproc, "glGetError");
   LOGI("draw funcs: enable=%p begin=%p frontend vertex=%p", (void *)enable,
        (void *)begin, (void *)vertex3f);

   {
      extern int ubwc_selftest(void *, void *);
      LOGI("UBWC selftest begin");
      int rc = ubwc_selftest(egl, (void *)getproc);
      LOGI("UBWC selftest rc=%d", rc);
      extern int ubwc_selftest_present(void *, void *, int, int, void *(*)(void *, void *), void *, void *);
      extern int ubwc_selftest_present_sz(void *, void *, int, int, void *(*)(void *, void *), void *, void *, int);
      ubwc_selftest_present_sz(egl, (void *)getproc, 2880, 1800,
                               (void *(*)(void *, void *))p_swap, (void *)dpy, (void *)surf, 1024);
      ubwc_selftest_present_sz(egl, (void *)getproc, 2880, 1800,
                               (void *(*)(void *, void *))p_swap, (void *)dpy, (void *)surf, 64);
   }

   if (viewport) viewport(0, 0, 2880, 1800);
   if (enable) { enable(0x0B71 /*DEPTH_TEST*/); }

   for (int i = 0; i < 600; i++) {
      if (clear_color && clear) {
         clear_color(0.1f, 0.1f, 0.5f, 1.0f);
         clear(0x4000 | 0x0100 /*COLOR|DEPTH*/);
      }
      if (begin && end && vertex3f && color3f) {
         float t = i * 0.02f;
         color3f(1.0f, 0.5f, 0.0f);
         begin(0x0004 /*GL_TRIANGLES*/);
         vertex3f(0.0f, 0.9f, 0.0f);
         vertex3f(-0.9f, -0.9f, 0.0f);
         vertex3f(0.9f, -0.9f, 0.0f);
         /* 再画一批不同深度的三角形，制造较多 tile 写入 */
         for (int k = 0; k < 64; k++) {
            float x = -0.9f + (k % 8) * 0.22f;
            float y = -0.9f + (k / 8) * 0.22f;
            color3f(0.2f + 0.01f * k, 0.8f, 0.3f);
            vertex3f(x, y, 0.5f + 0.001f * k);
            vertex3f(x + 0.2f, y, 0.5f + 0.001f * k);
            vertex3f(x, y + 0.2f, 0.5f + 0.001f * k);
         }
         end();
      }
      if (!p_swap(dpy, surf)) {
         LOGI("swapBuffers failed at frame %d err=0x%x", i, p_err());
         break;
      }
      if (i == 0 && get_error) LOGI("first swap OK, glGetError=0x%x", get_error());
      usleep(16000);
   }
}

static void handle_cmd(struct android_app *app, int32_t cmd) {
   if (cmd == APP_CMD_INIT_WINDOW && app->window) {
      LOGI("APP_CMD_INIT_WINDOW window=%p", (void *)app->window);
      /* 默认是 RGB_565，这里改成 RGBA_8888（与 FCL 的窗口一致） */
      int gerr = ANativeWindow_setBuffersGeometry(app->window, 0, 0, 1 /*RGBA_8888*/);
      LOGI("setBuffersGeometry(RGBA_8888) -> %d", gerr);
      run_test(app->window);
      LOGI("test finished");
   }
}

/* 试验钩子：/data/local/tmp/gltest.env 里每行 KEY=VALUE，在 dlopen 驱动前生效，
 * 免得每次换 FD_MESA_DEBUG 都要重编 APK。 */
static void load_env_file(void)
{
   FILE *fp = fopen("/data/local/tmp/gltest.env", "r");
   if (!fp)
      return;
   char line[512];
   while (fgets(line, sizeof(line), fp)) {
      char *nl = strchr(line, '\n');
      if (nl) *nl = 0;
      char *eq = strchr(line, '=');
      if (!eq || line[0] == '#')
         continue;
      *eq = 0;
      setenv(line, eq + 1, 1);
      LOGI("env hook: %s=%s", line, eq + 1);
   }
   fclose(fp);
}

void android_main(struct android_app *app) {
   load_env_file();
   self_dir();
   /* stderr/stdout 在 NativeActivity 里默认不进 logcat，写到私有目录方便排查 */
   char logpath[600];
   snprintf(logpath, sizeof(logpath), "%s/gltest.log",
            app->activity->internalDataPath);
   if (!freopen(logpath, "w", stderr)) {
      LOGI("freopen %s failed", logpath);
   }
   setvbuf(stderr, NULL, _IONBF, 0);
   LOGI("android_main libdir=%s log=%s", g_libdir, logpath);

   /* 探针：能否 dlopen 插件 shim（和 FCL 里失败的是同一个文件） */
   {
      char shim[700];
      snprintf(shim, sizeof(shim), "%s/libEGL_freedreno.so", g_libdir);
      dlerror();
      void *h = dlopen(shim, RTLD_LAZY | RTLD_GLOBAL);
      const char *e = dlerror();
      LOGI("probe dlopen(shim)=%p err=%s", h, e ? e : "(null)");
      if (h)
         LOGI("probe dlsym eglGetDisplay=%p", dlsym(h, "eglGetDisplay"));

      /* 跨 App：直接 dlopen FCL 插件目录里的同一个 .so（模拟 LWJGL hook 的调用） */
      const char *fcl_plugin =
         "/data/app/pkg.freedreno.kgsl/lib/arm64/libEGL_freedreno.so";
      dlerror();
      void *h2 = dlopen(fcl_plugin, 0x101 /* RTLD_LAZY|RTLD_GLOBAL */);
      const char *e2 = dlerror();
      LOGI("probe dlopen(fcl plugin)=%p err=%s", h2, e2 ? e2 : "(null)");
      if (!h2) {
         struct stat st;
         LOGI("  stat=%d size=%lld", stat(fcl_plugin, &st),
              (long long)(st.st_size));
      }
   }
   app->onAppCmd = handle_cmd;
   for (;;) {
      int events;
      struct android_poll_source *source;
      int timeout = -1;
      while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
         if (source) source->process(app, source);
         if (app->destroyRequested) return;
         timeout = 0;
      }
   }
}
