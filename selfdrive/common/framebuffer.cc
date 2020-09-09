#include <cstdio>
#include <cstdlib>
#include <cassert>

#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#include <EGLUtils.h>

using namespace android;

struct FramebufferState {
    sp<WindowSurfaceWrapper> windowSurface;

    EGLDisplay display;
    EGLNativeWindowType window;

    EGLint egl_major, egl_minor;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
};

extern "C" void framebuffer_swap(FramebufferState *s) {
  eglSwapBuffers(s->display, s->surface);
  assert(glGetError() == GL_NO_ERROR);
}

extern "C" void framebuffer_swap_layer(FramebufferState *S, int32_t layer) {
  s->windowSurface->swapLayer(layer);
}

extern "C" bool set_brightness(int brightness) {
  return false;
}


extern "C" void framebuffer_set_power(FramebufferState *s, int mode) {}

extern "C" void framebuffer_dl_init(void) {

}

extern "C" FramebufferState* framebuffer_init(
    const char* name, int32_t layer, int alpha,
    int *out_w, int *out_h) {
  status_t status;
  int success;

  FramebufferState *s = new FramebufferState;

  //sp<ProcessState> proc(ProcessState::self());
  //ProcessState::self()->startThreadPool();

  s->windowSurface = new WindowSurfaceWrapper(String8(name), layer); 

  // init opengl and egl
  const EGLint attribs[] = {
    EGL_RED_SIZE,     8,
    EGL_GREEN_SIZE,   8,
    EGL_BLUE_SIZE,    8,
    EGL_ALPHA_SIZE,   alpha ? 8 : 0,
    EGL_DEPTH_SIZE,   0,
    EGL_STENCIL_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    // enable MSAA
    EGL_SAMPLE_BUFFERS, 1,
    EGL_SAMPLES, 4,
    EGL_NONE,
  };

  s->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(s->display != EGL_NO_DISPLAY);

  success = eglInitialize(s->display, &s->egl_major, &s->egl_minor);
  assert(success);

  printf("egl version %d.%d\n", s->egl_major, s->egl_minor);

  //EGLint num_configs;
  //success = eglChooseConfig(s->display, attribs, &s->config, 1, &num_configs);
  //assert(success);

  s->window = s->windowSurface.getSurface().get();
  success = EGLUtils::selectConfigForNativeWindow(s->display, attribs, s->window, &&s->config);
  assert(success);
  printf("EGLUtils::selectConfigForNativeWindow() returned %d", success);

  s->surface = eglCreateWindowSurface(s->display, s->config, s->window, NULL);
  assert(s->surface != EGL_NO_SURFACE);

  const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE,
  };
  s->context = eglCreateContext(s->display, s->config, NULL, context_attribs);
  assert(s->context != EGL_NO_CONTEXT);

  EGLint w, h;
  eglQuerySurface(s->display, s->surface, EGL_WIDTH, &w);
  eglQuerySurface(s->display, s->surface, EGL_HEIGHT, &h);
  printf("egl w %d h %d\n", w, h);

  success = eglMakeCurrent(s->display, s->surface, s->surface, s->context);
  assert(success);

  printf("gl version %s\n", glGetString(GL_VERSION));

  //IPCThreadState::self()->joinThreadPool();

  if (out_w) *out_w = w;
  if (out_h) *out_h = h;

  return s;
}