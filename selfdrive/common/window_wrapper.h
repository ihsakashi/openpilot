
#include <gui/SurfaceControl.h>

namespace android {

class WindowSurfaceWrapper : public RefBase {
public:
    // Creates the window.
    WindowSurfaceWrapper(const String8& name, int32_t layer);

    virtual ~WindowSurfaceWrapper() {}

    virtual void onFirstRef();

    // Retrieves a handle to the window.
    sp<ANativeWindow>  getSurface() const;

    int width() {
        return mWidth;
    }

    int height() {
        return mHeight;
    }

    int layer() {
        return mLayer;
    }

private:
    WindowSurfaceWrapper(const WindowSurfaceWrapper&);
    WindowSurfaceWrapper& operator=(const WindowSurfaceWrapper&);

    sp<SurfaceControl> mSurfaceControl;

    int mWidth;
    int mHeight;
    String8 mName;
    int32_t mLayer;
};

} // namespace android
