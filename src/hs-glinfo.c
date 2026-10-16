/* hs-glinfo: minimal OpenGL ES 2 self-test without any dev headers (EGL/GLES are dlopen()ed).
   Renders a triangle off-screen (Mesa llvmpipe or a real GPU), reads a pixel back and prints driver info. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *P; typedef unsigned u32; typedef int i32; typedef unsigned char u8;
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 4
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
#define GL_COLOR_BUFFER_BIT 0x4000
static P egl, gles;
static P sym(const char *n){ P p = dlsym(gles, n); if(!p) p = dlsym(egl, n); if(!p){ fprintf(stderr, "missing symbol %s\n", n); exit(2);} return p; }
#define F(ret, name, args) ret (*name) args = (ret (*) args)sym(#name)
static int die(const char *m){ printf("FAIL: %s\n", m); return 1; }
int main(void){
  egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL); if(!egl) return die("libEGL.so.1 not found (3D runtime not installed)");
  gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL); if(!gles) return die("libGLESv2.so.2 not found");
  F(P, eglGetProcAddress, (const char *));
  F(u32, eglBindAPI, (u32)); F(u32, eglInitialize, (P, i32 *, i32 *)); F(u32, eglChooseConfig, (P, const i32 *, P *, i32, i32 *));
  F(P, eglCreateContext, (P, P, P, const i32 *)); F(u32, eglMakeCurrent, (P, P, P, P));
  F(const char *, eglQueryString, (P, i32));
  P (*getdpy)(u32, P, const i32 *) = (P (*)(u32, P, const i32 *))eglGetProcAddress("eglGetPlatformDisplayEXT");
  if(!getdpy) return die("eglGetPlatformDisplayEXT missing");
  P dpy = getdpy(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL); if(!dpy) return die("no EGL display");
  i32 maj, min; if(!eglInitialize(dpy, &maj, &min)) return die("eglInitialize failed");
  printf("EGL %d.%d  vendor: %s\n", maj, min, eglQueryString(dpy, 0x3053));
  eglBindAPI(EGL_OPENGL_ES_API);
  i32 ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE }, n = 0; P cfg;
  if(!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1){ cfg = NULL; }
  i32 xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  P ctx = eglCreateContext(dpy, cfg, NULL, xa); if(!ctx) return die("eglCreateContext failed");
  if(!eglMakeCurrent(dpy, NULL, NULL, ctx)) return die("eglMakeCurrent failed (surfaceless context unsupported)");
  F(const u8 *, glGetString, (u32));
  printf("GL vendor:   %s\nGL renderer: %s\nGL version:  %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
  F(void, glGenTextures, (i32, u32 *)); F(void, glBindTexture, (u32, u32)); F(void, glTexImage2D, (u32, i32, i32, i32, i32, i32, u32, u32, const void *));
  F(void, glGenFramebuffers, (i32, u32 *)); F(void, glBindFramebuffer, (u32, u32)); F(void, glFramebufferTexture2D, (u32, u32, u32, u32, i32));
  F(u32, glCheckFramebufferStatus, (u32)); F(u32, glCreateShader, (u32)); F(void, glShaderSource, (u32, i32, const char *const *, const i32 *));
  F(void, glCompileShader, (u32)); F(void, glGetShaderiv, (u32, u32, i32 *)); F(u32, glCreateProgram, (void)); F(void, glAttachShader, (u32, u32));
  F(void, glLinkProgram, (u32)); F(void, glGetProgramiv, (u32, u32, i32 *)); F(void, glUseProgram, (u32)); F(void, glViewport, (i32, i32, i32, i32));
  F(void, glClearColor, (float, float, float, float)); F(void, glClear, (u32)); F(void, glVertexAttribPointer, (u32, i32, u32, u8, i32, const void *));
  F(void, glEnableVertexAttribArray, (u32)); F(void, glDrawArrays, (u32, i32, i32)); F(void, glReadPixels, (i32, i32, i32, i32, u32, u32, void *));
  F(void, glFinish, (void)); F(void, glBindAttribLocation, (u32, u32, const char *));
  u32 tex, fbo; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return die("framebuffer incomplete");
  const char *vs = "attribute vec2 p; void main(){ gl_Position = vec4(p, 0.0, 1.0); }";
  const char *fs = "precision mediump float; void main(){ gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }";
  u32 v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), pr = glCreateProgram(); i32 ok = 0;
  glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glGetShaderiv(v, GL_COMPILE_STATUS, &ok); if(!ok) return die("vertex shader");
  glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glGetShaderiv(f, GL_COMPILE_STATUS, &ok); if(!ok) return die("fragment shader");
  glAttachShader(pr, v); glAttachShader(pr, f); glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glGetProgramiv(pr, GL_LINK_STATUS, &ok); if(!ok) return die("program link");
  glViewport(0, 0, 64, 64); glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(pr);
  static const float tri[] = { -1, -1, 3, -1, -1, 3 };   /* covers the whole target */
  glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri); glEnableVertexAttribArray(0); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
  u8 px[4] = {0}; glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  printf("pixel (32,32): %u,%u,%u,%u (expected 0,255,0,255)\n", px[0], px[1], px[2], px[3]);
  if(px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255){ puts("3D self-test: OK"); return 0; }
  return die("wrong pixel value");
}
