#pragma once

#include <stdbool.h>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImageReader.h>
#include "native_handle.h"

#include "camera_common.h"

typedef struct CameraState {
  int camera_id;
  CameraInfo ci;

  int fps;
  int format;
  float digital_gain;
  float cur_gain_frac;

  // camera2ndk id
  char camera2_id;

  // device
  ACameraMetadata *metadata;
  ACameraDevice *device;

  // session
  ACameraCaptureSession *session;
  ACaptureSessionOutputContainer *outputs;
  ACaptureSessionOutput *readerOutput;

  // request
  ACaptureRequest *request;
  ACameraOutputTarget *reqReaderOutput;

  // image reader and window
  AImageReader *reader;
  native_handle_t *window;
  AImageReader_ImageListener *readerCb;

  // callbacks
  ACameraDevice_StateCallbacks deviceCb;
  ACameraCaptureSession_stateCallbacks sessionCb;

  mat3 transform;

  CameraBuf buf;
} CameraState;

typedef struct MultiCameraState {
  int ispif_fd;

  ACameraManager *manager; // how to let devices use
  ACameraIdList *idList;

  CameraState rear;
  CameraState front;

  SubMaster *sm;
  PubMaster *pm;
} MultiCameraState;

void cameras_init(MultiCameraState *s, cl_device_id device_id, cl_context ctx);
void cameras_open(MultiCameraState *s);
void cameras_run(MultiCameraState *s);
void cameras_close(MultiCameraState *s);
void camera_autoexposure(CameraState *s, float grey_frac);