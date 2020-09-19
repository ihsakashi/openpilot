#include <cassert>
#include <cstring>
#include <cinttypes>

#include <unistd.h>

// GraphicBuffer Private API <= 25
#ifdef QCOM
#include <system/graphics.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <gralloc_priv.h>
#endif

// HardwareBuffer NDK API >= 26
#ifdef NEOS
#include <android/hardware_buffer.h>
#endif

#if defined(QCOM) || defined(NEOS)
#include <GLES3/gl3.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>

#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>

#endif

#include "common/util.h"
#include "common/visionbuf.h"

#include "common/visionimg.h"

#ifdef QCOM

using namespace android;

// from libadreno_utils.so
extern "C" void compute_aligned_width_and_height(int width,
                                      int height,
                                      int bpp,
                                      int tile_mode,
                                      int raster_mode,
                                      int padding_threshold,
                                      int *aligned_w,
                                      int *aligned_h);
#endif

void visionimg_compute_aligned_width_and_height(int width, int height, int *aligned_w, int *aligned_h) {
#if defined(QCOM) && !defined(QCOM_REPLAY)
  compute_aligned_width_and_height(ALIGN(width, 32), ALIGN(height, 32), 3, 0, 0, 512, aligned_w, aligned_h);
#else
  *aligned_w = width; *aligned_h = height;
#endif
}

VisionImg visionimg_alloc_rgb24(int width, int height, VisionBuf *out_buf) {
  int aligned_w = 0, aligned_h = 0;
  visionimg_compute_aligned_width_and_height(width, height, &aligned_w, &aligned_h);

  int stride = aligned_w * 3;
  size_t size = (size_t) aligned_w * aligned_h * 3;

  VisionBuf buf = visionbuf_allocate(size);

  *out_buf = buf;

  return (VisionImg){
    .fd = buf.fd,
    .format = VISIONIMG_FORMAT_RGB24,
    .width = width,
    .height = height,
    .stride = stride,
    .bpp = 3,
    .size = size,
  };
}

#ifdef NEOS
hwbuf_handle* hardwarebuffer_alloc(int width, int height, int stride, int format, int usage) {
  int ret;

  // our usage
  AHardwareBuffer_Desc usage = {
    .height = height,
    .width = width,
    .layers = 1,
    .format = format,
    .usage = usage,
    .stride = stride,
    .rfu0 = 0,
    .rfu1 = 0.
  };

  // create buffer and give usage
  AHardwareBuffer* buf = nullptr;
  ret = AHardwareBuffer_allocate(&usage, &buf);
  assert(ret == 0);

  // return the ref we made
  return reinterpret_cast<hnd*>(buf);
}
#endif

#if defined(QCOM) || defined(NEOS)

EGLClientBuffer visionimg_to_egl(const VisionImg *img, void **pph) {
  assert((img->size % img->stride) == 0);
  assert((img->stride % img->bpp) == 0);

  int format = 0;
#ifdef NEOS
  if (img->format == VISIONIMG_FORMAT_RGB24) {
    format = AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
  } else {
    assert(false);
  }

  hwbuf_handle *hnd = hardwarebuffer_alloc(img->width, img->height,
                        img->stride, format, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE);

  *pph = hnd;

  AHardwareBuffer* buf = reinterpret_cast<AHardwareBuffer*>(hnd);

  return (EGLClientBuffer) eglGetNativeClientBufferANDROID(buf);
#else
  if (img->format == VISIONIMG_FORMAT_RGB24) {
    format = HAL_PIXEL_FORMAT_RGB_888;
  } else {
    assert(false);
  }

  private_handle_t* hnd = new private_handle_t(img->fd, img->size,
                             private_handle_t::PRIV_FLAGS_USES_ION|private_handle_t::PRIV_FLAGS_FRAMEBUFFER,
                             0, format,
                             img->stride/img->bpp, img->size/img->stride,
                             img->width, img->height);

  GraphicBuffer* gb = new GraphicBuffer(img->width, img->height, (PixelFormat)format,
                                        GraphicBuffer::USAGE_HW_TEXTURE, img->stride/img->bpp, hnd, false);
  // GraphicBuffer is ref counted by EGLClientBuffer(ANativeWindowBuffer), no need and not possible to release.
  *pph = hnd;
  return (EGLClientBuffer) gb->getNativeBuffer();
#endif
}

GLuint visionimg_to_gl(const VisionImg *img, EGLImageKHR *pkhr, void **pph) {

  EGLClientBuffer buf = visionimg_to_egl(img, pph);

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(display != EGL_NO_DISPLAY);

  EGLint img_attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
  EGLImageKHR image = eglCreateImageKHR(display, EGL_NO_CONTEXT,
                                        EGL_NATIVE_BUFFER_ANDROID, buf, img_attrs);
  assert(image != EGL_NO_IMAGE_KHR);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
  *pkhr = image;
  return tex;
}

void visionimg_destroy_gl(EGLImageKHR khr, void *ph) {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(display != EGL_NO_DISPLAY);
  eglDestroyImageKHR(display, khr);
#ifdef NEOS
  int ret;
  AHardwareBuffer* buf = reinterpret_cast<AHardwareBuffer*>(ph);
  ret = AHardwareBuffer_release(buf);
  assert(ret == 0);
  buf = nullptr;
#else
  delete (private_handle_t*)ph;
#endif
}

#endif
