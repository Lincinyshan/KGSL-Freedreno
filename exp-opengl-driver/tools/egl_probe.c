/* egl_probe.c — 设备端验证自编 libEGL_freedreno.so（EGL → freedreno/KGSL）。
 *
 * 用 dlopen 加载 libEGL，走 android 平台 + /dev/kgsl-3d0，
 * 建 surfaceless 上下文 + FBO，清屏并读回像素。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

typedef void *(*PFN_eglGetPlatformDisplay)(EGLenum, void *, const EGLint *);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
typedef const char *(*PFN_eglQueryString)(EGLDisplay, EGLint);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLBoolean (*PFN_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef void *(*PFN_eglGetProcAddress)(const char *);
typedef EGLint (*PFN_eglGetError)(void);

/* 先直接 dlsym，失败再走 eglGetProcAddress（与 FCL 的 egl_loader 一致） */
#define LOAD(sym) do { \
   p_##sym = (void *)dlsym(egl, #sym); \
   if (!p_##sym && p_eglGetProcAddress) p_##sym = (void *)p_eglGetProcAddress(#sym); \
   if (!p_##sym) { printf("[egl] missing symbol: %s\n", #sym); return 2; } } while (0)

int main(int argc, char **argv) {
   const char *path = getenv("EGL_LIB");
   if (!path) path = argc > 1 ? argv[1] : "/data/local/tmp/libEGL_freedreno.so";

   void *egl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!egl) { printf("[egl] dlopen(%s) failed: %s\n", path, dlerror()); return 1; }
   printf("[egl] loaded %s\n", path);

   PFN_eglGetPlatformDisplay p_eglGetPlatformDisplay;
   PFN_eglInitialize p_eglInitialize;
   PFN_eglQueryString p_eglQueryString;
   PFN_eglBindAPI p_eglBindAPI;
   PFN_eglChooseConfig p_eglChooseConfig;
   PFN_eglGetConfigAttrib p_eglGetConfigAttrib;
   PFN_eglCreateContext p_eglCreateContext;
   PFN_eglMakeCurrent p_eglMakeCurrent;
   PFN_eglGetProcAddress p_eglGetProcAddress;
   PFN_eglGetError p_eglGetError;

   p_eglGetProcAddress = (void *)dlsym(egl, "eglGetProcAddress");
   LOAD(eglGetPlatformDisplay);
   LOAD(eglInitialize);
   LOAD(eglQueryString);
   LOAD(eglBindAPI);
   LOAD(eglChooseConfig);
   LOAD(eglGetConfigAttrib);
   LOAD(eglCreateContext);
   LOAD(eglMakeCurrent);
   LOAD(eglGetError);

   EGLDisplay dpy = p_eglGetPlatformDisplay(EGL_PLATFORM_ANDROID_KHR, EGL_DEFAULT_DISPLAY, NULL);
   printf("[egl] eglGetPlatformDisplay(android) = %p\n", (void *)dpy);
   fflush(stdout);

   EGLint major = 0, minor = 0;
   if (!p_eglInitialize(dpy, &major, &minor)) {
      printf("[egl] eglInitialize FAILED: 0x%x\n", p_eglGetError());
      return 3;
   }
   printf("[egl] EGL %d.%d vendor=%s version=%s\n", major, minor,
          p_eglQueryString(dpy, EGL_VENDOR), p_eglQueryString(dpy, EGL_VERSION));
   printf("[egl] client apis=%s\n", p_eglQueryString(dpy, EGL_CLIENT_APIS));
   fflush(stdout);

   if (!p_eglBindAPI(EGL_OPENGL_API)) { printf("[egl] bind OpenGL FAILED: 0x%x\n", p_eglGetError()); return 4; }

   const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
   };
   EGLConfig cfg = NULL;
   EGLint ncfg = 0;
   if (!p_eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
      printf("[egl] eglChooseConfig FAILED: 0x%x n=%d\n", p_eglGetError(), ncfg);
      return 5;
   }
   printf("[egl] config ok\n"); fflush(stdout);

   /* 诊断：用 FCL gl_bridge 的挑法（WINDOW|PBUFFER + ES2 bit）看 depth/stencil */
   {
      const EGLint fcl_attr[] = {
         EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
         EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
      };
      EGLConfig fc = NULL;
      EGLint n = 0, d = 0, s = 0, r = 0, a = 0, sm = 0;
      if (p_eglChooseConfig(dpy, fcl_attr, &fc, 1, &n) && n > 0) {
         p_eglGetConfigAttrib(dpy, fc, EGL_DEPTH_SIZE, &d);
         p_eglGetConfigAttrib(dpy, fc, EGL_STENCIL_SIZE, &s);
         p_eglGetConfigAttrib(dpy, fc, EGL_RED_SIZE, &r);
         p_eglGetConfigAttrib(dpy, fc, EGL_ALPHA_SIZE, &a);
         p_eglGetConfigAttrib(dpy, fc, EGL_SAMPLES, &sm);
         printf("[egl] FCL-style config: R=%d A=%d D=%d S=%d samples=%d\n", r, a, d, s, sm);
      } else {
         printf("[egl] FCL-style eglChooseConfig FAILED: 0x%x n=%d\n", p_eglGetError(), n);
      }
      fflush(stdout);
   }

   EGLContext ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
   if (ctx == EGL_NO_CONTEXT) { printf("[egl] eglCreateContext FAILED: 0x%x\n", p_eglGetError()); return 6; }
   printf("[egl] context ok\n"); fflush(stdout);

   if (!p_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
      printf("[egl] eglMakeCurrent(surfaceless) FAILED: 0x%x\n", p_eglGetError());
      return 7;
   }
   printf("[egl] current ok\n"); fflush(stdout);

   const char *(*p_glGetString)(GLenum) = (void *)p_eglGetProcAddress("glGetString");
   void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = (void *)p_eglGetProcAddress("glClearColor");
   void (*p_glClear)(GLbitfield) = (void *)p_eglGetProcAddress("glClear");
   void (*p_glFinish)(void) = (void *)p_eglGetProcAddress("glFinish");
   void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) = (void *)p_eglGetProcAddress("glReadPixels");
   void (*p_glGenFramebuffers)(GLsizei, GLuint *) = (void *)p_eglGetProcAddress("glGenFramebuffers");
   void (*p_glBindFramebuffer)(GLenum, GLuint) = (void *)p_eglGetProcAddress("glBindFramebuffer");
   void (*p_glGenRenderbuffers)(GLsizei, GLuint *) = (void *)p_eglGetProcAddress("glGenRenderbuffers");
   void (*p_glBindRenderbuffer)(GLenum, GLuint) = (void *)p_eglGetProcAddress("glBindRenderbuffer");
   void (*p_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = (void *)p_eglGetProcAddress("glRenderbufferStorage");
   void (*p_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = (void *)p_eglGetProcAddress("glFramebufferRenderbuffer");
   void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei) = (void *)p_eglGetProcAddress("glViewport");

   if (!p_glGetString || !p_glClear) { printf("[egl] GL entry points missing\n"); return 8; }
   printf("[egl] GL_VERSION  = %s\n", p_glGetString(GL_VERSION));
   printf("[egl] GL_RENDERER = %s\n", p_glGetString(GL_RENDERER));
   printf("[egl] GL_VENDOR   = %s\n", p_glGetString(GL_VENDOR));
   fflush(stdout);

   GLuint fbo = 0, rbo = 0;
   p_glGenFramebuffers(1, &fbo);
   p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   p_glGenRenderbuffers(1, &rbo);
   p_glBindRenderbuffer(GL_RENDERBUFFER, rbo);
   p_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 256, 256);
   p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
   p_glViewport(0, 0, 256, 256);
   p_glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
   p_glClear(GL_COLOR_BUFFER_BIT);
   p_glFinish();

   unsigned char px[4] = {0};
   p_glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   printf("[egl] clear pixel = %u,%u,%u,%u (expect 0,255,0,255)\n", px[0], px[1], px[2], px[3]);
   printf("[egl] RESULT: %s\n", (px[1] > 200 && px[0] < 40) ? "OK" : "FAIL");
   fflush(stdout);
   return (px[1] > 200) ? 0 : 9;
}
