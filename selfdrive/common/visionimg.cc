#include <cassert>
#include <cinttypes>

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
    .size = size,
    .bpp = 3,
  };
}

#ifdef QCOM
EGLClientBuffer visionimg_to_egl(const VisionImg *img, void **pph) {
  assert((img->size % img->stride) == 0);
  assert((img->stride % img->bpp) == 0);

  int format = 0;
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
}
#endif

#ifdef NEOS
EGLClientBuffer visionimg_to_egl(const VisionImg *img, void **pph /* what is this? */) {
  int ret;
  assert((img->size % img->stride) == 0);
  assert((img->stride % img->bpp) == 0);

  // fill our usage
  AHardwareBuffer_Desc usage = {};
  if (img->format == VISIONIMG_FORMAT_RGB24) {
    usage.format = AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
  } else {
    assert(false);
  }
  usage.height = static_cast<uint32_t>img.height;
  usage.width = static_cast<uint32_t>img.width;
  usage.layers = 1;
  usage.stride = static_cast<uint32_t>img.stride;
  // we are passing mainly
  usage.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

  // create buffer
  AHardwareBuffer* buf = nullptr;
  ret = AHardwareBuffer_allocate(&usage, &buf);
  assert(ret == 0);
  // control our buffer
  *pph = buf;

  // actual params
  //AHardwareBuffer_Desc usage1 = {};
  AHardwareBuffer_describe(buf, NULL);

  // get buffer and return
  return (EGLClientBuffer) eglGetNativeClientBufferANDROID(buf);
}
#endif

#if defined(QCOM) || defined(NEOS)
GLuint visionimg_to_gl(const VisionImg *img, EGLImageKHR *pkhr, void **pph) {

  EGLClientBuffer clientBuf = visionimg_to_egl(img, pph);

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(display != EGL_NO_DISPLAY);

  EGLint img_attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
  EGLImageKHR image = eglCreateImageKHR(display, EGL_NO_CONTEXT,
                                        EGL_NATIVE_BUFFER_ANDROID, clientBuf, img_attrs);
  assert(image != EGL_NO_IMAGE_KHR);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
  *pkhr = image;
  return tex;
}

void visionimg_destroy_gl(EGLImageKHR khr, void *ph) {
  int ret;

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(display != EGL_NO_DISPLAY);
  eglDestroyImageKHR(display, khr);
#ifdef QCOM
  delete (private_handle_t*)ph;
#elif NEOS
  ret = AHardwareBuffer_release((AHardwareBuffer*)ph); // to release ref of hardwarebuffer
  assert(ret == 0);
  AHardwareBuffer* ph = nullptr;
#endif
}

#endif
