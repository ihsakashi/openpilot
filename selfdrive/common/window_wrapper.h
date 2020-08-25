/**
 * Android Window Surface Wrapper
 * 
 */

#include <gui/SurfaceControl.h>

class WindowSurfaceWrapper : public RefBase {
public:
    // Creates the window.
    WindowSurfaceWrapper(const String8& name);
 
    virtual ~WindowSurfaceWrapper() {}
 
    virtual void onFirstRef();

    void swapLayer();

    void hide();

    void show();
 
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