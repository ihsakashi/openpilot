#include "camera_android2.h"

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
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <native_window.h>

extern volatile sig_atomic_t do_exit;

// should always work
#define FRAME_WIDTH  1440
#define FRAME_HEIGHT 1080
#define FORMAT_YUV_420_8888 0x23 // don't intend to use AIMAGE thing

struct Camera2Device {
    char *camera_id;
    int32_t orientation;
};

// CAMERA2 Status
static const char* camera2_status_to_string(camera_status_t status) {
	if (status == ACAMERA_OK) {
		return "ACAMERA_OK";
	} else if (status == ACAMERA_ERROR_BASE) {
		return "ACAMERA_ERROR_BASE";
	} else if (status == ACAMERA_ERROR_UNKNOWN) {
		return "ACAMERA_ERROR_UNKNOWN";
	} else if (status == ACAMERA_ERROR_INVALID_PARAMETER) {
		return "ACAMERA_ERROR_INVALID_PARAMETER";
	} else if (status == ACAMERA_ERROR_CAMERA_DISCONNECTED) {
		return "ACAMERA_ERROR_CAMERA_DISCONNECTED";
	} else if (status == ACAMERA_ERROR_NOT_ENOUGH_MEMORY) {
		return "ACAMERA_ERROR_NOT_ENOUGH_MEMORY";
	} else if (status == ACAMERA_ERROR_METADATA_NOT_FOUND) {
		return "ACAMERA_ERROR_METADATA_NOT_FOUND";
	} else if (status == ACAMERA_ERROR_CAMERA_DEVICE) {
		return "ACAMERA_ERROR_CAMERA_DEVICE";
	} else if (status == ACAMERA_ERROR_CAMERA_SERVICE) {
		return "ACAMERA_ERROR_CAMERA_SERVICE";
	} else if (status == ACAMERA_ERROR_SESSION_CLOSED) {
		return "ACAMERA_ERROR_SESSION_CLOSED";
	} else if (status == ACAMERA_ERROR_INVALID_OPERATION) {
		return "ACAMERA_ERROR_INVALID_OPERATION";
	} else if (status == ACAMERA_ERROR_STREAM_CONFIGURE_FAIL) {
		return "ACAMERA_ERROR_STREAM_CONFIGURE_FAIL";
	} else if (status == ACAMERA_ERROR_CAMERA_IN_USE) {
		return "ACAMERA_ERROR_CAMERA_IN_USE";
	} else if (status == ACAMERA_ERROR_MAX_CAMERA_IN_USE) {
		return "ACAMERA_ERROR_MAX_CAMERA_IN_USE";
	} else if (status == ACAMERA_ERROR_CAMERA_DISABLED) {
		return "ACAMERA_ERROR_CAMERA_DISABLED";
	} else if (status == ACAMERA_ERROR_PERMISSION_DENIED) {
		return "ACAMERA_ERROR_PERMISSION_DENIED";
	} else if (status == -10014) { // Can't use ACAMERA_ERROR_UNSUPPORTED_OPERATION, not present in NDK 17c...
		return "ACAMERA_ERROR_UNSUPPORTED_OPERATION";
	}

	return "UNKNOWN";
}

/**
 * CAMERA2NDK Implementation
 */
namespace {
void camera_open(CameraState *s, VisionBuf *camera_bufs, bool rear) {
	// NdkCameraDevice.h
	LOG("opening camera2ndk camera device");
	camera_status_t camera_status = ACAMERA_OK;

	s->deviceStateCallbacks.context = s;
	s->deviceStateCallbacks.onDisconnected = camera2_device_disconnected();
	s->deviceStateCallbacks.onError = camera2_device_error;

	if (!s->cd->camera_id) {
		LOGE("no device selected?");
	}

	//now we open stream for device
	ACameraDevice *cameraDevice;
	camera_status_t camera_status = ACameraManager_openCamera(s->cameraManager, s->cd->camera_id, &s->deviceStateCallbacks, &cameraDevice);
	if (camera_status != ACAMERA_OK) {
		LOGE("failed to open camera %s. error is %s", s->cd->camera_id, camera2_status_to_string(camera_status));
		return;
	}

	LOG("camera %s opened!");
	s->cameraDevice = cameraDevice;

	assert(camera_bufs);
	s->camera_bufs = camera_bufs;
}

/*static void camera2_device_disconnected(void *context, ACameraDevice *device) {
	LOGE("camera %s disconnected", ACameraDevice_getId((device));
	CameraState *s = (CameraState *)context;
	camera_close(s)
	//fixme
}*/

/*static void camera2_device_error(void *context, ACameraDevice *device, int err) {
	LOGE("camera %s has error %s", ACameraDevice_getId((device), err);
	CameraState *s = (CameraState *)context;
	camera_close(s)
	//fixme
}*/

void camera_close(CameraState *s) {
	LOG("closing camera");

	tbuffer_stop(&s->camera_tb);

	if (s->cameraDevice) {
		camera_status_t camera_status = ACameraDevice_close(s->cameraDevice);
		if (camera_status != ACAMERA_OK) {
			LOGE("failed to close camera %s. error is %s", s->cd->camera_id, camera2_status_to_string(camera_status));
		} else {
			LOG("camera has closed");
		}
		s->cameraDevice = nullptr;
	}
}

void camera_detect(CameraState *s, bool rear) {
    LOG("detecting cameras");

	ACameraIdList *cameraIdList = nullptr;
	ACameraMetadata *cameraMetadata = nullptr;

	camera_status_t camera_status = ACAMERA_OK;
	ACameraManager *cameraManager = ACameraManager_create();
	s->cameraManager = cameraManager

	camera_status = ACameraManager_getCameraIdList(s->cameraManager, &cameraIdList);
	if (camera_status != ACAMERA_OK) {
		LOGE("failed to get camera(s) list: %d ", camera_status);
		return;
	}
	if (cameraIdList->numCameras < 1) {
		LOGE("no cameras detected!");
		return;
	}

	bool front_facing_found = false;
	bool back_facing_found = false;

	const char *camId = nullptr;
	for (int i = 0; i < cameraIdList->numCameras; i++) {
		camId = cameraIdList->cameraIds[i];

		camera_status = ACameraManager_getCameraCharacteristics(s->cameraManager, camId, &cameraMetadata);
		if (camera_status != ACAMERA_OK) {
			LOGE("failed to get camera %s characteristics", camId);
		} else {
			/**
			 * Grab all characteristics, sort what we want into camera2devices
			 * 1 back, 1 front shooter
			 * not legacy
			 * TODO: everything just returns on oopsies
			 */

			// orientation
			ACameraMetadata_const_entry orientation;
			ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
			int32_t angle = orientation.data.i32[0];

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

			// fps - it should support 20fps np, as long as we aren't very large res
			/*ACameraMetadata_const_entry supportedFpsRanges;
			ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &supportedFpsRanges);
			for (int i = 0; i < supportedFpsRanges.count; i += 2) {
				int32_t min = supportedFpsRanges.data.i32[i];
				int32_t max = supportedFpsRanges.data.i32[i + 1];
				ms_message("[Camera2 Capture] Supported FPS range: [%d-%d]", min, max);
			}*/

			// always on hdr mode
			// NOT scene mode hdr! thats for photos and hurts
			bool hdr = true;

			// formats - YUV only
			// TODO: add check for others
			// you can also create a stream for EON viewport in RGBA888 but would need refactoring
			// ^ this would be better but YUV fully is better
			s->format = FORMAT_YUV_420_888;

			// available + recommended? scaler
			ACameraMetadata_const_entry scaler;
			ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &scaler);

			int askWidth = FRAME_WIDTH;
			int askHeight = FRAME_HEIGHT;

			// scaler is int32[n * 4] is (format, width, height, input)
			for (int i = 0; 1 < scaler.count; i += 4) {
				int32_t input = entry.data.i32[i + 3];
				int32_t format = entry.data.i32[i + 3];

				if (input) {
					continue;
				}

				// check format
				if (format == s->format) {
					int32_t width = scaler.data.i32[i + 1];
					int32_t height = scaler.data.i32[i + 2];
					// check if frame size is there already
					if (width == askWidth && height == askHeight) {
						LOG("scaler res supported");
					} else { // we get closest res with aspect ratio
						return; //lazy
					}
				}
			}

			// facing
			ACameraMetadata_const_entry face;
			ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_LENS_FACING, &face);
			bool back_facing = face.data.u8[0] == ACAMERA_LENS_FACING_BACK;
			std::string facing = std::string(!back_facing ? "front" : "back");

			// out camerainfo and camera2device
			if (supportedHardwareLevel != "limited" || supportedHardwareLevel != "unknown") {
				if (back_facing && !back_facing_found && rear) {
					s->camera_id = CAMERA_ID_A0;
					back_facing_found = true;
				} else {
					s->camera_id = CAMERA_ID_A1;
					front_facing_found = true;
				}
				s->cd.camera_id = camId;
				s->cd.orientation = angle;
				s->ci.frame_width = askWidth;
				s->ci.frame_height = askHeight;
				s->ci.frame_stride = askWidth * 3;
				s->ci.hdr = hdr;
			}
			// done
			ACameraMetadata_free(cameraMetadata);
		}
	}
	ACameraManager_deleteCameraIdList(cameraIdList);
	//ACameraManager_delete(s->cameraManager);
}

void camera_release_buffer(void *cookie, int buf_idx) {
  CameraState *s = static_cast<CameraState *>(cookie);
}

void camera_init(CameraState *s, unsigned int fps) {
	assert(s->camera_id < ARRAYSIZE(cameras_supported));
	assert(s->ci.frame_width != 0);
	s->frame_size = s->ci.frame_height * s->ci.frame_stride;
	s->fps = fps;

	tbuffer_init2(&s->camera_tb, FRAME_BUF_COUNT, "frame", camera_release_buffer, s);
}

static void* rear_thread(void *arg) {
	int err;
	
	set_thread_name("webcam_rear_thread");
	CameraState* s = (CameraState*)arg;

	if (!s->cameraDevice) {
		err = 1;
	}

	uint32_t frame_id = 0;
	

	while (!do_exit) {}
}

void front_thread(CameraState *s) {}

} // namespace

/**
 * CAMERAD specific
 */
void cameras_init(DualCameraState *s) {
	memset(s, 0, sizeof(*s));

	camera_detect(&s->rear, true);
	camera_detect(&s->front, false);

	camera_init(&s->rear);
	camera_init(&s->front);
}

void camera_autoexposure(CameraState *s, float grey_fac) {}

void cameras_open(DualCameraState *s, VisionBuf *camera_bufs_rear,
                VisionBuf *camera_bufs_focus, VisionBuf *camera_bufs_stats,
                VisionBuf *camera_bufs_front) {
	assert(camera_bufs_rear);
	assert(camera_bufs_front);
	int err;

	LOG("*** open rear ***");
	camera_open(&s->rear, camera_bufs_front, true);

	LOG("*** open front ***");
	camera_open(&s->rear, camera_bufs_rear, true);
}

void cameras_close(DualCameraState *s) {
  camera_close(&s->rear);
  camera_close(&s->front);
}

void cameras_run(DualCameraState *s) {
  set_thread_name("android_thread");

  int err;
  pthread_t rear_thread_handle;
  err = pthread_create(&rear_thread_handle, NULL,
                        rear_thread, &s->rear);
  assert(err == 0);

  front_thread(&s->front);

  err = pthread_join(rear_thread_handle, NULL);
  assert(err == 0);
  cameras_close(s);
}