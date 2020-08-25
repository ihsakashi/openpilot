#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FramebufferState FramebufferState;

FramebufferState* framebuffer_init(
    const char* name, int32_t layer, int alpha,
    int *out_w, int *out_h);

void framebuffer_swap(FramebufferState *s);
void framebuffer_swap_layer(FramebufferState *s);
void framebuffer_show(FramebufferState *s);
void framebuffer_hide(FramebufferState *s);
void framebuffer_set_power(FramebufferState *s, int mode);
bool set_brightness(int brightness);

#ifdef __cplusplus
}
#endif
