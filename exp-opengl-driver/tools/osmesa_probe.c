/* osmesa_probe.c — 设备端直接验证 libOSMesa_8.so (freedreno/KGSL) 是否能渲染。
 *
 * 目的：把"驱动能不能画"和"FCL 桥接有没有把画面送显"分开。
 * 每一步都会 fflush，崩溃时也能看到最后一步。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <GLES3/gl3.h>

typedef void *OSMesaContext;
extern OSMesaContext OSMesaCreateContext(GLuint format, OSMesaContext sharelist);
extern GLboolean OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, GLenum type,
                                   GLsizei width, GLsizei height);
extern void OSMesaDestroyContext(OSMesaContext ctx);
extern void OSMesaPixelStore(GLint pname, GLint value);
#define OSMESA_ROW_LENGTH 0x10
#define OSMESA_Y_UP       0x11

#define W 256
#define H 256

static GLuint compile_shader(GLenum type, const char *src, const char *what);

static double now_ms(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* 粗略模拟游戏的每帧开销：
 *   - draws 个 draw call（测 CPU 提交开销）
 *   - glFinish（测同步）
 *   - readback=1 时把整帧读回用户内存（模拟 OSMesa flush_front 的拷贝）
 */
static int bench_mode(int w, int h, int frames, int draws, int readback) {
   printf("[bench] %dx%d frames=%d draws/frame=%d readback=%d\n", w, h, frames, draws, readback);
   fflush(stdout);

   unsigned char *buf = calloc((size_t)w * h, 4);
   OSMesaContext ctx = OSMesaCreateContext(GL_RGBA, NULL);
   if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, w, h)) {
      printf("[bench] context failed\n");
      return 1;
   }
   OSMesaPixelStore(OSMESA_ROW_LENGTH, w);
   OSMesaPixelStore(OSMESA_Y_UP, 0);
   glViewport(0, 0, w, h);

   const char *vs =
      "#version 150\n"
      "uniform vec2 off;\n"
      "void main() {\n"
      "   vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0,\n"
      "                 gl_VertexID == 2 ? 3.0 : -1.0);\n"
      "   gl_Position = vec4(p * 0.02 + off, 0.0, 1.0);\n"
      "}\n";
   const char *fs =
      "#version 150\n"
      "uniform vec2 off;\n"
      "out vec4 c;\n"
      "void main() { c = vec4(fract(off.x + 0.5), fract(off.y + 0.5), 0.5, 1.0); }\n";

   GLuint vsh = compile_shader(GL_VERTEX_SHADER, vs, "bench-vs");
   GLuint fsh = vsh ? compile_shader(GL_FRAGMENT_SHADER, fs, "bench-fs") : 0;
   if (!vsh || !fsh) return 2;

   GLuint prog = glCreateProgram();
   glAttachShader(prog, vsh);
   glAttachShader(prog, fsh);
   glLinkProgram(prog);
   GLint ok = 0;
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   if (!ok) { printf("[bench] link failed\n"); return 3; }
   glUseProgram(prog);
   GLint loc = glGetUniformLocation(prog, "off");

   /* 预热 */
   for (int i = 0; i < 3; i++) { glClear(GL_COLOR_BUFFER_BIT); glFinish(); }

   double t0 = now_ms();
   for (int f = 0; f < frames; f++) {
      glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      for (int i = 0; i < draws; i++) {
         float x = (float)((i * 37) % 100) / 50.0f - 1.0f;
         float y = (float)((i * 53) % 100) / 50.0f - 1.0f;
         glUniform2f(loc, x, y);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      glFinish();
      if (readback)
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
   }
   double t1 = now_ms();
   double per = (t1 - t0) / frames;
   printf("[bench] %.2f ms/frame  = %.1f fps\n", per, frames * 1000.0 / (t1 - t0));
   fflush(stdout);
   OSMesaDestroyContext(ctx);
   free(buf);
   return 0;
}

static void step(const char *s) { printf("[probe] %s\n", s); fflush(stdout); }

static GLuint compile_shader(GLenum type, const char *src, const char *what) {
   GLuint sh = glCreateShader(type);
   glShaderSource(sh, 1, &src, NULL);
   glCompileShader(sh);
   GLint ok = 0;
   glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
   GLint len = 0;
   glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
   if (len > 1) {
      char *log = malloc(len);
      glGetShaderInfoLog(sh, len, NULL, log);
      printf("[probe] %s log: %s\n", what, log);
      free(log);
   }
   if (!ok) {
      printf("[probe] %s COMPILE FAILED\n", what);
      return 0;
   }
   printf("[probe] %s compiled\n", what);
   return sh;
}

static int env_int(const char *name, int def) {
   const char *v = getenv(name);
   return v ? atoi(v) : def;
}

int main(void) {
   if (getenv("PROBE_BENCH"))
      return bench_mode(env_int("PROBE_W", 2880), env_int("PROBE_H", 1800),
                        env_int("PROBE_FRAMES", 30), env_int("PROBE_DRAWS", 1000),
                        env_int("PROBE_READBACK", 1));

   unsigned char *buf = calloc(W * H, 4);
   if (!buf) return 1;

   const char *gd = getenv("GALLIUM_DRIVER");
   printf("[probe] start (GALLIUM_DRIVER=%s)\n", gd ? gd : "(unset)");
   fflush(stdout);

   step("OSMesaCreateContext(GL_RGBA)");
   OSMesaContext ctx = OSMesaCreateContext(GL_RGBA, NULL);
   if (!ctx) { printf("[probe] FAIL: no context\n"); return 2; }

   step("OSMesaMakeCurrent(256x256 RGBA8)");
   if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
      printf("[probe] FAIL: MakeCurrent returned false\n");
      return 3;
   }
   OSMesaPixelStore(OSMESA_ROW_LENGTH, W);
   OSMesaPixelStore(OSMESA_Y_UP, 0);

   const char *ver = (const char *)glGetString(GL_VERSION);
   const char *ren = (const char *)glGetString(GL_RENDERER);
   const char *ven = (const char *)glGetString(GL_VENDOR);
   const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
   printf("[probe] GL_VERSION  = %s\n", ver ? ver : "(null)");
   printf("[probe] GL_RENDERER = %s\n", ren ? ren : "(null)");
   printf("[probe] GL_VENDOR   = %s\n", ven ? ven : "(null)");
   printf("[probe] GLSL        = %s\n", glsl ? glsl : "(null)");
   fflush(stdout);

   step("glClear(green) + glReadPixels");
   glViewport(0, 0, W, H);
   glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glFinish();
   unsigned char px[4] = {0};
   glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   printf("[probe] clear pixel = %u,%u,%u,%u (expect 0,255,0,255)\n", px[0], px[1], px[2], px[3]);
   int clear_ok = (px[1] > 200 && px[0] < 40 && px[2] < 40);

   step("shader compile/link/draw (ir3)");
   /* 全屏三角形，不依赖 gl_PointSize / 顶点属性，只考验 ir3 + 光栅化 */
   const char *vs_src =
      "#version 150\n"
      "void main() {\n"
      "   vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0,\n"
      "                 gl_VertexID == 2 ? 3.0 : -1.0);\n"
      "   gl_Position = vec4(p, 0.0, 1.0);\n"
      "}\n";
   const char *fs_src =
      "#version 150\n"
      "out vec4 color;\n"
      "void main() { color = vec4(1.0, 0.0, 1.0, 1.0); }\n";
   int shader_ok = 0;
   GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src, "vs");
   GLuint fs = vs ? compile_shader(GL_FRAGMENT_SHADER, fs_src, "fs") : 0;
   if (vs && fs) {
      GLuint prog = glCreateProgram();
      glAttachShader(prog, vs);
      glAttachShader(prog, fs);
      glLinkProgram(prog);
      GLint ok = 0, len = 0;
      glGetProgramiv(prog, GL_LINK_STATUS, &ok);
      glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
      if (len > 1) {
         char *log = malloc(len);
         glGetProgramInfoLog(prog, len, NULL, log);
         printf("[probe] program log: %s\n", log);
         free(log);
      }
      if (ok) {
         glClear(GL_COLOR_BUFFER_BIT);
         glUseProgram(prog);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         glFinish();
         unsigned char px2[4] = {0};
         glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px2);
         GLenum err = glGetError();
         printf("[probe] draw pixel  = %u,%u,%u,%u (expect 255,0,255,255) glGetError=0x%x\n",
                px2[0], px2[1], px2[2], px2[3], err);
         shader_ok = (px2[0] > 200 && px2[2] > 200);
      } else {
         printf("[probe] program LINK FAILED\n");
      }
   }

   if (getenv("PROBE_DUMP")) {
      FILE *f = fopen(getenv("PROBE_DUMP"), "wb");
      if (f) { fwrite(buf, 1, W * H * 4, f); fclose(f); }
   }

   OSMesaDestroyContext(ctx);
   printf("[probe] RESULT: clear=%s shader=%s\n",
          clear_ok ? "OK" : "FAIL", shader_ok ? "OK" : "FAIL");
   fflush(stdout);
   return (clear_ok && shader_ok) ? 0 : 4;
}
