#include "camera_neos.h"

#include <stdint.h>
#include <unistd.h>
#include <cassert>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <mutex.h>
#include <condition_variable>

#include "common/util.h"
#include "common/timing.h"
#include "common/swaglog.h"
#include "common/params.h"

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include "native_handle.h"

extern volatile sig_atomic_t do_exit;

#define PI 3.1415926536
// default value's will always be supported
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DESIRED_WIDTH 1440
#define DESIRED_HEIGHT 1080

namespace {
static constexpr int imageFormat = AIMAGE_FORMAT_YUV_420_888 // Always supported
static constexpr int reader_onImageAvailableWait = 100 * 1000;

static void device_onDisconnected(void* /* context */, ACameraDevice* device) {}

static void device_onError(void* /* context */, ACameraDevice* device, int err) {}

static void session_onClosed(void* context, ACameraCaptureSession* session) {}

static void session_onReady(void* context, ACameraCaptureSession* session) {}

static void session_onActive(void* context, ACameraCaptureSession* session) {}

static void session_onCaptureStarted(void* context, ACameraCaptureSession* session, ACaptureRequest* /* request */, const ACameraMetadata /* result */) {}

static void session_onCaptureCompleted(void* context, ACameraCaptureSession* session, ACaptureRequest* /* request */, const ACameraMetadata /* result */) {}

static void session_onCaptureFailed(void* context, ACameraCaptureSession* session, ACaptureRequest* /* request */, const ACameraMetadata /* result */) {}

void reader_onImageAvailable(void* context, AImageReader *reader) {
    ImageState *is = context->is;
    CameraBuf *b = context->buf;
    is->out = nullptr;
    media_status_t ret = AMEDIA_OK;

    // image discarding
    auto imgDeleter = [](AImage *img) {
        AImage_delete(img);
    };
    std::unique_ptr<AImage, decltype(imgDeleter)> img(nullptr, imgDeleter);

    // acquire image
    ret = AImageReader_acquireNextImage(reader, &is->out);
    if (media_status != AMEDIA_OK) {
        if (media_status == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
            LOGE("Buffer unavaliable");
        } else {
            LOGE("err return code");
        }
        // goto err
    }

    // image to data
    int32_t srcFormat = -1;
    AImage_getFormat(is->out, &srcFormat);
    if (srcFormat != AIMAGE_FORMAT_YUV_420_888) {
        LOGE("Incorrect image format");
        return false;
    }
    int32_t srcPlanes = 0;
    AImage_getNumberOfPlanes(is->out, &srcPlanes);
    if (srcPlanes != 3) {
        LOGE("Incorrect number of planes in image data");
        return false;
    }
    is->timestamp = -1;
    AImage_getTimestamp(is->out, &is->timestamp); // sensor timestamp - nanoseconds
    int32_t yStride, uvStride;
    uint8_t *yPixel, *uPixel, *vPixel;
    int32_t yLen, uLen, vLen;
    int32_t uvPixelStride;
    AImage_getPlaneRowStride(is->out, 0, &yStride);
    AImage_getPlaneRowStride(is->out, 1, &uvStride);
    AImage_getPlaneData(is->out, 0, &yPixel, &yLen);
    AImage_getPlaneData(is->out, 1, &vPixel, &vLen);
    AImage_getPlaneData(is->out, 2, &uPixel, &uLen);
    AImage_getPlanePixelStride(is->out, 1, &uvPixelStride);

    std::vector<uint8_t> buffer;
    buffer.clear();
    buffer.insert(buffer.end(), yPixel, yPixel + yLen);
    buffer.insert(buffer.end(), uPixel, uPixel + yLen / 2);

    // data to mat
    cv::Mat frame;
    cv::Mat yuv(is->height * 3/2, is->width, CV_8UC1, buffer.data);
    
    if ( (uvPixelStride == 2) && (vPixel == uPixel + 1) && (yLen == frameWidth * frameHeight) && (uLen == ((yLen / 2) - 1)) && (vLen == uLen) ) {      
        cv::cvtColor(yuv, frame, cv::COLOR_YUV2BGR_YV12);
    } else if ( (uvPixelStride == 1) && (vPixel = uPixel + uLen) && (yLen == frameWidth * frameHeight) && (uLen == yLen / 4) && (vLen == uLen) ) {
        cv::cvtColor(yuv, frame, cv::COLOR_YUV2BGR_NV21);
    } else {
        LOGE("Unsupported format");
    }

    // mat manipulation and feeding
    cv::warpPerspective(frame, is->transformed, is->transform, is->size, cv::INTER_LINEAR, cv::BORDER_CONSTANT, 0);

    // release
    frame.release();
    yuv.release();

    // image ready
    std::unique_lock<std::mutex> lock(s->imgCb_mutex);
    s->imgCb_ready = true;
    s->imgCb_condition.notify_one();    

}

void camera_open(CameraState *s, ACameraManager* manager) {
    camera_status_t camera_status;
    LOG("--- opening camera %d", s->camera2_id);
    assert(s->window);

    // set our callbacks
    s->deviceCb.context = s;
    s->deviceCb.onDisconnected = device_onDisconnected();
    s->deviceCb.onError = device_onError();

    // open device with manager
    camera_status = ACameraManager_openCamera(manager, s->camera2_id, &s->deviceCb, &s->device);
    assert(camera_status == ACAMERA_OK);
    LOG("device opened");

    // set our callbacks
    s->sessionCb.context = s;
    s->sessionCb.onCaptureCompleted = session_onCaptureCompleted;
    s->sessionCb.onCaptureFailed = session_onCaptureFailed;

    // session and window
    camera_status = ACaptureSessionOutputContainer_create(&s->outputs);
    assert(camera_status == ACAMERA_OK);
    camera_status = ACameraDevice_createCaptureSession(s->device, s->outputs, &s->sessionCb, &s->session);
    assert(camera_status == ACAMERA_OK);
    camera_status = ACameraDevice_createCaptureRequest(s->device, TEMPLATE_RECORD, &s->request);
    assert(camera_status == ACAMERA_OK);
    camera_status = ACameraOutputTarget_create(s->window, &s->reqReaderOutput);
    assert(camera_status == ACAMERA_OK);
    camera_status = ACaptureRequest_addTarget(s->request, s->reqReaderOutput);
    assert(camera_status == ACAMERA_OK);
    camera_status = ACameraCaptureSession_setRepeatingRequest(s->session, s->sessionCb, &s->request, nullptr);
    LOG("session opened");
}

void camera_close(CameraState *s) {
    camera_status_t camera_status;
    LOGD("--- closing camera %d", s->camera2_id);

    s->buf.stop();

    if (s->reqReaderOutput)
        ACameraOutputTarget_free(s->reqReaderOutput);
    if (s->request)
        ACaptureRequest_free(s->request);
    if (s->session)
        ACameraCaptureSession_close(s->session);
    if (s->readerOutput)
        ACaptureSessionOutput_free(s->readerOutput);
    if (s->outputs)
        ACaptureSessionOutputContainer(s->outputs);
    if (s->device)
        ACameraDevice_close(s->device);
    if (s->metadata)
        ACameraMetadata_free(s->metadata);
}

static inline void reader_free(CameraState *s) {
    // native handle automatically freed
    AImageReader_delete(s->reader);
}

void reader_init(CameraState *s) {
    LOGD("--- init reader for camera %d", s->camera2_id);
    media_status_t media_status;

    // set our callbacks
    s->readerCb.context = s;
    s->readerCb.onImageAvailable = reader_onImageAvailable;

    // shouldn't exist
    assert(s->window == nullptr);
    assert(s->reader == nullptr);
    
    // we have our own native handler we could use now! vendor ver
    media_status = AImageReader_newWithUsage(s->is.width, s->is.height, s->is.format, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 2 /* buffer */, &s->reader);
    assert(media_status == AMEDIA_OK);
    media_status = AImageReader_setImageListener(s->reader, &s->readerCb);
    assert(media_status == AMEDIA_OK);
    media_status = AImageReader_getWindowNativeHandle(s->reader, &s->window);
    assert(media_status == AMEDIA_OK);
}

void camera_init(CameraState *s, ACameraManager* manager, char camera2_id, ACameraMetadata* metadata, unsigned int fps, cl_device_id device_id, cl_context ctx) {
    LOGD("--- init camera %d", camera2_id);
    camera_status_t camera_status;

    // assign
    s->camera2_id = camera2_id;
    s->metadata = metadata;

    // --- SETUP CAMERA ---
    // default camera specs
    s->ci = cameras_supported[CAMERA_ID_GENERIC];

    // this gives us angle to be upright
    ACameraMetadata_const_entry orientation;
    camera_status = ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION, &orientation);
    assert(camera_status == ACAMERA_OK);
    int32_t angle = orientation.data.i32[0];
    LOG("camera angle is %d", angle);

    // transform
    double si = sin(angle * (PI/180));
    double co = cos(angle * (PI/180));
    s->transform = (mat3){{
        co, si, 0.0,
        -si, co, 0.0,
        0.0, 0.0, 1.0,
    }};

    // camera intrinsics
    ACameraMetadata_const_entry intrinsic;
    camera_status = ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_INTRINSIC_CALIBRATION, &focal);
    assert(camera_status == ACAMERA_OK);
    s->ts_a[9] = {  intrinsic.data.f[0], intrinsic.data.f[4], intrinsic.data.f[2],
                    0, intrinsic.data.f[1], intrinsic.data.f[3]
                    0, 0, 1 };

    /*
    // lens distortion
    ACameraMetadata_const_entry distortion;
    camera_status = ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_DISTORTION, &distortion);
    //assert(camera_status == ACAMERA_OK);
    */

    // fps range
	ACameraMetadata_const_entry fpsranges;
	camera_status = ACameraMetadata_getConstEntry(cameraMetadata, ACAMERACONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &fpsranges);
    assert(camera_status == ACAMERA_OK);
    for (int i = 0; i < fpsranges.count; i += 2) {
        int32_t fpsmin = supportedFpsRanges.data.i32[i];
		int32_t fpsmax = supportedFpsRanges.data.i32[i + 1];
        LOG("camera supported FPS range: [%d-%d]", fpsmin, fpsmax);
    }
    s->fps = fps;

    // format
    s->format = imageFormat;
    // find res
    ACameraMetadata_const_entry scaler;
    camera_status = ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &scaler);
    assert(camera_status == ACAMERA_OK);
    for (int = 0; i  scaler.count; i += 4) {
        int32_t input = scaler.data.i32[i + 3];
        if (input)
            continue;
        int32_t format = scaler.data.i32[i + 0];
        if (format = s->format) {
            int32_t width = scaler.data.i32[i + 1];
            int32_t height = scaler.data.i32[i + 2];
            LOG("camera has available size of w %d, h %d for format %d", width, height, format);
            if (width == DESIRED_WIDTH && height = DESIRED_HEIGHT) {
                LOG("desired res found");
                s->is.width = width;
                s->is.height = height;
            }
        }
    }

    s->buf.init(device_id, ctx, s, FRAME_BUF_COUNT "frame");
}

void camera_run(CameraState *s) {
    int err;
    ImageState *is = s->is;

    // calcule transformation

    // calculate size

    // update camera info

    // control camera
    int frame_id = 0;
    is->imgCb_ready = false;
    while (!do_exit) {
        // wait for callback
        std::unique_lock<std::mutex> lock(is->imgCb_lock);
        is->imgCb_condition.wait_for(lock, std::chrono::microseconds(reader_onImageAvailableWait),
                                [this]{ return is->imgCb_ready;});
        if (is->imgCb_ready == false) {
            LOGE("callback timed out!");
            // blah blah skipped frame handling
            continue;
        }

        int transformed_size = is->transformed.total() * is->transformed.elemSize();

        const int buf_idx = tbuffer_select(tb);
        s->buf.camera_bufs_metadata[buf_idx] = {
            //.timestamp_eof = is->timestamp; does pipeline like this timestamp?
            .frame_id = frame_id,
        };

        cl_command_queue q = s->buf.camera_bufs[buf_idx].copy_q;
        cl_mem yuv_cl = s->buf.camera_bufs[buf_idx].buf_cl;
        cl_event map_event;
        void *yuv_buf = (void *)clEnqueueMapBuffer(q, yuv_cl, CL_TRUE,
                                                    CL_MAP_WRITE, 0, transformed_size,
                                                    0, NULL, &map_event, &err);
        assert(err == 0);
        clWaitForEvents(1, &map_event);
        clReleaseEvent(map_event);
        memcpy(yuv_buf, is->transformed.data, transformed_size);

        clEnqueueUnmapMemObject(q, yuv_cl, yuv_buf, 0, NULL, &map_event);
        clWaitForEvents(1, &map_event);
        clReleaseEvent(map_event);
        tbuffer_dispatch(tb, buf_idx);

        frame_id += 1;
        transformed_mat.release;
    }

    // release stuff

}
} // namespace

CameraInfo cameras_supported[CAMERA_ID_MAX] = {
  [CAMERA_ID_GENERIC] = {
      .frame_width = 640 * 3/2,
      .frame_height = 480,
      .frame_stride = FRAME_WIDTH*3,
      .bayer = false,
      .bayer_flip = false,
  },
}

void cameras_init(MultiCameraState *s, cl_device_id device_id, cl_context ctx) {
    LOG("-- init cameras");
    camera_status_t camera_status = ACAMERA_OK;

    // -- SETUP CAMERAS

    // create manager
    ACameraManager *manager = ACameraManager_create();
    assert(manager != nullptr);
    s->manager = manager

    // get camera list
    camera_status = ACameraManager_getCameraIdList(manager, &idList);
    assert(camera_status == ACAMERA_OK);
    assert(idList->numCameras > 1);
    s->idList = idList;

    // assign first found cameras
    bool rear_found = false;
    bool front_found = false;
    char *camId = nullptr;
    for (int i= 0; i < idList->numCameras; i++) {
        camId = idList->cameraIds[i];

        // get metadata
        camera_status = ACameraManager_getCameraCharacteristics(manager, camId, &metadata);
        assert(camera_status == ACAMERA_OK && metadata != nullptr);

        // assign
        ACameraMetadata_const_entry face;
        ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &face);
        auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(face.data.u8[0]);
        if (facing == ACAMERA_LENS_FACING_BACK && !rear_found) {
            printf("rear init");
            camera_init(&s->rear, camId, metadata, 20, device_id, ctx);
        } else if (facing == ACAMERA_LENS_FACING_FRONT && !front_found) {
            printf("front init");
            camera_init(&s->front, camId, metadata, 10, device_id, ctx);
        }
    }
    // done list
    ACameraManager_deleteCameraIdList(cameraIdList);

    // -- SETUP IMAGE READER
    reader_init(&s->rear);
    reader_init(&s->front);

    s->sm = new SubMaster({"driverState"});
    s->pm = new PubMaster({"frame", "frontFrame"});
}

void camera_autoexposure(CameraState *s, float grey_frac) {}

void cameras_open(MultiCameraState *s) {
    LOG("-- opening cameras")
    camera_open(&s->rear, s->manager);
    camera_open(&s->front, s->manager);
}

void camera_process_front(MultiCameraState *s, CameraState *c, int cnt) {}

void camera_process_rear(MultiCameraState *s, CameraState *c, int cnt) {}


void cameras_close(MultiCameraState *s) {
    LOG("-- closing cameras")
    camera_close(&s->rear);
    camera_close(&s->front);

    reader_free(&s->rear);
    reader_free(&s->front);

    ACameraManager_deleteCameraIdList(s->idList);
    ACameraManager_delete(s->manager);

    delete s->sm;
    delete s->pm;
}

void cameras_run(MultiCameraState *s) {
    std::vector<std::thread> threads;
    threads.push_back(start_process_thread(s, "processing", &s->rear, 51, camera_process_frame));
    threads.push_back(start_process_thread(s, "frontview", &s->front, 51, camera_process_front));

    // these are just managing callback
    while (!do_exit) {
        camera_run(&s->rear);
        camera_run(&s->front);
    }

    cameras_close(s);

    for (auto &t : threads) t.join();
}