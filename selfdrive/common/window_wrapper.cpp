/**
 * Android Window Surface Wrapper
 * 
 * https://www.jianshu.com/p/8e7a9a0b5726
 * 
 */

#include <window_wrapper.h>

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <ui/DisplayInfo.h>


WindowSurfaceWrapper::WindowSurfaceWrapper(const String8& name, int32_t layer) {
    mName = name;
    mLayer = layer;
}
 
void WindowSurfaceWrapper::onFirstRef() {
    status_t err;
 
    sp<SurfaceComposerClient> surfaceComposerClient = new SurfaceComposerClient;
    err = surfaceComposerClient->initCheck();
    if (err != NO_ERROR) {
        fprintf(stderr, "SurfaceComposerClient::initCheck error: %#x\n", err);
        return;
    }
 
    // Get main display parameters.
    sp<IBinder> mainDisplay = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);
    DisplayInfo mainDisplayInfo;
    err = SurfaceComposerClient::getDisplayInfo(mainDisplay, &mainDisplayInfo);
    if (err != NO_ERROR) {
        fprintf(stderr, "ERROR: unable to get display characteristics\n");
        return;
    }
 
    // landscape
    uint32_t width, height;
    if (mainDisplayInfo.orientation == DISPLAY_ORIENTATION_0 &&
            mainDisplayInfo.orientation == DISPLAY_ORIENTATION_180) {
        // rotated
        width = mainDisplayInfo.h;
        height = mainDisplayInfo.w;
    } else {
        width = mainDisplayInfo.w;
        height = mainDisplayInfo.h;
    }
 
    sp<SurfaceControl> surfaceControl = surfaceComposerClient->createSurface(
            mName, width, height,
            PIXEL_FORMAT_RGBX_8888, ISurfaceComposerClient::eOpaque);
    if (surfaceControl == NULL || !surfaceControl->isValid()) {
        fprintf(stderr, "Failed to create SurfaceControl\n");
        return;
    }
 
    SurfaceComposerClient::Transaction{}
            .setLayer(surfaceControl, mLayer)
            .show(surfaceControl)
            .apply();
 
    mSurfaceControl = surfaceControl;
    mWidth = width;
    mHeight = height;
 
}

void WindowSurfaceWrapper::swapLayer(int32_t layer) {
    SurfaceComposerClient::Transaction{}
            .setLayer(surfaceControl, layer)
            .apply();
}

void WindowSurfaceWrapper::hide() {
    SurfaceComposerClient::Transaction{}
            .hide()
            .apply();
}

void WindowSurfaceWrapper::show() {
    SurfaceComposerClient::Transaction{}
            .show()
            .apply();
}

sp<ANativeWindow> WindowSurfaceWrapper::getSurface() const {
    sp<ANativeWindow> anw = mSurfaceControl->getSurface();
    return anw;
}