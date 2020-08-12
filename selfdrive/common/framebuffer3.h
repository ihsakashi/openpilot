#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FramebufferState FramebufferState;

FramebufferState* framebuffer_init(
    const char* name, int32_t layer, int alpha,
    int *out_w, int *out_h);

void framebuffer_swap(FramebufferState *s);

#ifdef __cplusplus
}
#endif
