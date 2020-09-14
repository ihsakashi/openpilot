#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <framebuffer2.h>
#include "common/touch.h"

typedef struct UIState {
  FramebufferState *fb;
  int fb_w, fb_h;
  EGLDisplay display;
  EGLSurface surface;
} UIState;

TouchState touch = {0};

void wait_for_touch() {
  int touch_x = -1, touch_y = -1;
  while (1) {
    int touched = touch_poll(&touch, &touch_x, &touch_y, 0);
    if (touched == 1) { break; }
  }
}

int main() {
  UIState uistate;
  UIState *s = &uistate;

  memset(s, 0, sizeof(UIState));
  s->fb = framebuffer_init("ui", 0x00010000, true,
                           &s->display, &s->surface, &s->fb_w, &s->fb_h);

  touch_init(&touch);

  printf("waiting for touch with screen on\n");
  wait_for_touch();

  printf("waiting for touch with screen off\n");
  wait_for_touch();
  printf("done\n");
}

