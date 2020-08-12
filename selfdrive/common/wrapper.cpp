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

WindowSurfaceWrapper::WindowSurfaceWrapper(const String8& name) {
    mName = name;
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

    uint32_t width, height;
    if (mainDisplayInfo.orientation != DISPLAY_ORIENTATION_0 &&
            mainDisplayInfo.orientation != DISPLAY_ORIENTATION_180) {
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
            .setLayer(surfaceControl, 0x7FFFFFFF)
            .show(surfaceControl)
            .apply();

    mSurfaceControl = surfaceControl;
    mWidth = width;
    mHeight = height;

}

sp<ANativeWindow> WindowSurfaceWrapper::getSurface() const {
    sp<ANativeWindow> anw = mSurfaceControl->getSurface();
    return anw;
}

int drawNativeWindow(sp<WindowSurfaceWrapper> /* windowSurface */) {
    return NO_ERROR;
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

    sp<WindowSurfaceWrapper> windowSurface(new WindowSurfaceWrapper(String8("NativeBinApp")));

    drawNativeWindow(windowSurface->getSurface().get(), windowSurface->width(), windowSurface->height());

    IPCThreadState::self()->joinThreadPool();

    return EXIT_SUCCESS;
}