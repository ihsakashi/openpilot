#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "common/mat.h"
#include "common/visionbuf.h"
#include "common/buffering.h"

#include "camera_common.h"

#define FRAME_BUF_COUNT 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CameraState {
    int camera_id; // camerad specific

    CameraInfo ci;
    int frame_size;
    Camera2Device *cd; // Camera2NDK assigned device

    VisionBuf *camera_bufs;
    FrameMetadata camera_bufs_metadata[FRAME_BUF_COUNT];
    TBuffer camera_tb;

    int32_t format;
    int fps;

    // Camera2NDK
    ACameraManager *cameraManager;
    ACameraDevice *cameraDevice;
    ACameraCaptureSession *captureSession;
    ACaptureSessionOutputContainer *captureSessionOutputContainer;

    ACaptureRequest *capturePreviewRequest;
    ACameraOutputTarget *cameraCaptureOutputTarget;
    ACameraOutputTarget *cameraPreviewOutputTarget;
    ACaptureSessionOutput *sessionCaptureOutput;
    ACaptureSessionOutput *sessionPreviewOutput;

    ACameraDevice_StateCallbacks deviceStateCallbacks;
    ACameraCaptureSession_stateCallbacks captureSessionStateCallbacks;
} CameraState;

struct Camera2Device {
    char *camera_id;
    int32_t orientation;

};

typedef struct DualCameraState {
  int ispif_fd;

  CameraState rear;
  CameraState front;
} DualCameraState;

void cameras_init(DualCameraState *s);
void cameras_open(DualCameraState *s, VisionBuf *camera_bufs_rear, VisionBuf *camera_bufs_focus, VisionBuf *camera_bufs_stats, VisionBuf *camera_bufs_front);
void cameras_run(DualCameraState *s);
void cameras_close(DualCameraState *s);
void camera_autoexposure(CameraState *s, float grey_frac);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif