/**
 * Android Window Surface Wrapper
 * 
 */

#include <gui/SurfaceControl.h>
#include <gui/Surface.h>

namespace android {
class WindowSurfaceWrapper : public RefBase {
public:
    // Creates the window.
    WindowSurfaceWrapper(const String8& name, int32_t layer);
 
    virtual ~WindowSurfaceWrapper() {}
 
    virtual void onFirstRef();

    void swapLayer(int32_t layer);

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
    int mLayer;
    String8 mName;
};

} // namespace android