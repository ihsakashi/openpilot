#include "camera_android.h"

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "common/util.h"
#include "common/timing.h"
#include "common/swaglog.h"
#include "buffering.h"

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <cutils/native_handle.h>

extern volatile sig_atomic_t do_exit;

#define FRAME_WDITH 1440
#define FRAME_HEIGHT 1080

struct Camera2Device {
    char *camera_id;
    int32_t orientation;
    int32_t fpsmin;
    int32_t fpsmax;
    int format;
};

namespace {

static const char *camera_status_string(camera_status_t val)
{
    switch(val) {
        RETURN_CASE(ACAMERA_OK)
        RETURN_CASE(ACAMERA_ERROR_UNKNOWN)
        RETURN_CASE(ACAMERA_ERROR_INVALID_PARAMETER)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DISCONNECTED)
        RETURN_CASE(ACAMERA_ERROR_NOT_ENOUGH_MEMORY)
        RETURN_CASE(ACAMERA_ERROR_METADATA_NOT_FOUND)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DEVICE)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_SERVICE)
        RETURN_CASE(ACAMERA_ERROR_SESSION_CLOSED)
        RETURN_CASE(ACAMERA_ERROR_INVALID_OPERATION)
        RETURN_CASE(ACAMERA_ERROR_STREAM_CONFIGURE_FAIL)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_IN_USE)
        RETURN_CASE(ACAMERA_ERROR_MAX_CAMERA_IN_USE)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DISABLED)
        RETURN_CASE(ACAMERA_ERROR_PERMISSION_DENIED)
        RETURN_DEFAULT(ACAMERA_ERROR_UNKNOWN)
    }
}

static constexpr int ImageFormat = AIMAGE_FORMAT_YUV_420_888

// TODO: do actions
static void camera2_device_on_disconnected_error(void* context, ACameraDevice* device) {
  LOG("Camera(id: %s) is diconnected.\n", ACameraDevice_getId(device));
}
static void camera2_device_on_error(void* context, ACameraDevice* device,
                                int error) {
  LOGE("Error(code: %d) on Camera(id: %s).\n", error,
       ACameraDevice_getId(device));
}
// Capture Callbacks
static void camera2_capture_session_on_ready(void* context,
                                  ACameraCaptureSession* session) {
  LOG("Session is ready.\n");
}
static void camera2_capture_session_on_activated(void* context,
                                   ACameraCaptureSession* session) {
  LOG("Session is activated.\n");
}

void camera_open(CameraState *s, VisionBuf *camera_bufs, bool rear) {
	LOG("open camera2 device");
	camera_status_t camera_status = ACAMERA_OK;

	s->deviceStateCallbacks.context = s;
	s->deviceStateCallbacks.onDisconnected = camera2_device_on_disconnected_error();
	s->deviceStateCallbacks.onError = camera2_device_on_error();

	assert(s->c2d->camera_id, "No device found?");

	// open camera device
	ACameraDevice *cameraDevice;
	camera_status_t camera_status = ACameraManager_openCamera(s->cameraManager, s->c2d->camera_id,
										&s->deviceStateCallbacks, &cameraDevice);
	assert(camera_status == ACAMERA_OK,  "Failed to open camera with id %s, error: %s.\n",
               s->c2d->camera_id, camera_status_string(camera_status));)

	// assign device
	LOG("camera opened!");
	s->cameraDevice = cameraDevice;

	// start image reader stream

	assert(camera_bufs);
	s->camera_bufs = camera_bufs;
}

static void create_image_reader(CameraState *s) {
	media_status_t media_status;

	media_status = AImageReader_new(s->ci.frame_width, s->ci.frame_size, s->c2d.format, FRAME_BUF_COUNT)
}

void camera_close(CameraState *s) {}

void camera_release_buffer(void *cookie, int buf_idx) {
	CameraState *s = static_cast<CameraState *>(cookie);
}

void camera2_image_callback(void *context, AImageReader *reader) {}

void camera_init(CameraState *s, unsigned int fps) {
	LOG("camera init %d", s->camera_id);
	assert(s->camera_id < ARRAYSIZE(cameras_supported));

	// make sure frame stuff set
	assert(s->ci.frame_width != 0);
	s->frame_size = s->ci.frame_height * s->ci.frame_stride;

	// NOTE: for a stable framerate control of device, choose video capture over preview
	// although preview is probably better
	if (fps > s->c2d.fpsmin && fps < s->c2d.fpsmax) {
		s->fps = fps;
	} else {
		s->fps = 20;
	}

	tbuffer_init2(&s->camera_tb, FRAME_BUF_COUNT "frame", camera_release_buffer, s);
	
}

void camera_detect(CameraState *s, int camera_id, bool rear) {
    LOG("detecting cameras");

	camera_status_t camera_status = ACAMERA_OK;
	ACameraManager *cameraManager = ACameraManager_create();
	s->cameraManager = cameraManager

	camera_status = ACameraManager_getCameraIdList(s->cameraManager, &cameraIdList);
	assert(camera_status == ACAMERA_OK, "failed to get camera(s) list: %d ", camera_status);
	assert(cameraIdList->numCameras > 1, "no cameras detected!");

	bool back_found = false;
	bool front_found = false;

	//bool desired scaler = false;
	int desired_width = FRAME_WIDTH;
	int desired_height = FRAME_HEIGHT;

	const char *camId = nullptr;
	for (int i = 0; i < cameraIdList->numCameras; i++) {
		camId = cameraIdList->cameraIds[i];
		LOG("is camera %d", camId);
		// start
		camera_status = ACameraManager_getCameraCharacteristics(s->cameraManager, camId, &cameraMetadata);

		// hardware support
		ACameraMetadata_const_entry hardwareLevel;
		ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL, &hardwareLevel);
		std::string supportedHardwareLevel = "unknown";
		switch (hardwareLevel.data.u8[0]) {
			case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED:
				supportedHardwareLevel = "limited";
				break;
			case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_FULL:
				supportedHardwareLevel = "full";
				break;
			case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY:
				supportedHardwareLevel = "legacy";
				break;
			case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_3:
				supportedHardwareLevel = "3";
				break;
		}
		if (supportedHardwareLevel == "legacy")
			continue;

		// face
		ACameraMetadata_const_entry face;
		ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_LENS_FACING, &face);
		auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(face.data.u8[0]);
		LOG("camera is facing %d", facing);
		if ((back_found && rear) || (front_found && !rear))
			continue;
		if ((rear && facing == ACAMERA_LENS_FACING_BACK) || (!rear && facing == ACAMERA_LENS_FACING_FRONT)) {
			s->c2d.camera_id;
			if (facing == ACAMERA_LENS_FACING_BACK) {
				back_found = true;
			} else {
				front_found = true;
			}
		}

		// orientation
		ACameraMetadata_const_entry orientation;
		ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
		int32_t angle = orientation.data.i32[0];
		LOG("camera angle is %d", angle);
		s->c2d.orientation = angle;

		// fps
		ACameraMetadata_const_entry supportedFpsRanges;
		ACameraMetadata_getConstEntry(cameraMetadata, ACAMERACONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &supportedFpsRanges);
		for (int i = 0; i < supportedFpsRanges.count; i += 2) {
			int32_t fpsmin = supportedFpsRanges.data.i32[i];
			int32_t fpsmax = supportedFpsRanges.data.i32[i + 1];
			LOG("camera supported FPS range: [%d-%d]", fpsmin, fpsmax);
			}
		s->c2d.fpsmin = fpsmin
		s->c2d.fpsmax = fpsmax

		// always on hdr support
		// NOT scene mode hdr!

		// supported format - only yuv for now
		s->c2d.format = ImageFormat

		// scaler
		ACameraMetadata_const_entry scaler;
		ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &scaler);
		for (int i = 0; i < scaler.count; i += 4) {
			// We are only interested in output streams, so skip input stream
			int32_t input = scaler.data.i32[i + 3];
			if (input)
				continue;
			int32_t format = scaler.data.i32[i + 0];
			if (format == s->c2d.format) {
				int32_t width = scaler.data.i32[i + 1];
				int32_t height = scaler.data.i32[i + 2];
				LOG("camera has available size of w %d, h %d for format %d", width, height, format);
				if (width == desired_width && height == desired_height) {
					//desired_scaler = true;
					LOG("desired scaler res found!")
					s->ci.frame_width = width;
					s->ci.frame_height = height;
					s->ci.frame_stride = width * 3;
					break;
				} // else find best match
			}
		}
		// done
		ACameraMetadata_free(cameraMetadata);
	}
	ACameraManager_deleteCameraIdList(cameraIdList);
	//ACameraManager_delete(s->cameraManager);
}

static void* rear_thread(void *arg) {}

void front_thread(CameraState *s) {}

} // namespace

void cameras_init(DualCameraState *s) {

}

void camera_autoexposure(CameraState *s, float grey_frac) {}

void cameras_open(DualCameraState *s, VisionBuf *camera_bufs_rear,
                    VisionBuf *camera_bufs_focus, VisionBuf *camera_bufs_stats,
                    VisionBuf *camera_bufs _front) {}

void cameras_close(DualCameraState *s) {}

void cameras_run(DualCameraState *s) {}
