/**
 * Android Native Window App Wrapper
 * Test file
 * 
 * Execute and Black Screen should appear.
 * https://www.jianshu.com/p/81e9c814f10a
 * 
 */

#include "common/window_wrapper.h"

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <ui/DisplayInfo.h>
#include <system/window.h>

int drawNativeWindow(sp<WindowSurfaceWrapper> windowSurface) {
    status_t err = NO_ERROR;
    ANativeWindowBuffer *aNativeBuffer = nullptr;
    sp<SurfaceControl> surfaceControl = windowSurface->getSurfaceControl();
    ANativeWindow* aNativeWindow = surfaceControl->getSurface().get();

    // 1. We need to reconnect to the ANativeWindow as a CPU client to ensure that no frames 
    //  get dropped by SurfaceFlinger assuming that these are other frames.
    err = native_window_api_disconnect(aNativeWindow, NATIVE_WINDOW_API_CPU);
    if (err != NO_ERROR) {
        ALOGE("ERROR: unable to native_window_api_disconnect ignore...\n");
    }

    // 2. connect the ANativeWindow as a CPU client
    err = native_window_api_connect(aNativeWindow, NATIVE_WINDOW_API_CPU);
    if (err != NO_ERROR) {
        ALOGE("ERROR: unable to native_window_api_connect\n");
        return EXIT_FAILURE;
    }

    // 3. set the ANativeWindow dimensions
    err = native_window_set_buffers_user_dimensions(aNativeWindow, windowSurface->width(), windowSurface->height());
    if (err != NO_ERROR) {
        ALOGE("ERROR: unable to native_window_set_buffers_user_dimensions\n");
        return EXIT_FAILURE;
    }

    // 4. set the ANativeWindow format
    int format = PIXEL_FORMAT_RGBX_8888;
    err = native_window_set_buffers_format(aNativeWindow,format );
    if (err != NO_ERROR) {
        ALOGE("ERROR: unable to native_window_set_buffers_format\n");
        return EXIT_FAILURE;
    }

    // 5. set the ANativeWindow transform
    int rotation = 0;
    int transform = 0;
    if ((rotation % 90) == 0) {
        switch ((rotation / 90) & 3) {
            case 1:  transform = HAL_TRANSFORM_ROT_90;  break;
            case 2:  transform = HAL_TRANSFORM_ROT_180; break;
            case 3:  transform = HAL_TRANSFORM_ROT_270; break;
            default: transform = 0;                     break;
        }
    }
    err = native_window_set_buffers_transform(aNativeWindow, transform);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_buffers_transform failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    // 6. handle the ANativeWindow usage
    int consumerUsage = 0;
    err = aNativeWindow->query(aNativeWindow, NATIVE_WINDOW_CONSUMER_USAGE_BITS, &consumerUsage);
    if (err != NO_ERROR) {
        ALOGE("failed to get consumer usage bits. ignoring");
        err = NO_ERROR;
    }

    // Make sure to check whether either  requested protected buffers.
    int usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
    if (usage & GRALLOC_USAGE_PROTECTED) {
        // Check if the ANativeWindow sends images directly to SurfaceFlinger.
        int queuesToNativeWindow = 0;
        err = aNativeWindow->query(
                aNativeWindow, NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER, &queuesToNativeWindow);
        if (err != NO_ERROR) {
            ALOGE("error authenticating native window: %s (%d)", strerror(-err), -err);
            return err;
        }

        // Check if the consumer end of the ANativeWindow can handle protected content.
        int isConsumerProtected = 0;
        err = aNativeWindow->query(
                aNativeWindow, NATIVE_WINDOW_CONSUMER_IS_PROTECTED, &isConsumerProtected);
        if (err != NO_ERROR) {
            ALOGE("error query native window: %s (%d)", strerror(-err), -err);
            return err;
        }

        // Deny queuing into native window if neither condition is satisfied.
        if (queuesToNativeWindow != 1 && isConsumerProtected != 1) {
            ALOGE("native window cannot handle protected buffers: the consumer should either be "
                  "a hardware composer or support hardware protection");
            return PERMISSION_DENIED;
        }
    }

    // 7. set the ANativeWindow usage
    int finalUsage = usage | consumerUsage;
    ALOGE("gralloc usage: %#x(producer) + %#x(consumer) = %#x", usage, consumerUsage, finalUsage);
    err = native_window_set_usage(aNativeWindow, finalUsage);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_usage failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    // 8. set the ANativeWindow scale mode
    err = native_window_set_scaling_mode(
            aNativeWindow, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_scaling_mode failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    ALOGE("set up nativeWindow %p for %dx%d, color %#x, rotation %d, usage %#x",
            aNativeWindow, windowSurface->width(), windowSurface->height(), format, rotation, finalUsage);

    // 9. set the ANativeWindow permission to allocte new buffer, default is true
    static_cast<Surface*>(aNativeWindow)->getIGraphicBufferProducer()->allowAllocation(true);

    // 10. set the ANativeWindow buffer count
    int numBufs = 0;
    int minUndequeuedBufs = 0;

    err = aNativeWindow->query(aNativeWindow,
            NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &minUndequeuedBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: MIN_UNDEQUEUED_BUFFERS query "
                "failed: %s (%d)", strerror(-err), -err);
        goto handle_error;
    }

    numBufs = minUndequeuedBufs + 1;
    err = native_window_set_buffer_count(aNativeWindow, numBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_buffer_count failed: %s (%d)", strerror(-err), -err);
        goto handle_error;
    }

    // 11. draw the ANativeWindow
    for (int i = 0; i < numBufs + 1; i++) {
        // 12. dequeue a buffer
        int hwcFD= -1;
        err = aNativeWindow->dequeueBuffer(aNativeWindow, &aNativeBuffer, &hwcFD);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: dequeueBuffer failed: %s (%d)",
                    strerror(-err), -err);
            break;
        }

        // 13. make sure really control the dequeued buffer
        sp<Fence> hwcFence(new Fence(hwcFD));
        int waitResult = hwcFence->waitForever("dequeueBuffer_EmptyNative");
        if (waitResult != OK) {
            ALOGE("dequeueBuffer_EmptyNative: Fence::wait returned an error: %d", waitResult);
            break;
        }

        sp<GraphicBuffer> buf(GraphicBuffer::from(aNativeBuffer));

        // 14. Fill the buffer with black
        uint8_t *img = NULL;
        err = buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: lock failed: %s (%d)", strerror(-err), -err);
            break;
        }

        //15. Draw the window, here we fill the window with black.
        *img = 0;

        err = buf->unlock();
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: unlock failed: %s (%d)", strerror(-err), -err);
            break;
        }

        // 16. queue the buffer to display
        int gpuFD = -1;
        err = aNativeWindow->queueBuffer(aNativeWindow, buf->getNativeBuffer(), gpuFD);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: queueBuffer failed: %s (%d)", strerror(-err), -err);
            break;
        }

        aNativeBuffer = NULL;
    }

handle_error:
    // 17. cancel buffer
    if (aNativeBuffer != NULL) {
        aNativeWindow->cancelBuffer(aNativeWindow, aNativeBuffer, -1);
        aNativeBuffer = NULL;
    }

    // 18. Clean up after success or error.
    status_t err2 = native_window_api_disconnect(aNativeWindow, NATIVE_WINDOW_API_CPU);
    if (err2 != NO_ERROR) {
        ALOGE("error pushing blank frames: api_disconnect failed: %s (%d)", strerror(-err2), -err2);
        if (err == NO_ERROR) {
            err = err2;
        }
    }
    return err;
}


int main(int argc, char *argv[]) {
    unsigned samples = 0;
    printf("usage: %s [samples]\n", argv[0]);
    if (argc == 2) {
        samples = atoi( argv[1] );
        printf("Multisample enabled: GL_SAMPLES = %u\n", samples);
    }
 
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
 
    sp<WindowSurfaceWrapper> windowSurface(new WindowSurfaceWrapper(String8("NativeBinApp"), 0x7FFFFFFF));
 
    drawNativeWindow(windowSurface->getSurface().get(), windowSurface->width(), windowSurface->height());
 
    IPCThreadState::self()->joinThreadPool();

    return EXIT_SUCCESS;
}
