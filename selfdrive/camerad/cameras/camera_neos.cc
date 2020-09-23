#include "camera_neos.h"

#include <stdint.h>
#include <unistd.h>
#include <cassert>
#include <string.h>
#include <signal.h>

#include "common/util.h"
#include "common/timing.h"
#include "common/swaglog.h"
#include "common/params.h"

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include "native_handle.h"

extern volatile sig_atomic_t do_exit;

namespace {

void camera_open(CameraState *s) {}

void camera_close(CameraState *s) {}

void camera_init(CameraState *s, int camera_id, unsigned int fps, cl_device_id device_id, cl_context ctx) {}

static void* rear_thread(void *arg) {}

void front_thread(CameraState *s) {}

} // namespace

void cameras_init(MultiCameraState *s, cl_device_id device_id, cl_context ctx) {}

void camera_autoexposure(CameraState *s, float grey_frac) {}

void cameras_open(MultiCameraState *s) {}

void cameras_close(MultiCameraState *s) {}

void camera_process_rear(MultiCameraState *s, CameraState *c, int cnt, ) {}

void camera_process_front(MultiCameraState *s, CameraState *c, int cnt, ) {}
