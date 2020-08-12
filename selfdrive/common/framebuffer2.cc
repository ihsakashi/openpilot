#include <cstdio>
#include <cstdlib>
#include <cassert>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGLUtils.h>

#include <window_wrapper.h>

using namespace android;

struct FramebufferState {
    EGLDisplay display;
    EGLint egl_major, egl_minor;
    EGLConfig config;

    EGLContext context;
    EGLSurface surface;
};

/**
 * window_wrapper creates an anativewindow without jni.
 * framebuffer2 will retain as much of how GLES is initalised currently.
 * 
 * TODO: Manipulate z-order of surfaces.
 * 
 */

FramebufferState* draw_egl_window(sp<WindowSurfaceWrapper> windowSurface) {
  int success;
  sp<SurfaceControl> surfaceControl = windowSurface->getSurfaceControl();
  FramebufferState *s = new FramebufferState;

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

  EGLNativeWindowType window = surfaceControl->getSurface().get();
  success = EGLUtils::selectConfigForNativeWindow(s->display, attribs, window, &s->config);
  assert(success);
  printf("EGLUtils::selectConfigForNativeWindow() returned %d", success);

  s->surface = eglCreateWindowSurface(s->display, s->config, window, nullptr);
  assert(s->surface != EGL_NO_SURFACE);

  const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE,
  };
  s->context = eglCreateContext(s->display, s->config, nullptr, context_attribs);
  assert(s->context != EGL_NO_CONTEXT);

  success = eglMakeCurrent(s->display, s->surface, s->surface, s->context);
  assert(success);

  EGLint w, h;
  eglQuerySurface(s->display, s->surface, EGL_WIDTH, &w);
  eglQuerySurface(s->display, s->surface, EGL_HEIGHT, &h);
  printf("egl w %d h %d\n", w, h);

  printf("gl version %s\n", glGetString(GL_VERSION));

  return s;
}

void framebuffer_swap(FramebufferState *s) {
  eglSwapBuffers(s->display, s->surface);
  assert(glGetError() == GL_NO_ERROR);
}

int framebuffer_swap_layer(sp<WindowSurfaceWrapper> windowSurface, int32_t layer) {
  windowSurface->swapLayer(layer)
}

int window_init(sp<WindowSurfaceWrapper> windowSurface, const String8& name, int32_t layer){
  sp<ProcessState> proc(ProcessState::self());
  ProcessState::self()->startThreadPool();

  sp<WindowSurfaceWrapper> windowSurface(new WindowSurfaceWrapper(String8(name), layer)));

  draw_egl_window(windowSurface->getSurface().get(), windowSurface->width(), windowSurface->height());

  IPCThreadState::self()->joinThreadPool();

  return EXIT_SUCCESS;
}