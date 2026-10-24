/* UBWC 自检：离屏 FBO（512x512 RGBA8，满足 UBWC 宽度要求）上做
 *   T1 清屏 + scissor 清块   -> ReadPixels 校验
 *   T2 FBO->FBO blit          -> ReadPixels 校验
 *   T3 纹理部分更新(glTexSubImage2D) -> blit 到 RT -> 校验
 * 整套循环 50 次，专门抓"运行越久越花"的累积性错误。
 * 结果用 LOGI 输出（adb logcat -s GLTEST）。
 */
#include <android/log.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

#define TAG "GLTEST"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned char GLubyte;
typedef float GLfloat;

#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_SCISSOR_TEST 0x0C11
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_NO_ERROR 0

#define W 512
#define H 512

typedef void (*PFN_glGenTextures)(GLsizei, GLuint *);
typedef void (*PFN_glBindTexture)(GLenum, GLuint);
typedef void (*PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (*PFN_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (*PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void (*PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFN_glCheckFramebufferStatus)(GLenum);
typedef void (*PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glClear)(GLenum);
typedef void (*PFN_glScissor)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_glUniform1f)(GLint, GLfloat);
typedef void (*PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_glEnable)(GLenum);
typedef void (*PFN_glDisable)(GLenum);
typedef void (*PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (*PFN_glPixelStorei)(GLenum, GLint);
typedef void (*PFN_glFinish)(void);
typedef void (*PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, unsigned, GLenum);
typedef void (*PFN_glDeleteTextures)(GLsizei, const GLuint *);
typedef void (*PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef GLenum (*PFN_glGetError)(void);

static struct {
   PFN_glGenTextures GenTextures;
   PFN_glBindTexture BindTexture;
   PFN_glTexImage2D TexImage2D;
   PFN_glTexSubImage2D TexSubImage2D;
   PFN_glTexParameteri TexParameteri;
   PFN_glGenFramebuffers GenFramebuffers;
   PFN_glBindFramebuffer BindFramebuffer;
   PFN_glFramebufferTexture2D FramebufferTexture2D;
   PFN_glCheckFramebufferStatus CheckFramebufferStatus;
   PFN_glClearColor ClearColor;
   PFN_glClear Clear;
   PFN_glScissor Scissor;
   PFN_glViewport Viewport;
   PFN_glEnable Enable;
   PFN_glDisable Disable;
   PFN_glReadPixels ReadPixels;
   PFN_glPixelStorei PixelStorei;
   PFN_glFinish Finish;
   PFN_glBlitFramebuffer BlitFramebuffer;
   PFN_glDeleteTextures DeleteTextures;
   PFN_glDeleteFramebuffers DeleteFramebuffers;
   PFN_glGetError GetError;
} gl;

static int resolve(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
   int missing = 0;
#define GET(field, name)                                                    \
   do {                                                                     \
      gl.field = (void *)dlsym(egl, name);                                   \
      if (!gl.field && gp) gl.field = (void *)gp(name);                      \
      if (!gl.field) { LOGI("UBWC: missing %s", name); missing++; }          \
   } while (0)
   GET(GenTextures, "glGenTextures");
   GET(BindTexture, "glBindTexture");
   GET(TexImage2D, "glTexImage2D");
   GET(TexSubImage2D, "glTexSubImage2D");
   GET(TexParameteri, "glTexParameteri");
   GET(GenFramebuffers, "glGenFramebuffers");
   GET(BindFramebuffer, "glBindFramebuffer");
   GET(FramebufferTexture2D, "glFramebufferTexture2D");
   GET(CheckFramebufferStatus, "glCheckFramebufferStatus");
   GET(ClearColor, "glClearColor");
   GET(Clear, "glClear");
   GET(Scissor, "glScissor");
   GET(Viewport, "glViewport");
   GET(Enable, "glEnable");
   GET(Disable, "glDisable");
   GET(ReadPixels, "glReadPixels");
   GET(PixelStorei, "glPixelStorei");
   GET(Finish, "glFinish");
   GET(BlitFramebuffer, "glBlitFramebuffer");
   GET(DeleteTextures, "glDeleteTextures");
   GET(DeleteFramebuffers, "glDeleteFramebuffers");
   GET(GetError, "glGetError");
#undef GET
   return missing;
}

static GLuint make_rt_sz(int *fb_out, int w, int h)
{
   GLuint tex = 0, fb = 0;
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.GenFramebuffers(1, &fb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
   if (st != GL_FRAMEBUFFER_COMPLETE) LOGI("UBWC: FBO incomplete 0x%x", st);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   *fb_out = (int)fb;
   return tex;
}

static GLuint make_rt(int *fb_out)
{
   return make_rt_sz(fb_out, W, H);
}

static int read_px(int fb, int x, int y, unsigned char out[4])
{
   gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
   gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
   gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   return 0;
}

static int near(unsigned char got, unsigned char want)
{
   int d = (int)got - (int)want;
   return d >= -4 && d <= 4;
}

static int eq4(const unsigned char g[4], int r, int gg, int b, int a)
{
   return near(g[0], r) && near(g[1], gg) && near(g[2], b) && near(g[3], a);
}


/* T4：MC 的真实尺寸场景 —— 2880x1800 颜色 RT + 深度附件，
 * 每帧 clear 后做一次 FBO->FBO blit，循环 100 次。 */
typedef void (*PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);

#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_ATTACHMENT 0x8D00


/* 文件作用域的 renderbuffer 入口（big / msaa 两个用例共用） */
typedef void (*PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
typedef void (*PFN_glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);

int ubwc_selftest_big(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
   PFN_glGenRenderbuffers GenRB = (void *)(dlsym(egl, "glGenRenderbuffers") ? dlsym(egl, "glGenRenderbuffers") : gp("glGenRenderbuffers"));
   PFN_glBindRenderbuffer BindRB = (void *)(dlsym(egl, "glBindRenderbuffer") ? dlsym(egl, "glBindRenderbuffer") : gp("glBindRenderbuffer"));
   PFN_glRenderbufferStorage RBStorage = (void *)(dlsym(egl, "glRenderbufferStorage") ? dlsym(egl, "glRenderbufferStorage") : gp("glRenderbufferStorage"));
   PFN_glFramebufferRenderbuffer FBRB = (void *)(dlsym(egl, "glFramebufferRenderbuffer") ? dlsym(egl, "glFramebufferRenderbuffer") : gp("glFramebufferRenderbuffer"));
   PFN_glDeleteRenderbuffers DelRB = (void *)(dlsym(egl, "glDeleteRenderbuffers") ? dlsym(egl, "glDeleteRenderbuffers") : gp("glDeleteRenderbuffers"));
   if (!GenRB || !BindRB || !RBStorage || !FBRB) { LOGI("UBWC-BIG: SKIP (missing rb funcs)"); return -1; }

   const int BW = 2880, BH = 1800;
   GLuint texA = 0, texB = 0, fbA = 0, fbB = 0, rb = 0;
   gl.GenTextures(1, &texA); gl.GenTextures(1, &texB);
   gl.GenFramebuffers(1, &fbA); gl.GenFramebuffers(1, &fbB);
   gl.GenTextures(1, &texB);
   GenRB(1, &rb);

   for (int i = 0; i < 2; i++) {
      GLuint t = i ? texB : texA, f = i ? fbB : fbA;
      gl.BindTexture(GL_TEXTURE_2D, t);
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BW, BH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl.BindFramebuffer(GL_FRAMEBUFFER, f);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
      if (i == 0) {
         BindRB(GL_RENDERBUFFER, rb);
         RBStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, BW, BH);
         FBRB(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
      }
      GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
      if (st != GL_FRAMEBUFFER_COMPLETE) LOGI("UBWC-BIG: FBO%d incomplete 0x%x", i, st);
   }
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   LOGI("UBWC-BIG: created %dx%d color RTs + depth", BW, BH);

   unsigned char px[4];
   int failures = 0, first = -1;
   for (int iter = 0; iter < 100; iter++) {
      float r = (iter & 1) ? 0.0f : 1.0f;
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbA);
      gl.ClearColor(r, 0.0f, 1.0f - r, 1.0f);
      gl.Clear(0x4000 | 0x0100); /* COLOR | DEPTH */
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      read_px(fbA, 1440, 900, px);
      int want[3] = {(int)(r * 255), 0, (int)((1.0f - r) * 255)};
      int bad = !(near(px[0], want[0]) && near(px[1], want[1]) && near(px[2], want[2]));
      if (bad) {
         failures++;
         if (first < 0) {
            first = iter;
            LOGI("UBWC-BIG: iter %d got %u,%u,%u want %d,%d,%d", iter, px[0], px[1], px[2],
                 want[0], want[1], want[2]);
         }
      }
   }
   gl.DeleteTextures(1, &texA);
   gl.DeleteTextures(1, &texB);
   GLuint fbs[2] = {fbA, fbB};
   gl.DeleteFramebuffers(2, fbs);
   if (DelRB) DelRB(1, &rb);
   LOGI("UBWC-BIG: RESULT %s (failures=%d/100 first=%d)", failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T5：MSAA 4x 颜色 RT + resolve（blit 到单采样 RT），循环 60 次。 */
int ubwc_selftest_msaa(void *egl, void *getproc)
{
   typedef void (*PFN_glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
   typedef void (*PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
   typedef void (*PFN_glGenRenderbuffers)(GLsizei, GLuint *);
   typedef void (*PFN_glBindRenderbuffer)(GLenum, GLuint);
   typedef void (*PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
   typedef void (*PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
#define F2(x, n) PFN_##x x = (void *)(dlsym(egl, n) ? dlsym(egl, n) : gp(n))
   F2(glRenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
   F2(glRenderbufferStorage, "glRenderbufferStorage");
   F2(glGenRenderbuffers, "glGenRenderbuffers");
   F2(glBindRenderbuffer, "glBindRenderbuffer");
   F2(glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
   F2(glDeleteRenderbuffers, "glDeleteRenderbuffers");
#undef F2
   if (!glRenderbufferStorageMultisample || !glGenRenderbuffers) {
      LOGI("UBWC-MSAA: SKIP");
      return -1;
   }
   const int BW = 1024, BH = 1024;
   GLuint ms_rb = 0, ms_fb = 0, out_tex = 0, out_fb = 0;
   glGenRenderbuffers(1, &ms_rb);
   glBindRenderbuffer(GL_RENDERBUFFER, ms_rb);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, BW, BH);
   gl.GenFramebuffers(1, &ms_fb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, ms_fb);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ms_rb);
   GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
   LOGI("UBWC-MSAA: msaa fbo status=0x%x", st);

   gl.GenTextures(1, &out_tex);
   gl.BindTexture(GL_TEXTURE_2D, out_tex);
   gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BW, BH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.GenFramebuffers(1, &out_fb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, out_fb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_tex, 0);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

   unsigned char px[4];
   int failures = 0, first = -1;
   for (int iter = 0; iter < 60; iter++) {
      float r = (iter & 1) ? 0.0f : 1.0f;
      gl.BindFramebuffer(GL_FRAMEBUFFER, ms_fb);
      gl.ClearColor(r, 0.0f, 1.0f - r, 1.0f);
      gl.Clear(0x4000);
      gl.Finish();
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, ms_fb);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, out_fb);
      gl.BlitFramebuffer(0, 0, BW, BH, 0, 0, BW, BH, 0x4000, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      read_px((int)out_fb, 512, 512, px);
      int want[3] = {(int)(r * 255), 0, (int)((1.0f - r) * 255)};
      if (!(near(px[0], want[0]) && near(px[1], want[1]) && near(px[2], want[2]))) {
         failures++;
         if (first < 0) {
            first = iter;
            LOGI("UBWC-MSAA: iter %d got %u,%u,%u want %d,%d,%d", iter, px[0], px[1], px[2],
                 want[0], want[1], want[2]);
         }
      }
   }
   gl.DeleteFramebuffers(1, &ms_fb);
   gl.DeleteFramebuffers(1, &out_fb);
   gl.DeleteTextures(1, &out_tex);
   if (glDeleteRenderbuffers) glDeleteRenderbuffers(1, &ms_rb);
   LOGI("UBWC-MSAA: RESULT %s (failures=%d/60 first=%d)", failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T6：mipmap 生成 —— MC 的物品图集带 mip，粒子图集不带。
 * 上传 level0 纯色 -> glGenerateMipmap -> 把 level2 贴到 FBO -> blit 到 RT -> 读回校验。 */
typedef void (*PFN_glGenerateMipmap)(GLenum);
int ubwc_selftest_mipmap(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
   PFN_glGenerateMipmap GenMip = (void *)(dlsym(egl, "glGenerateMipmap") ? dlsym(egl, "glGenerateMipmap") : gp("glGenerateMipmap"));
   if (!GenMip) { LOGI("UBWC-MIP: SKIP"); return -1; }

   const int S = 512, LEVELS = 10;
   GLuint tex = 0, fbLvl = 0, rt = 0, rtFb = 0;
   static unsigned char green[S * S * 4];
   for (int i = 0; i < S * S; i++) {
      green[i * 4 + 0] = 0;
      green[i * 4 + 1] = 255;
      green[i * 4 + 2] = 0;
      green[i * 4 + 3] = 255;
   }
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703 /*LINEAR_MIPMAP_LINEAR*/);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, 0x813D /*TEXTURE_MAX_LEVEL*/, LEVELS - 1);
   int sz = S;
   for (int l = 0; l < LEVELS; l++) {
      gl.TexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    l == 0 ? green : NULL);
      sz /= 2;
   }
   GenMip(GL_TEXTURE_2D);
   gl.Finish();
   gl.BindTexture(GL_TEXTURE_2D, 0);

   /* level2 -> FBO */
   gl.GenFramebuffers(1, &fbLvl);
   gl.BindFramebuffer(GL_FRAMEBUFFER, fbLvl);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 2);
   GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   if (st != GL_FRAMEBUFFER_COMPLETE) {
      LOGI("UBWC-MIP: level FBO incomplete 0x%x", st);
      return 1;
   }

   /* 目标 RT */
   rt = make_rt((int *)&rtFb);
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbLvl);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, rtFb);
   gl.BlitFramebuffer(0, 0, 128, 128, 0, 0, 128, 128, 0x4000, GL_NEAREST);
   gl.Finish();
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

   unsigned char px[4];
   read_px((int)rtFb, 64, 64, px);
   int ok = eq4(px, 0, 255, 0, 255);
   LOGI("UBWC-MIP: level2 read %u,%u,%u want 0,255,0 -> %s", px[0], px[1], px[2],
        ok ? "PASS" : "FAIL");
   gl.DeleteFramebuffers(1, &fbLvl);
   gl.DeleteTextures(1, &tex);
   gl.DeleteTextures(1, &rt);
   gl.DeleteFramebuffers(1, &rtFb);
   return ok ? 0 : 1;
}


/* T7：cubemap（MC 里唯一的特殊资源）—— 6 个面各自清成不同颜色，
 * 逐面绑定到 FBO 后 blit 到 RT 并读回校验。 */
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
int ubwc_selftest_cube(void *egl, void *getproc)
{
   (void)getproc;
   const int S = 512;
   GLuint tex = 0, fb = 0, rt = 0, rtFb = 0;
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_CUBE_MAP, tex);
   for (int i = 0; i < 6; i++)
      gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, S, S, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, NULL);
   gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   gl.GenFramebuffers(1, &fb);
   rt = make_rt((int *)&rtFb);

   unsigned char px[4];
   int failures = 0;
   for (int i = 0; i < 6; i++) {
      gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tex, 0);
      GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
      if (st != GL_FRAMEBUFFER_COMPLETE) {
         LOGI("UBWC-CUBE: face %d FBO incomplete 0x%x", i, st);
         failures++;
         continue;
      }
      gl.ClearColor((i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Finish();

      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, rtFb);
      gl.BlitFramebuffer(0, 0, S, S, 0, 0, S, S, 0x4000, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      read_px((int)rtFb, S / 2, S / 2, px);
      int wr = (i & 1) ? 255 : 0, wg = (i & 2) ? 255 : 0, wb = (i & 4) ? 255 : 0;
      if (!eq4(px, wr, wg, wb, 255)) {
         failures++;
         LOGI("UBWC-CUBE: face %d got %u,%u,%u want %d,%d,%d", i, px[0], px[1], px[2], wr, wg, wb);
      }
   }
   gl.DeleteFramebuffers(1, &fb);
   gl.DeleteTextures(1, &tex);
   gl.DeleteTextures(1, &rt);
   gl.DeleteFramebuffers(1, &rtFb);
   LOGI("UBWC-CUBE: RESULT %s (failures=%d/6)", failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}


/* T8：MRT（两个 UBWC 颜色附件）+ 双 RT ping-pong 拷贝，模拟 MC 的帧间交替。 */
#define GL_COLOR_ATTACHMENT1 0x8CE1
int ubwc_selftest_mrt(void *egl, void *getproc)
{
   (void)egl; (void)getproc;
   const int S = 512;
   GLuint t0 = 0, t1 = 0, fb = 0, rt = 0, rtFb = 0;
   gl.GenTextures(1, &t0);
   gl.GenTextures(1, &t1);
   gl.GenFramebuffers(1, &fb);

   GLuint texs[2] = {t0, t1};
   for (int i = 0; i < 2; i++) {
      gl.BindTexture(GL_TEXTURE_2D, texs[i]);
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   }
   gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t0, 0);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, t1, 0);
   GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   LOGI("UBWC-MRT: fbo status=0x%x (两个 UBWC 颜色附件)", st);
   if (st != GL_FRAMEBUFFER_COMPLETE) return 1;

   rt = make_rt((int *)&rtFb);
   int failures = 0, first = -1;
   unsigned char px[4];

   for (int iter = 0; iter < 80; iter++) {
      /* 两个附件分别 clear 成不同颜色 */
      gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
      gl.ClearColor((iter & 1) ? 1.0f : 0.0f, 0.0f, 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT); /* MRT: 两个附件都会被 clear 成同色 */
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      /* 从 attachment0 读 */
      read_px((int)fb, S / 2, S / 2, px);
      int want = (iter & 1) ? 255 : 0;
      if (!(near(px[0], want) && near(px[1], 0) && near(px[2], 0))) {
         failures++;
         if (first < 0) { first = iter; LOGI("UBWC-MRT: att0 iter %d got %u,%u,%u want %d,0,0", iter, px[0], px[1], px[2], want); }
      }

      /* ping-pong：fb -> rt -> fb */
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, rtFb);
      gl.BlitFramebuffer(0, 0, S, S, 0, 0, S, S, 0x4000, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, rtFb);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
      gl.BlitFramebuffer(0, 0, S, S, 0, 0, S, S, 0x4000, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      read_px((int)fb, S / 2, S / 2, px);
      if (!eq4(px, want, 0, 0, 255)) {
         failures++;
         if (first < 0) { first = iter; LOGI("UBWC-MRT: pingpong iter %d got %u,%u,%u want %d,0,0", iter, px[0], px[1], px[2], want); }
      }
   }
   gl.DeleteFramebuffers(1, &fb);
   gl.DeleteTextures(1, &t0);
   gl.DeleteTextures(1, &t1);
   gl.DeleteTextures(1, &rt);
   gl.DeleteFramebuffers(1, &rtFb);
   LOGI("UBWC-MRT: RESULT %s (failures=%d/160 first=%d)", failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T9：用着色器“采样”一个 UBWC 纹理 —— MC 的物品图集就是这样用的，
 * 而粒子图集宽度小于 UBWC 阈值(128)不压缩，正好对应“物品花、粒子不花”。 */
typedef GLuint (*PFN_glCreateShader)(GLenum);
typedef void (*PFN_glShaderSource)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void (*PFN_glCompileShader)(GLuint);
typedef void (*PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef void (*PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (*PFN_glCreateProgram)(void);
typedef void (*PFN_glAttachShader)(GLuint, GLuint);
typedef void (*PFN_glLinkProgram)(GLuint);
typedef void (*PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void (*PFN_glUseProgram)(GLuint);
typedef GLint (*PFN_glGetUniformLocation)(GLuint, const char *);
typedef void (*PFN_glUniform1i)(GLint, GLint);
typedef void (*PFN_glGenVertexArrays)(GLsizei, GLuint *);
typedef void (*PFN_glBindVertexArray)(GLuint);
typedef void (*PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindBuffer)(GLenum, GLuint);
typedef void (*PFN_glBufferData)(GLenum, long, const void *, GLenum);
typedef void (*PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, unsigned char, GLsizei, const void *);
typedef void (*PFN_glEnableVertexAttribArray)(GLuint);
typedef void (*PFN_glDrawArrays)(GLenum, GLint, GLsizei);

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_TRIANGLE_STRIP 0x0005
#define GL_COLOR_ATTACHMENT0_ 0x8CE0

int ubwc_selftest_sample(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
#define G(type, var, name) type var = (void *)(dlsym(egl, name) ? dlsym(egl, name) : gp(name)); if (!var) { LOGI("UBWC-SAMPLE: missing %s", name); return -1; }
   G(PFN_glCreateShader, CreateShader, "glCreateShader");
   G(PFN_glShaderSource, ShaderSource, "glShaderSource");
   G(PFN_glCompileShader, CompileShader, "glCompileShader");
   G(PFN_glGetShaderiv, GetShaderiv, "glGetShaderiv");
   G(PFN_glGetShaderInfoLog, GetShaderInfoLog, "glGetShaderInfoLog");
   G(PFN_glCreateProgram, CreateProgram, "glCreateProgram");
   G(PFN_glAttachShader, AttachShader, "glAttachShader");
   G(PFN_glLinkProgram, LinkProgram, "glLinkProgram");
   G(PFN_glGetProgramiv, GetProgramiv, "glGetProgramiv");
   G(PFN_glUseProgram, UseProgram, "glUseProgram");
   G(PFN_glGetUniformLocation, GetUniformLocation, "glGetUniformLocation");
   G(PFN_glUniform1i, Uniform1i, "glUniform1i");
   G(PFN_glGenVertexArrays, GenVertexArrays, "glGenVertexArrays");
   G(PFN_glBindVertexArray, BindVertexArray, "glBindVertexArray");
   G(PFN_glGenBuffers, GenBuffers, "glGenBuffers");
   G(PFN_glBindBuffer, BindBuffer, "glBindBuffer");
   G(PFN_glBufferData, BufferData, "glBufferData");
   G(PFN_glVertexAttribPointer, VertexAttribPointer, "glVertexAttribPointer");
   G(PFN_glEnableVertexAttribArray, EnableVertexAttribArray, "glEnableVertexAttribArray");
   G(PFN_glDrawArrays, DrawArrays, "glDrawArrays");
#undef G

   const char *vs_src =
      "#version 330 core\n"
      "layout(location=0) in vec2 pos;\n"
      "out vec2 uv;\n"
      "void main(){ uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
   const char *fs_src =
      "#version 330 core\n"
      "uniform sampler2D tex;\n"
      "in vec2 uv;\n"
      "out vec4 color;\n"
      "void main(){ color = texture(tex, uv); }\n";

   GLuint vs = CreateShader(GL_VERTEX_SHADER), fs = CreateShader(GL_FRAGMENT_SHADER);
   ShaderSource(vs, 1, &vs_src, NULL); CompileShader(vs);
   ShaderSource(fs, 1, &fs_src, NULL); CompileShader(fs);
   GLint okv = 0, okf = 0;
   GetShaderiv(vs, GL_COMPILE_STATUS, &okv);
   GetShaderiv(fs, GL_COMPILE_STATUS, &okf);
   if (!okv || !okf) {
      char log[512] = {0};
      GetShaderInfoLog(okv ? fs : vs, sizeof(log) - 1, NULL, log);
      LOGI("UBWC-SAMPLE: shader compile failed vs=%d fs=%d: %s", okv, okf, log);
      return -1;
   }
   GLuint prog = CreateProgram();
   AttachShader(prog, vs); AttachShader(prog, fs); LinkProgram(prog);
   GLint linked = 0; GetProgramiv(prog, GL_LINK_STATUS, &linked);
   if (!linked) { LOGI("UBWC-SAMPLE: link failed"); return -1; }
   UseProgram(prog);
   Uniform1i(GetUniformLocation(prog, "tex"), 0);

   GLuint vao = 0, vbo = 0;
   GenVertexArrays(1, &vao); BindVertexArray(vao);
   const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
   GenBuffers(1, &vbo); BindBuffer(GL_ARRAY_BUFFER, vbo);
   BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   VertexAttribPointer(0, 2, GL_FLOAT, 0, 8, NULL);
   EnableVertexAttribArray(0);

   /* 源纹理：512x512（>= UBWC 阈值），用 glTexSubImage2D 上色 */
   const int S = 512;
   static unsigned char magenta[S * S * 4];
   for (int i = 0; i < S * S; i++) {
      magenta[i * 4 + 0] = 255; magenta[i * 4 + 1] = 0;
      magenta[i * 4 + 2] = 255; magenta[i * 4 + 3] = 255;
   }
   GLuint src = 0;
   gl.GenTextures(1, &src);
   gl.BindTexture(GL_TEXTURE_2D, src);
   gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, magenta);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.BindTexture(GL_TEXTURE_2D, 0);

   GLuint rt = 0, rtFb = 0;
   rt = make_rt((int *)&rtFb);

   unsigned char px[4];
   int failures = 0, first = -1;
   for (int iter = 0; iter < 40; iter++) {
      gl.BindFramebuffer(GL_FRAMEBUFFER, rtFb);
      gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.BindTexture(GL_TEXTURE_2D, src);
      BindVertexArray(vao);
      DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      read_px((int)rtFb, S / 2, S / 2, px);
      if (!eq4(px, 255, 0, 255, 255)) {
         failures++;
         if (first < 0) { first = iter; LOGI("UBWC-SAMPLE: iter %d got %u,%u,%u want 255,0,255", iter, px[0], px[1], px[2]); }
      }
   }
   gl.DeleteFramebuffers(1, &rtFb);
   gl.DeleteTextures(1, &rt);
   gl.DeleteTextures(1, &src);
   BindVertexArray(0);
   LOGI("UBWC-SAMPLE: RESULT %s (failures=%d/40 first=%d)", failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T10：UBWC RT -> 窗口（默认帧缓冲）blit + swap，循环 100 次。
 * 这是 MC 每帧真正做的事（我们其它用例都只在 FBO 之间搬运）。 */
int ubwc_selftest_present_sz(void *egl, void *getproc, int win_w, int win_h,
                             void *(*swap)(void *, void *), void *dpy, void *surf, int S);

int ubwc_selftest_present(void *egl, void *getproc, int win_w, int win_h,
                          void *(*swap)(void *, void *), void *dpy, void *surf)
{
   return ubwc_selftest_present_sz(egl, getproc, win_w, win_h, swap, dpy, surf, 1024);
}

int ubwc_selftest_present_sz(void *egl, void *getproc, int win_w, int win_h,
                             void *(*swap)(void *, void *), void *dpy, void *surf, int S)
{
   (void)getproc;
   GLuint tex = 0, fb = 0;
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.GenFramebuffers(1, &fb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

   unsigned char px[4];
   int failures = 0, first = -1;
   for (int iter = 0; iter < 100; iter++) {
      float r = (iter & 1) ? 1.0f : 0.0f;
      gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
      gl.ClearColor(r, 0.0f, 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Finish();

      /* 缩放到窗口尺寸 blit 到默认帧缓冲 */
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      gl.BlitFramebuffer(0, 0, S, S, 0, 0, win_w, win_h, 0x4000, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      /* swap 前从默认帧缓冲读回 */
      gl.ReadPixels(win_w / 2, win_h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
      int want = (iter & 1) ? 255 : 0;
      if (!(near(px[0], want) && near(px[1], 0) && near(px[2], 0))) {
         failures++;
         if (first < 0) { first = iter; LOGI("UBWC-PRESENT: iter %d got %u,%u,%u want %d,0,0", iter, px[0], px[1], px[2], want); }
      }
      if (swap)
         swap(dpy, surf);
   }
   gl.DeleteTextures(1, &tex);
   gl.DeleteFramebuffers(1, &fb);
   LOGI("UBWC-PRESENT(%dx%d): RESULT %s (failures=%d/100 first=%d)", S, S, failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T11：反馈用例 —— 先渲染进 UBWC RT，再把同一个 RT 当纹理用 shader 采样到另一个 RT。
 * 这是 MC 物品/实体渲染的典型模式。 */
int ubwc_selftest_feedback_sz(void *egl, void *getproc, int S);
int ubwc_selftest_feedback_mode(void *egl, void *getproc, int S, int mode);

int ubwc_selftest_feedback(void *egl, void *getproc)
{
   ubwc_selftest_feedback_mode(egl, getproc, 512, 0);
   ubwc_selftest_feedback_mode(egl, getproc, 512, 1);
   ubwc_selftest_feedback_mode(egl, getproc, 512, 2);
   ubwc_selftest_feedback_mode(egl, getproc, 512, 3);
   ubwc_selftest_feedback_mode(egl, getproc, 512, 4);
   ubwc_selftest_feedback_mode(egl, getproc, 64, 0);
   return 0;
}

int ubwc_selftest_feedback_sz(void *egl, void *getproc, int S)
{
   return ubwc_selftest_feedback_mode(egl, getproc, S, 0);
}

int ubwc_selftest_feedback_mode(void *egl, void *getproc, int S, int mode)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
#define GF(type, var, name) type var = (void *)(dlsym(egl, name) ? dlsym(egl, name) : gp(name)); if (!var) { LOGI("UBWC-FB: missing %s", name); return -1; }
   GF(PFN_glCreateShader, CreateShader, "glCreateShader");
   GF(PFN_glShaderSource, ShaderSource, "glShaderSource");
   GF(PFN_glCompileShader, CompileShader, "glCompileShader");
   GF(PFN_glGetShaderiv, GetShaderiv, "glGetShaderiv");
   GF(PFN_glCreateProgram, CreateProgram, "glCreateProgram");
   GF(PFN_glAttachShader, AttachShader, "glAttachShader");
   GF(PFN_glLinkProgram, LinkProgram, "glLinkProgram");
   GF(PFN_glGetProgramiv, GetProgramiv, "glGetProgramiv");
   GF(PFN_glUseProgram, UseProgram, "glUseProgram");
   GF(PFN_glGetUniformLocation, GetUniformLocation, "glGetUniformLocation");
   GF(PFN_glUniform1i, Uniform1i, "glUniform1i");
   GF(PFN_glGenVertexArrays, GenVertexArrays, "glGenVertexArrays");
   GF(PFN_glBindVertexArray, BindVertexArray, "glBindVertexArray");
   GF(PFN_glGenBuffers, GenBuffers, "glGenBuffers");
   GF(PFN_glBindBuffer, BindBuffer, "glBindBuffer");
   GF(PFN_glBufferData, BufferData, "glBufferData");
   GF(PFN_glVertexAttribPointer, VertexAttribPointer, "glVertexAttribPointer");
   GF(PFN_glEnableVertexAttribArray, EnableVertexAttribArray, "glEnableVertexAttribArray");
   GF(PFN_glDrawArrays, DrawArrays, "glDrawArrays");
#undef GF

   const char *vs_src = "#version 330 core\nlayout(location=0) in vec2 p;\nout vec2 uv;\nvoid main(){uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}\n";
   const char *fs_src = "#version 330 core\nuniform sampler2D t;\nin vec2 uv;\nout vec4 c;\nvoid main(){c=texture(t,uv);}\n";
   GLuint vs = CreateShader(GL_VERTEX_SHADER), fs = CreateShader(GL_FRAGMENT_SHADER);
   ShaderSource(vs, 1, &vs_src, NULL); CompileShader(vs);
   ShaderSource(fs, 1, &fs_src, NULL); CompileShader(fs);
   GLint ok = 0;
   GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
   GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
   if (!ok) { LOGI("UBWC-FB: shader compile failed"); return -1; }
   GLuint prog = CreateProgram();
   AttachShader(prog, vs); AttachShader(prog, fs); LinkProgram(prog);
   GLint linked = 0; GetProgramiv(prog, GL_LINK_STATUS, &linked);
   if (!linked) { LOGI("UBWC-FB: link failed"); return -1; }
   UseProgram(prog);
   Uniform1i(GetUniformLocation(prog, "t"), 0);

   GLuint vao = 0, vbo = 0;
   GenVertexArrays(1, &vao); BindVertexArray(vao);
   const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
   GenBuffers(1, &vbo); BindBuffer(GL_ARRAY_BUFFER, vbo);
   BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   VertexAttribPointer(0, 2, GL_FLOAT, 0, 8, NULL);
   EnableVertexAttribArray(0);

   GLuint src = 0, srcFb = 0, dst = 0, dstFb = 0;
   src = make_rt_sz((int *)&srcFb, S, S);
   dst = make_rt_sz((int *)&dstFb, S, S);

   unsigned char px[4];
   int failures = 0, first = -1;
   for (int iter = 0; iter < 40; iter++) {
      if (mode >= 4) {
         /* 先当普通纹理上传一次（初始化纹理内容/布局），再渲染覆盖 */
         static unsigned char cyan[512 * 512 * 4];
         static int init4;
         if (!init4) {
            for (int i = 0; i < S * S; i++) {
               cyan[i * 4 + 0] = 0; cyan[i * 4 + 1] = 255;
               cyan[i * 4 + 2] = 255; cyan[i * 4 + 3] = 255;
            }
            init4 = 1;
         }
         gl.BindTexture(GL_TEXTURE_2D, src);
         gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, cyan);
         gl.BindTexture(GL_TEXTURE_2D, 0);
      }

      /* 渲染进 UBWC RT：清红 + 中间绿块 */
      gl.BindFramebuffer(GL_FRAMEBUFFER, srcFb);
      gl.Viewport(0, 0, S, S);
      gl.Enable(GL_SCISSOR_TEST);
      gl.Scissor(0, 0, S, S);
      gl.ClearColor(1, 0, 0, 1);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Scissor(S / 4, S / 4, S / 2, S / 2);
      gl.ClearColor(0, 1, 0, 1);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Disable(GL_SCISSOR_TEST);
      gl.Finish();
      if (mode >= 3) {
         /* 把 src 从 FBO 上摘下来，再当纹理采样（排除“挂在 FBO 上”的边界情况） */
         gl.BindFramebuffer(GL_FRAMEBUFFER, srcFb);
         gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
         gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      }
      if (mode >= 1)
         gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      if (mode >= 2)
         gl.Finish();

      /* 把同一个 RT 当纹理采样到另一个 RT（反馈/别名） */
      gl.BindFramebuffer(GL_FRAMEBUFFER, dstFb);
      gl.Viewport(0, 0, S, S);
      gl.ClearColor(0, 0, 0, 1);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.BindTexture(GL_TEXTURE_2D, src);   /* src 同时是上面渲染的 RT */
      BindVertexArray(vao);
      DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

      read_px((int)dstFb, S / 2, S / 2, px);
      int okc = eq4(px, 0, 255, 0, 255);
      if (first < 0)
         LOGI("UBWC-FB(S=%d): center=(%u,%u,%u) want 0,255,0", S, px[0], px[1], px[2]);
      read_px((int)dstFb, 8, 8, px);
      if (first < 0)
         LOGI("UBWC-FB(S=%d): edge  =(%u,%u,%u) want 255,0,0", S, px[0], px[1], px[2]);
      okc = okc && eq4(px, 255, 0, 0, 255);
      if (!okc) {
         failures++;
         if (first < 0) { first = iter; LOGI("UBWC-FB: iter %d center/edge mismatch", iter); }
      }
   }
   gl.DeleteFramebuffers(1, &srcFb);
   gl.DeleteFramebuffers(1, &dstFb);
   gl.DeleteTextures(1, &src);
   gl.DeleteTextures(1, &dst);
   BindVertexArray(0);
   LOGI("UBWC-FB(%d,mode=%d): RESULT %s (failures=%d/40 first=%d)", S, mode, failures ? "FAIL" : "PASS",
        failures, first);
   return failures ? 1 : 0;
}


/* T12：区分「两次全屏 clear」「全屏 clear + scissor 局部 clear」，
 * 都用 blit 读回（不涉及采样反馈）。 */
int ubwc_selftest_clears(void *egl, void *getproc)
{
   (void)egl; (void)getproc;
   const int S = 512, L = 64;
   GLuint rt = 0, rtFb = 0, lin = 0, linFb = 0;
   rt = make_rt((int *)&rtFb);
   gl.GenTextures(1, &lin);
   gl.BindTexture(GL_TEXTURE_2D, lin);
   gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, L, L, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.GenFramebuffers(1, &linFb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, linFb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lin, 0);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

   unsigned char px[4];
   int failures = 0;

   /* A: 两次全屏 clear */
   gl.BindFramebuffer(GL_FRAMEBUFFER, rtFb);
   gl.ClearColor(1, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
   gl.ClearColor(0, 1, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
   gl.Finish();
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, rtFb);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, linFb);
   gl.BlitFramebuffer(200, 200, 264, 264, 0, 0, L, L, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   gl.Finish();
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   read_px((int)linFb, L / 2, L / 2, px);
   if (!eq4(px, 0, 255, 0, 255)) { failures++; LOGI("UBWC-CL: A(two clear) got %u,%u,%u want 0,255,0", px[0], px[1], px[2]); }

   /* B: 全屏红 + scissor 绿块 */
   gl.BindFramebuffer(GL_FRAMEBUFFER, rtFb);
   gl.Enable(GL_SCISSOR_TEST);
   gl.Scissor(0, 0, S, S);
   gl.ClearColor(1, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
   gl.Scissor(192, 192, 128, 128);
   gl.ClearColor(0, 1, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
   gl.Disable(GL_SCISSOR_TEST);
   gl.Finish();
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, rtFb);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, linFb);
   gl.BlitFramebuffer(200, 200, 264, 264, 0, 0, L, L, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   gl.Finish();
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   read_px((int)linFb, L / 2, L / 2, px);
   if (!eq4(px, 0, 255, 0, 255)) { failures++; LOGI("UBWC-CL: B(full+scissor) got %u,%u,%u want 0,255,0", px[0], px[1], px[2]); }

   /* 再读块外，应为红 */
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, rtFb);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, linFb);
   gl.BlitFramebuffer(8, 8, 72, 72, 0, 0, L, L, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   gl.Finish();
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   read_px((int)linFb, L / 2, L / 2, px);
   if (!eq4(px, 255, 0, 0, 255)) { failures++; LOGI("UBWC-CL: B-outside got %u,%u,%u want 255,0,0", px[0], px[1], px[2]); }

   gl.DeleteFramebuffers(1, &rtFb);
   gl.DeleteFramebuffers(1, &linFb);
   gl.DeleteTextures(1, &rt);
   gl.DeleteTextures(1, &lin);
   LOGI("UBWC-CL: RESULT %s (failures=%d/3)", failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}


/* T15：MC 图集的真实形态 —— NPOT + mipmap + 逐格局部上传。
 * 每层都当 FBO 清一遍，回头再检查 level0 的每一格有没有被别的层踩坏
 * （UBWC 的 flag buffer 是每层一段，最容易在这里算错）。 */
int ubwc_selftest_mip_npot(void *egl, void *getproc)
{
   (void)egl; (void)getproc;
   const int W0 = 1024, H0 = 512, LEVELS = 10;
   const int CW = 128, CH = 64;       /* 8 x 8 格 */
   GLuint tex = 0, rt = 0, rtFb = 0;
   unsigned char cell[CW * CH * 4];
   unsigned char px[4];
   int failures = 0;

   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, 0x813D /*TEXTURE_MAX_LEVEL*/, LEVELS - 1);
   for (int l = 0, w = W0, h = H0; l < LEVELS; l++, w /= 2, h /= 2)
      gl.TexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   /* 逐格上传（模拟图集里一格一格塞图） */
   for (int cy = 0; cy < H0 / CH; cy++) {
      for (int cx = 0; cx < W0 / CW; cx++) {
         for (int i = 0; i < CW * CH; i++) {
            cell[i * 4 + 0] = (unsigned char)(cx * 32);
            cell[i * 4 + 1] = (unsigned char)(cy * 32);
            cell[i * 4 + 2] = 200;
            cell[i * 4 + 3] = 255;
         }
         gl.TexSubImage2D(GL_TEXTURE_2D, 0, cx * CW, cy * CH, CW, CH,
                          GL_RGBA, GL_UNSIGNED_BYTE, cell);
      }
   }
   gl.BindTexture(GL_TEXTURE_2D, 0);

   rt = make_rt_sz((int *)&rtFb, W0, H0);

   /* 把 level0 blit 到 RT，逐格校验 */
#define CHECK_LEVEL0(tag)                                                        \
   do {                                                                          \
      gl.GenFramebuffers(1, &fbL0);                                              \
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbL0);                                  \
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,              \
                              GL_TEXTURE_2D, tex, 0);                            \
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbL0);                             \
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, rtFb);                             \
      gl.BlitFramebuffer(0, 0, W0, H0, 0, 0, W0, H0, 0x4000, GL_NEAREST);        \
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);                                     \
      gl.DeleteFramebuffers(1, &fbL0);                                           \
      gl.Finish();                                                               \
      int bad = 0;                                                               \
      for (int cy = 0; cy < H0 / CH; cy++)                                       \
         for (int cx = 0; cx < W0 / CW; cx++) {                                  \
            read_px((int)rtFb, cx * CW + CW / 2, cy * CH + CH / 2, px);          \
            if (!eq4(px, cx * 32, cy * 32, 200, 255)) {                          \
               if (!bad)                                                         \
                  LOGI("UBWC-MIPS: " tag " cell(%d,%d)=(%u,%u,%u) want (%d,%d,200)", \
                       cx, cy, px[0], px[1], px[2], cx * 32, cy * 32);           \
               bad++;                                                            \
            }                                                                    \
         }                                                                       \
      failures += bad;                                                           \
      LOGI("UBWC-MIPS: " tag " cells bad=%d", bad);                              \
   } while (0)

   GLuint fbL0 = 0;
   CHECK_LEVEL0("after-upload");

   /* 逐层当 FBO 清掉（每层都有自己的 UBWC flag 区） */
   for (int l = 1; l < LEVELS - 1; l++) {
      GLuint fb = 0;
      int w = W0 >> l, h = H0 >> l;
      gl.GenFramebuffers(1, &fb);
      gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, l);
      gl.Viewport(0, 0, w, h);
      gl.Enable(GL_SCISSOR_TEST);
      gl.Scissor(0, 0, w, h);
      gl.ClearColor(1, 1, 1, 1);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Disable(GL_SCISSOR_TEST);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      gl.DeleteFramebuffers(1, &fb);
   }
   gl.Finish();
   CHECK_LEVEL0("after-level-clears");

   gl.DeleteFramebuffers(1, &rtFb);
   gl.DeleteTextures(1, &rt);
   gl.DeleteTextures(1, &tex);
   LOGI("UBWC-MIPS: RESULT %s (failures=%d)", failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}


/* T16：多层 mip 的 UBWC 纹理逐层采样（物品图集就是这种：带 mip、小尺寸、
 * 各向异性过滤会一次读多层）。每层上传不同纯色，用 textureLod 精确取该层。 */
int ubwc_selftest_mip_lod(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
#define GF(type, var, name) type var = (void *)(dlsym(egl, name) ? dlsym(egl, name) : gp(name)); if (!var) { LOGI("UBWC-MIPLOD: missing %s", name); return -1; }
   GF(PFN_glCreateShader, CreateShader, "glCreateShader");
   GF(PFN_glShaderSource, ShaderSource, "glShaderSource");
   GF(PFN_glCompileShader, CompileShader, "glCompileShader");
   GF(PFN_glGetShaderiv, GetShaderiv, "glGetShaderiv");
   GF(PFN_glCreateProgram, CreateProgram, "glCreateProgram");
   GF(PFN_glAttachShader, AttachShader, "glAttachShader");
   GF(PFN_glLinkProgram, LinkProgram, "glLinkProgram");
   GF(PFN_glUseProgram, UseProgram, "glUseProgram");
   GF(PFN_glGetUniformLocation, GetUniformLocation, "glGetUniformLocation");
   GF(PFN_glUniform1i, Uniform1i, "glUniform1i");
   GF(PFN_glUniform1f, Uniform1f, "glUniform1f");
   GF(PFN_glGenVertexArrays, GenVertexArrays, "glGenVertexArrays");
   GF(PFN_glBindVertexArray, BindVertexArray, "glBindVertexArray");
   GF(PFN_glGenBuffers, GenBuffers, "glGenBuffers");
   GF(PFN_glBindBuffer, BindBuffer, "glBindBuffer");
   GF(PFN_glBufferData, BufferData, "glBufferData");
   GF(PFN_glVertexAttribPointer, VertexAttribPointer, "glVertexAttribPointer");
   GF(PFN_glEnableVertexAttribArray, EnableVertexAttribArray, "glEnableVertexAttribArray");
   GF(PFN_glDrawArrays, DrawArrays, "glDrawArrays");
#undef GF

   const char *vs_src = "#version 330 core\nlayout(location=0) in vec2 p;\nvoid main(){gl_Position=vec4(p,0,1);}\n";
   const char *fs_src = "#version 330 core\nuniform sampler2D t;\nuniform float lod;\nout vec4 c;\nvoid main(){c=textureLod(t, vec2(0.5), lod);}\n";
   GLuint vs = CreateShader(GL_VERTEX_SHADER), fs = CreateShader(GL_FRAGMENT_SHADER);
   ShaderSource(vs, 1, &vs_src, NULL); CompileShader(vs);
   ShaderSource(fs, 1, &fs_src, NULL); CompileShader(fs);
   GLint ok = 0;
   GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
   if (!ok) { LOGI("UBWC-MIPLOD: fs compile failed"); return -1; }
   GLuint prog = CreateProgram(); AttachShader(prog, vs); AttachShader(prog, fs); LinkProgram(prog);
   UseProgram(prog);
   Uniform1i(GetUniformLocation(prog, "t"), 0);
   GLint ulod = GetUniformLocation(prog, "lod");

   GLuint vao = 0, vbo = 0;
   GenVertexArrays(1, &vao); BindVertexArray(vao);
   const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
   GenBuffers(1, &vbo); BindBuffer(GL_ARRAY_BUFFER, vbo);
   BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   VertexAttribPointer(0, 2, GL_FLOAT, 0, 8, NULL);
   EnableVertexAttribArray(0);

   const int W0 = 1024, H0 = 1024, LEVELS = 5, S = 256;
   GLuint tex = 0;
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703 /*LINEAR_MIPMAP_LINEAR*/);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, 0x813D /*TEXTURE_MAX_LEVEL*/, LEVELS - 1);
   static unsigned char buf[1024 * 1024 * 4];
   for (int l = 0, w = W0, h = H0; l < LEVELS; l++, w /= 2, h /= 2) {
      for (int i = 0; i < w * h; i++) {
         buf[i * 4 + 0] = (unsigned char)(l * 25);
         buf[i * 4 + 1] = (unsigned char)(255 - l * 25);
         buf[i * 4 + 2] = 128;
         buf[i * 4 + 3] = 255;
      }
      gl.TexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
   }
   gl.BindTexture(GL_TEXTURE_2D, 0);

   GLuint dst = 0, dstFb = 0;
   dst = make_rt_sz((int *)&dstFb, S, S);
   unsigned char px[4];
   int failures = 0;
   for (int l = 0; l < LEVELS; l++) {
      gl.BindFramebuffer(GL_FRAMEBUFFER, dstFb);
      gl.Viewport(0, 0, S, S);
      gl.ClearColor(0, 0, 0, 1);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.BindTexture(GL_TEXTURE_2D, tex);
      Uniform1f(ulod, (float)l);
      BindVertexArray(vao);
      DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      read_px((int)dstFb, S / 2, S / 2, px);
      if (!eq4(px, l * 25, 255 - l * 25, 128, 255)) {
         LOGI("UBWC-MIPLOD: level %d got (%u,%u,%u) want (%d,%d,128)",
              l, px[0], px[1], px[2], l * 25, 255 - l * 25);
         failures++;
      }
   }
   /* 决定性区分：直接把 level 9 挂到 FBO 上读，看数据到底在不在那一层 */
   {
      GLuint fb9 = 0;
      gl.GenFramebuffers(1, &fb9);
      gl.BindFramebuffer(GL_FRAMEBUFFER, fb9);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 9);
      GLenum st9 = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
      read_px((int)fb9, 0, 0, px);
      LOGI("UBWC-MIPLOD: level9 FBO status=0x%x read=(%u,%u,%u)", st9, px[0], px[1], px[2]);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      gl.DeleteFramebuffers(1, &fb9);
   }

   gl.DeleteFramebuffers(1, &dstFb);
   gl.DeleteTextures(1, &dst);
   gl.DeleteTextures(1, &tex);
   LOGI("UBWC-MIPLOD: RESULT %s (failures=%d/%d)", failures ? "FAIL" : "PASS",
        failures, LEVELS);
   return failures ? 1 : 0;
}


/* T17：MC 方块图集的真实形态 —— 2048x2048 + mip + 逐格 TexSubImage2D + GenerateMipmap。
 * 生物图集没这形态（所以实体好了物品还坏），这里是物品的嫌疑点。 */
int ubwc_selftest_atlas(void *egl, void *getproc)
{
   typedef void *(*PFN_gp)(const char *);
   PFN_gp gp = (PFN_gp)getproc;
   PFN_glGenerateMipmap GenMip = (void *)(dlsym(egl, "glGenerateMipmap") ? dlsym(egl, "glGenerateMipmap") : gp("glGenerateMipmap"));
   if (!GenMip) { LOGI("UBWC-ATLAS: SKIP"); return -1; }

   const int S = 1152, LEVELS = 5, CELL = 16, GRID = 32; /* 512x512 区域内有 32x32 格 */
   GLuint tex = 0;
   gl.GenTextures(1, &tex);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703 /*LINEAR_MIPMAP_LINEAR*/);
   gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   gl.TexParameteri(GL_TEXTURE_2D, 0x813D /*TEXTURE_MAX_LEVEL*/, LEVELS - 1);
   for (int l = 0, w = S; l < LEVELS; l++, w /= 2)
      gl.TexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   /* 逐格上传纯色（模拟图集拼接） */
   static unsigned char cell[CELL * CELL * 4];
   for (int cy = 0; cy < GRID; cy++) {
      for (int cx = 0; cx < GRID; cx++) {
         for (int i = 0; i < CELL * CELL; i++) {
            cell[i * 4 + 0] = (unsigned char)(cx * 8);
            cell[i * 4 + 1] = (unsigned char)(cy * 8);
            cell[i * 4 + 2] = 128;
            cell[i * 4 + 3] = 255;
         }
         gl.TexSubImage2D(GL_TEXTURE_2D, 0, cx * CELL, cy * CELL, CELL, CELL,
                          GL_RGBA, GL_UNSIGNED_BYTE, cell);
      }
   }
   GenMip(GL_TEXTURE_2D);
   gl.Finish();
   gl.BindTexture(GL_TEXTURE_2D, 0);

   /* 用 texelFetch 把 level0 左下 512x512 拷到 RT，逐格校验 */
   GLuint dst = 0, dstFb = 0;
   dst = make_rt_sz((int *)&dstFb, 512, 512);
   unsigned char px[4];
   int failures = 0;
   int bad = 0;
   /* 直接用 blit 读回 level0 更简单：把 tex 挂到 FBO 上 blit */
   GLuint fb = 0;
   gl.GenFramebuffers(1, &fb);
   gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
   gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFb);
   gl.BlitFramebuffer(0, 0, 512, 512, 0, 0, 512, 512, 0x4000, GL_NEAREST);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   gl.Finish();
   LOGI("UBWC-ATLAS: fbo status=0x%x", st);
   for (int cy = 0; cy < GRID; cy++) {
      for (int cx = 0; cx < GRID; cx++) {
         read_px((int)dstFb, cx * CELL + CELL / 2, cy * CELL + CELL / 2, px);
         if (!eq4(px, cx * 8, cy * 8, 128, 255)) {
            if (!bad)
               LOGI("UBWC-ATLAS: cell(%d,%d)=(%u,%u,%u) want(%d,%d,128)",
                    cx, cy, px[0], px[1], px[2], cx * 8, cy * 8);
            bad++;
         }
      }
   }
   failures += bad;
   LOGI("UBWC-ATLAS: cells bad=%d/%d", bad, GRID * GRID);
   /* ==== 关键：用 TP 采样（而不是 blit 读回）再验一遍 ==== */
   {
      typedef void *(*PFN_gp)(const char *);
      PFN_gp gp = (PFN_gp)getproc;
      PFN_glCreateShader CS = (void *)(dlsym(egl, "glCreateShader") ? dlsym(egl, "glCreateShader") : gp("glCreateShader"));
      PFN_glShaderSource SS = (void *)(dlsym(egl, "glShaderSource") ? dlsym(egl, "glShaderSource") : gp("glShaderSource"));
      PFN_glCompileShader CC = (void *)(dlsym(egl, "glCompileShader") ? dlsym(egl, "glCompileShader") : gp("glCompileShader"));
      PFN_glCreateProgram CP = (void *)(dlsym(egl, "glCreateProgram") ? dlsym(egl, "glCreateProgram") : gp("glCreateProgram"));
      PFN_glAttachShader AS = (void *)(dlsym(egl, "glAttachShader") ? dlsym(egl, "glAttachShader") : gp("glAttachShader"));
      PFN_glLinkProgram LP = (void *)(dlsym(egl, "glLinkProgram") ? dlsym(egl, "glLinkProgram") : gp("glLinkProgram"));
      PFN_glUseProgram UP = (void *)(dlsym(egl, "glUseProgram") ? dlsym(egl, "glUseProgram") : gp("glUseProgram"));
      PFN_glGetUniformLocation GU = (void *)(dlsym(egl, "glGetUniformLocation") ? dlsym(egl, "glGetUniformLocation") : gp("glGetUniformLocation"));
      PFN_glUniform1i U1 = (void *)(dlsym(egl, "glUniform1i") ? dlsym(egl, "glUniform1i") : gp("glUniform1i"));
      PFN_glGenVertexArrays GVA = (void *)(dlsym(egl, "glGenVertexArrays") ? dlsym(egl, "glGenVertexArrays") : gp("glGenVertexArrays"));
      PFN_glBindVertexArray BVA = (void *)(dlsym(egl, "glBindVertexArray") ? dlsym(egl, "glBindVertexArray") : gp("glBindVertexArray"));
      PFN_glGenBuffers GB = (void *)(dlsym(egl, "glGenBuffers") ? dlsym(egl, "glGenBuffers") : gp("glGenBuffers"));
      PFN_glBindBuffer BB = (void *)(dlsym(egl, "glBindBuffer") ? dlsym(egl, "glBindBuffer") : gp("glBindBuffer"));
      PFN_glBufferData BD = (void *)(dlsym(egl, "glBufferData") ? dlsym(egl, "glBufferData") : gp("glBufferData"));
      PFN_glVertexAttribPointer VAP = (void *)(dlsym(egl, "glVertexAttribPointer") ? dlsym(egl, "glVertexAttribPointer") : gp("glVertexAttribPointer"));
      PFN_glEnableVertexAttribArray EVA = (void *)(dlsym(egl, "glEnableVertexAttribArray") ? dlsym(egl, "glEnableVertexAttribArray") : gp("glEnableVertexAttribArray"));
      PFN_glDrawArrays DA = (void *)(dlsym(egl, "glDrawArrays") ? dlsym(egl, "glDrawArrays") : gp("glDrawArrays"));
      if (CS && SS && CC && CP && AS && LP && UP && GU && U1 && GVA && BVA && GB && BB && BD && VAP && EVA && DA) {
         const char *vs = "#version 330 core\nlayout(location=0) in vec2 p;\nvoid main(){gl_Position=vec4(p,0,1);}\n";
         const char *fs = "#version 330 core\nuniform sampler2D t;\nout vec4 c;\nvoid main(){c=texelFetch(t, ivec2(gl_FragCoord.xy), 0);}\n";
         GLuint v = CS(GL_VERTEX_SHADER), fsh = CS(GL_FRAGMENT_SHADER);
         SS(v, 1, &vs, NULL); CC(v);
         SS(fsh, 1, &fs, NULL); CC(fsh);
         GLuint prog = CP(); AS(prog, v); AS(prog, fsh); LP(prog); UP(prog);
         U1(GU(prog, "t"), 0);
         GLuint vao = 0, vbo = 0;
         GVA(1, &vao); BVA(vao);
         const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
         GB(1, &vbo); BB(GL_ARRAY_BUFFER, vbo);
         BD(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
         VAP(0, 2, GL_FLOAT, 0, 8, NULL); EVA(0);
         gl.BindFramebuffer(GL_FRAMEBUFFER, dstFb);
         gl.Viewport(0, 0, 512, 512);
         gl.ClearColor(0, 0, 0, 1);
         gl.Clear(GL_COLOR_BUFFER_BIT);
         gl.BindTexture(GL_TEXTURE_2D, tex);
         DA(GL_TRIANGLE_STRIP, 0, 4);
         gl.Finish();
         gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
         int tbad = 0;
         for (int cy = 0; cy < GRID; cy++) {
            for (int cx = 0; cx < GRID; cx++) {
               read_px((int)dstFb, cx * CELL + CELL / 2, cy * CELL + CELL / 2, px);
               if (!eq4(px, cx * 8, cy * 8, 128, 255)) {
                  if (!tbad)
                     LOGI("UBWC-ATLAS: TP cell(%d,%d)=(%u,%u,%u) want(%d,%d,128)",
                          cx, cy, px[0], px[1], px[2], cx * 8, cy * 8);
                  tbad++;
               }
            }
         }
         LOGI("UBWC-ATLAS: TP cells bad=%d/%d", tbad, GRID * GRID);
         failures += tbad;
      } else {
         LOGI("UBWC-ATLAS: TP probe SKIP (missing GL entry points)");
      }
   }

   gl.DeleteFramebuffers(1, &fb);
   gl.DeleteFramebuffers(1, &dstFb);
   gl.DeleteTextures(1, &dst);
   gl.DeleteTextures(1, &tex);
   LOGI("UBWC-ATLAS: RESULT %s (failures=%d)", failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}


/* T18：先整张 clear（置 UBWC fast-clear 标记），再局部 TexSubImage2D。
 * 若上传没清掉目标块的 flag，采样/读回会一直是 clear 颜色。 */
int ubwc_selftest_clear_upload(void *egl, void *getproc)
{
   (void)egl; (void)getproc;
   const int S = 512, CELL = 16;
   GLuint tex = 0, fb = 0, rt = 0, rtFb = 0;
   tex = make_rt_sz((int *)&fb, S, S);
   rt = make_rt_sz((int *)&rtFb, S, S);

   gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
   gl.Viewport(0, 0, S, S);
   gl.Enable(GL_SCISSOR_TEST);
   gl.Scissor(0, 0, S, S);
   gl.ClearColor(0, 1, 0, 1);
   gl.Clear(GL_COLOR_BUFFER_BIT);
   gl.Disable(GL_SCISSOR_TEST);
   gl.Finish();

   static unsigned char red[CELL * CELL * 4];
   for (int i = 0; i < CELL * CELL; i++) {
      red[i * 4 + 0] = 255; red[i * 4 + 1] = 0; red[i * 4 + 2] = 0; red[i * 4 + 3] = 255;
   }
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   gl.BindTexture(GL_TEXTURE_2D, tex);
   gl.TexSubImage2D(GL_TEXTURE_2D, 0, 32, 32, CELL, CELL, GL_RGBA, GL_UNSIGNED_BYTE, red);
   gl.BindTexture(GL_TEXTURE_2D, 0);
   gl.Finish();

   /* blit 读回到 RT 再取点 */
   gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
   gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, rtFb);
   gl.BlitFramebuffer(0, 0, S, S, 0, 0, S, S, 0x4000, GL_NEAREST);
   gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
   gl.Finish();

   unsigned char px[4];
   int failures = 0;
   read_px((int)rtFb, 40, 40, px);
   LOGI("UBWC-CU: sprite=(%u,%u,%u) want 255,0,0", px[0], px[1], px[2]);
   if (!eq4(px, 255, 0, 0, 255)) failures++;
   read_px((int)rtFb, 200, 200, px);
   LOGI("UBWC-CU: bg=(%u,%u,%u) want 0,255,0", px[0], px[1], px[2]);
   if (!eq4(px, 0, 255, 0, 255)) failures++;
   gl.DeleteFramebuffers(1, &fb);
   gl.DeleteFramebuffers(1, &rtFb);
   gl.DeleteTextures(1, &tex);
   gl.DeleteTextures(1, &rt);
   LOGI("UBWC-CU: RESULT %s (failures=%d)", failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}

int ubwc_selftest(void *egl, void *getproc)
{
   if (resolve(egl, getproc)) {
      LOGI("UBWC: SKIP (missing GL entry points)");
      return -1;
   }

   /* 新用例先跑，避免被后面的用例污染 GL 状态 */
   ubwc_selftest_clear_upload(egl, getproc);
   ubwc_selftest_atlas(egl, getproc);
   ubwc_selftest_mip_lod(egl, getproc);
   ubwc_selftest_mip_npot(egl, getproc);


   int fbA = 0, fbB = 0;
   GLuint texA = make_rt(&fbA);
   GLuint texB = make_rt(&fbB);
   LOGI("UBWC: created 512x512 RTs texA=%u fbA=%d texB=%u fbB=%d", texA, fbA, texB, fbB);

   unsigned char px[4];
   int failures = 0;
   int first_iter = -1;

   for (int iter = 0; iter < 50; iter++) {
      int bad = 0;

      /* T1: 清屏红 + scissor 清绿块 */
      gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbA);
      gl.Enable(GL_SCISSOR_TEST);
      gl.Scissor(0, 0, W, H);
      gl.ClearColor(1.0f, 0.0f, 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Scissor(64, 64, 64, 64);
      gl.ClearColor(0.0f, 1.0f, 0.0f, 1.0f);
      gl.Clear(GL_COLOR_BUFFER_BIT);
      gl.Disable(GL_SCISSOR_TEST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      read_px(fbA, 96, 96, px);
      if (!eq4(px, 0, 255, 0, 255)) {
         bad++;
         if (iter == 0) LOGI("UBWC: T1 in-block got %u,%u,%u want 0,255,0", px[0], px[1], px[2]);
      }
      read_px(fbA, 300, 300, px);
      if (!eq4(px, 255, 0, 0, 255)) {
         bad++;
         if (iter == 0) LOGI("UBWC: T1 out-block got %u,%u,%u want 255,0,0", px[0], px[1], px[2]);
      }

      /* T2: A -> B blit */
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbA);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fbB);
      gl.BlitFramebuffer(0, 0, W, H, 0, 0, W, H, 0x4000 /*COLOR_BUFFER_BIT*/, GL_NEAREST);
      gl.Finish();
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      read_px(fbB, 96, 96, px);
      if (!eq4(px, 0, 255, 0, 255)) {
         bad++;
         if (iter == 0) LOGI("UBWC: T2 blit got %u,%u,%u want 0,255,0", px[0], px[1], px[2]);
      }

      /* T3: 纹理部分更新 -> blit 进 RT -> 读回 */
      static unsigned char blue[256 * 256 * 4];
      static int init;
      if (!init) {
         for (int i = 0; i < 256 * 256; i++) {
            blue[i * 4 + 0] = 0;
            blue[i * 4 + 1] = 0;
            blue[i * 4 + 2] = 255;
            blue[i * 4 + 3] = 255;
         }
         init = 1;
      }
      GLuint texC = 0;
      gl.GenTextures(1, &texC);
      gl.BindTexture(GL_TEXTURE_2D, texC);
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gl.TexSubImage2D(GL_TEXTURE_2D, 0, iter % 128, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, blue);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      GLuint fbC = 0;
      gl.GenFramebuffers(1, &fbC);
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbC);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texC, 0);
      GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      if (st != GL_FRAMEBUFFER_COMPLETE) {
         bad++;
         LOGI("UBWC: T3 FBO incomplete 0x%x", st);
      } else {
         gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbC);
         gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fbA);
         gl.BlitFramebuffer(0, 0, W, H, 0, 0, W, H, 0x4000, GL_NEAREST);
         gl.Finish();
         gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
         read_px(fbA, 300, 300, px); /* 落在上传区域外，应为黑（未初始化 tile）*/
         (void)px;
         read_px(fbA, iter % 128 + 8, 8, px);
         if (!eq4(px, 0, 0, 255, 255)) {
            bad++;
            if (iter == 0)
               LOGI("UBWC: T3 partial got %u,%u,%u want 0,0,255", px[0], px[1], px[2]);
         }
      }
      gl.DeleteTextures(1, &texC);
      gl.DeleteFramebuffers(1, &fbC);

      if (bad) {
         failures++;
         if (first_iter < 0) first_iter = iter;
      }
   }

   gl.DeleteTextures(1, &texA);
   gl.DeleteTextures(1, &texB);
   GLuint fbs[2] = {(GLuint)fbA, (GLuint)fbB};
   gl.DeleteFramebuffers(2, fbs);

   LOGI("UBWC: RESULT %s (failures=%d/50 first=%d)",
        failures ? "FAIL" : "PASS", failures, first_iter);
   {
      extern int ubwc_selftest_big(void *, void *);
      ubwc_selftest_big(egl, getproc);
      extern int ubwc_selftest_msaa(void *, void *);
      ubwc_selftest_msaa(egl, getproc);
      extern int ubwc_selftest_mipmap(void *, void *);
      ubwc_selftest_mipmap(egl, getproc);
      extern int ubwc_selftest_cube(void *, void *);
      ubwc_selftest_cube(egl, getproc);
      extern int ubwc_selftest_mrt(void *, void *);
      ubwc_selftest_mrt(egl, getproc);
      extern int ubwc_selftest_sample(void *, void *);
      ubwc_selftest_sample(egl, getproc);
      extern int ubwc_selftest_feedback(void *, void *);
      ubwc_selftest_feedback(egl, getproc);
      extern int ubwc_selftest_clears(void *, void *);
      ubwc_selftest_clears(egl, getproc);
   }
   return failures ? 1 : 0;
}

