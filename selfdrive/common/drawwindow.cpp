#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <system/window.h>
#include <ui/GraphicBuffer.h>
#include <ui/Fence.h>
#include <utils/Log.h>
#include <utils/RefBase.h>
#include <gui/SurfaceControl.h>
#include <gui/SurfaceComposerClient.h>
#include <binder/IBinder.h>
#include <ui/DisplayInfo.h>
#include <gui/Surface.h>
using namespace android;
//Status value
status_t err;

sp<SurfaceComposerClient> mSurfaceComposerClient;

sp<SurfaceControl> mSurfaceControl;
//Screen width and height
int mWidth,mHeight;
//The parent class of GraphicBuffer, anativewindowbuffer
ANativeWindowBuffer *mNativeBuffer = nullptr;
//Connect SurfaceFlinger
void connectSurfaceFlinger();
//Get Surface
sp<ANativeWindow> getSurface();
//Connect to BufferQueue
void connectBufferQueue(ANativeWindow *surface);
//Set Transaction
void setBufferTransaction();
//Set up additional information
void setBuffer(ANativeWindow *surface);

int main() {
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
    //Connect SurfaceFlinger
    connectSurfaceFlinger();
    //Get surface
    ANativeWindow *surface = getSurface().get();
    //surface connection bufferqueue
    connectBufferQueue(surface);
    //Set Transaction
    setBufferTransaction();
    //Set up additional information
    setBuffer(surface);

    int fenceFD= -1;
    //Apply for buffer
    err = surface->dequeueBuffer(surface, &mNativeBuffer, &fenceFD);
    if (err != NO_ERROR) {
            ALOGE("dequeueBuffer err....");
        }
        //Confirm whether the requested buffer is completely used by the previous user through Fence
        sp<Fence> fence(new Fence(fenceFD));
        //Waiting to receive releaseFence
        int waitResult = fence->waitForever("dequeueBuffer_EmptyNative");
        if (waitResult != OK) {
            ALOGE("Fence wait err....");
        }
        //ANativeWindowBuffer to GraphicBuffer
        sp<GraphicBuffer> buff(GraphicBuffer::from(mNativeBuffer));
        //buffer data
        uint8_t *data = NULL;
        //Lock the buffer first through lock
        err = buff->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&data));
        if (err != NO_ERROR) {
            ALOGE("lock buffer err....");
        }

        //Fill in the data. If the value is 0, a black window will be drawn
        *data = 0;
        err = buff->unlock();
        if (err != NO_ERROR) {
            ALOGE("unlock buffer err....");
        }

        //Send the filled buffer to SurfaceFlinger for composite display
        err = surface->queueBuffer(surface, buff->getNativeBuffer(), -1);
        if (err != NO_ERROR) {
            ALOGE("queueBuffer buffer err....");
            return err;
        }
    mNativeBuffer = NULL;
    IPCThreadState::self()->joinThreadPool();
    return EXIT_SUCCESS;
}EGLDisplay

//1. Create SurfaceComposerClient, which is the Client side of SurfaceFlinger
void connectSurfaceFlinger(){
    
    mSurfaceComposerClient = new SurfaceComposerClient;
    err = mSurfaceComposerClient->initCheck();
    if (err != NO_ERROR) {
        ALOGE("SurfaceComposerClient initCheck err....");
        return;
    }
}
//2. Create Surface. Anativewindow is the parent class of Surface
sp<ANativeWindow> getSurface(){
    sp<IBinder> display = SurfaceComposerClient::getInternalDisplayToken();
    android::DisplayInfo mainDisplayInfo;
    //Get screen information of mobile phone
    err = SurfaceComposerClient::getDisplayInfo(display, &mainDisplayInfo);
    if (err != NO_ERROR) {
        ALOGE("getDisplayInfo err....");
    }
    //Screen width
    mWidth = mainDisplayInfo.w;
    //Screen height
    mHeight = mainDisplayInfo.h;
    //Create surface corresponding surface linker process layer
    mSurfaceControl = mSurfaceComposerClient->createSurface(
            String8("drawWindow"), mWidth/2, mHeight/2,
            PIXEL_FORMAT_RGBX_8888, ISurfaceComposerClient::eOpaque);
    if (mSurfaceControl == NULL || !mSurfaceControl->isValid()) {
        ALOGE("mSurfaceControl err....");
    }
    //Get surface
    sp<ANativeWindow> anw = mSurfaceControl->getSurface();
    return anw;
}
//3.Surface connection BufferQueue
void connectBufferQueue(ANativeWindow *surface){
    err = native_window_api_connect(surface, NATIVE_WINDOW_API_CPU);
    if (err != NO_ERROR) {
        ALOGE("connect bufferqueue err....");
    }
}
//4. Set buffer data
void setBufferTransaction(){
    /*
    setLayer(): Set the Z-order of the window
    setPosition(): Set the position of window display
    show(): The settings window is displayed
    apply(): Apply the set window information to the SurfaceFlinger for real effect
    */
    SurfaceComposerClient::Transaction{}
            .setLayer(mSurfaceControl, 0x7FFFFFFF)
            .setPosition(mSurfaceControl,mWidth/4,mHeight/4)
            .show(mSurfaceControl)
            .apply();
}
//5. Set buffer
void setBuffer(ANativeWindow *surface){
    //Set usage
    err = native_window_set_usage(surface, GRALLOC_USAGE_SW_WRITE_OFTEN);
    if (err != NO_ERROR) {
    ALOGE("native_window_set_usage err....");	
    }
    //Set transform
    err = native_window_set_buffers_transform(surface, NATIVE_WINDOW_TRANSFORM_ROT_90);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_buffers_transform err....");
    }
    //Setting scaling_mode
    err = native_window_set_scaling_mode(
            surface, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    if (err != NO_ERROR) {
            ALOGE("native_window_set_scaling_mode err....");
    }
}
