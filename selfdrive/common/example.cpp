/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

extern "C" {
#include <omap/omap_drm.h>
#include <omap/omap_drmif.h>
#include <xf86drm.h>
}

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hwcomposer/hal_public.h>

#include <utils/Timers.h>

#include <WindowSurface.h>
#include <ui/GraphicBuffer.h>
#include <EGLUtils.h>

using namespace android;

static void printGLString(const char *name, GLenum s) {
    // fprintf(stderr, "printGLString %s, %d\n", name, s);
    const char *v = (const char *) glGetString(s);
    // int error = glGetError();
    // fprintf(stderr, "glGetError() = %d, result of glGetString = %x\n", error,
    //        (unsigned int) v);
    // if ((v < (const char*) 0) || (v > (const char*) 0x10000))
    //    fprintf(stderr, "GL %s = %s\n", name, v);
    // else
    //    fprintf(stderr, "GL %s = (null) 0x%08x\n", name, (unsigned int) v);
    fprintf(stderr, "GL %s = %s\n", name, v);
}

static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) {
    if (returnVal != EGL_TRUE) {
        fprintf(stderr, "%s() returned %d\n", op, returnVal);
    }

    for (EGLint error = eglGetError(); error != EGL_SUCCESS; error
            = eglGetError()) {
        fprintf(stderr, "after %s() eglError %s (0x%x)\n", op, EGLUtils::strerror(error),
                error);
    }
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
    }
}

static const char gVertexShader[] = "attribute vec4 vPosition;\n"
    "varying vec2 yuvTexCoords;\n"
    "void main() {\n"
    "  yuvTexCoords = vPosition.xy + vec2(0.5, 0.5);\n"
    "  gl_Position = vPosition;\n"
    "}\n";

static const char gFragmentShader[] = "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES yuvTexSampler;\n"
    "varying vec2 yuvTexCoords;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(yuvTexSampler, yuvTexCoords);\n"
    "}\n";

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stdout, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            } else {
                fprintf(stdout, "Guessing at GL_INFO_LOG_LENGTH size\n");
                char* buf = (char*) malloc(0x1000);
                if (buf) {
                    glGetShaderInfoLog(shader, 0x1000, NULL, buf);
                    fprintf(stdout, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }
    printf("Loaded vertex shader\n");

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    printf("Loaded pixel shaders\n");

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

GLuint gProgram;
GLint gvPositionHandle;
GLint gYuvTexSamplerHandle;

bool setupGraphics(int w, int h) {
    printf("Creating glProgram\n");
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        return false;
    }
    printf("createProgram complete\n");
    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    checkGlError("glGetAttribLocation");
    fprintf(stdout, "glGetAttribLocation(\"vPosition\") = %d\n",
            gvPositionHandle);
    gYuvTexSamplerHandle = glGetUniformLocation(gProgram, "yuvTexSampler");
    checkGlError("glGetUniformLocation");
    fprintf(stdout, "glGetUniformLocation(\"yuvTexSampler\") = %d\n",
            gYuvTexSamplerHandle);

    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    return true;
}

int align(int x, int a) {
    return (x + (a-1)) & (~(a-1));
}

const int yuvTexWidth = 608;
const int yuvTexHeight = 480;
const int yuvTexUsage = GraphicBuffer::USAGE_HW_TEXTURE |
        GraphicBuffer::USAGE_SW_WRITE_RARELY;
const int yuvTexFormat = HAL_PIXEL_FORMAT_YV12;
const int yuvTexOffsetY = 0;
const bool yuvTexSameUV = false;
static sp<GraphicBuffer> yuvTexBuffer;
static GLuint yuvTex;
static ANativeWindowBuffer anwb;
const gralloc_module_t *grModule = NULL;
const alloc_device_t *allocDevice = NULL;

static int openGraphicsHAL()
{
    int err = 0;
    gralloc_module_t const *module;

    if (grModule)
        return 0;

    err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
            (const hw_module_t **)&module);
    if (err) {
        fprintf(stderr, "%s: Failed to open Android Graphics HAL (err=%d)\n",
               __func__, err);
        goto err_out;
    }

    err = module->common.methods->open((const hw_module_t *)module,
            GRALLOC_HARDWARE_GPU0, (hw_device_t **)&allocDevice);
    if (err) {
        fprintf(stderr, "%s: Failed to open Android Grapichs HAL device (err=%d)\n",
                __func__, err);
        goto err_out;
    }

    grModule = module;

err_out:
    return err;
}

static void incRefNop(struct android_native_base_t __unused *base) {}
static void decRefNop(struct android_native_base_t __unused *base) {}

bool setupYuvTexSurface(EGLDisplay dpy, EGLContext context) {
    int blockWidth = yuvTexWidth > 16 ? yuvTexWidth / 16 : 1;
    int blockHeight = yuvTexHeight > 16 ? yuvTexHeight / 16 : 1;

    /* Allocate a omap bo and wrap it to gralloc using registerBuffer
     * interface
     */
    struct omap_device *dev;
    struct omap_bo *bo;
    int fd = -1;
    int dmabuf;
    uint32_t size, flags = 0;

    fd = open("/dev/dri/renderD128", O_RDWR, 0);
    if (fd <= 0) {
        fprintf(stderr, "%s: Failed to open omapdrm device\n", __func__);
        return false;
    }

    dev = omap_device_new(fd);
    if (dev == NULL) {
        fprintf(stderr, "%s: Failed to get omap device\n", __func__);
        return false;
    }

    size = align(yuvTexWidth, 16) * yuvTexHeight * 1.5;
    size = align(size, 4096);
    flags = OMAP_BO_WC;

    bo = omap_bo_new(dev, size, flags);
    if (!bo) {
        fprintf(stderr, "%s: Failed to create omap bo\n", __func__);
        return false;
    }
    printf("Created omap_bo\n");

    char *buf = (char *)omap_bo_map(bo);
    int yuvTexStrideY = align(yuvTexWidth, 16);
    int yuvTexOffsetV = yuvTexStrideY * yuvTexHeight;
    int yuvTexStrideV = (yuvTexStrideY/2 + 0xf) & ~0xf;
    int yuvTexOffsetU = yuvTexOffsetV + yuvTexStrideV * yuvTexHeight/2;
    int yuvTexStrideU = yuvTexStrideV;
    for (int x = 0; x < yuvTexWidth; x++) {
        for (int y = 0; y < yuvTexHeight; y++) {
            int parityX = (x / blockWidth) & 1;
            int parityY = (y / blockHeight) & 1;
            unsigned char intensity = (parityX ^ parityY) ? 63 : 191;
            buf[yuvTexOffsetY + (y * yuvTexStrideY) + x] = intensity;
            if (x < yuvTexWidth / 2 && y < yuvTexHeight / 2) {
                buf[yuvTexOffsetU + (y * yuvTexStrideU) + x] = intensity;
                if (yuvTexSameUV) {
                    buf[yuvTexOffsetV + (y * yuvTexStrideV) + x] = intensity;
                } else if (x < yuvTexWidth / 4 && y < yuvTexHeight / 4) {
                    buf[yuvTexOffsetV + (y*2 * yuvTexStrideV) + x*2 + 0] =
                    buf[yuvTexOffsetV + (y*2 * yuvTexStrideV) + x*2 + 1] =
                    buf[yuvTexOffsetV + ((y*2+1) * yuvTexStrideV) + x*2 + 0] =
                    buf[yuvTexOffsetV + ((y*2+1) * yuvTexStrideV) + x*2 + 1] = intensity;
                }
            }
        }
    }
    printf("Wrote sample data to buffer\n");

    dmabuf = omap_bo_dmabuf(bo);
    omap_bo_del(bo);
    if (dmabuf <= 0) {
        fprintf(stderr, "%s: Failed to get dmabuf fd for bo\n", __func__);
        return false;
    }
    printf("Got dmabuf fd %d from bo\n", dmabuf);

    /* create a native_handle and wrap it in ANativeWindowBuffer */
    IMG_native_handle_t *imgHandle = (IMG_native_handle_t *)native_handle_create(
            1, IMG_NATIVE_HANDLE_NUMFDS - 1 + IMG_NATIVE_HANDLE_NUMINTS);
    if (!imgHandle) {
        fprintf(stderr, "%s: Failed to create IMG_native_handle_t\n", __func__);
        return false;
    }
    imgHandle->fd[0] = dmabuf;
    imgHandle->usage = yuvTexUsage;
    imgHandle->iWidth = yuvTexWidth;
    imgHandle->iHeight = yuvTexHeight;
    imgHandle->iFormat = yuvTexFormat;
    imgHandle->uiBpp = 12;
    imgHandle->uiFlags = 0;
    imgHandle->ui64Stamp = 0ULL;
    printf("Created buffer_handle\n");

    status_t err = openGraphicsHAL();
    if (err) {
        fprintf(stderr, "%s: Failed to open graphics HAL\n", __func__);
        return false;
    }

    err = grModule->registerBuffer(grModule, (buffer_handle_t)imgHandle);
    if (err) {
        fprintf(stderr, "%s: Failed to register buffer with gralloc (%d)\n",
                __func__, err);
        return false;
    }
    printf("Registerd the buffer with gralloc\n");

    anwb.common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
    anwb.common.version = sizeof(ANativeWindowBuffer);
    memset(anwb.common.reserved, 0, sizeof(anwb.common.reserved));
    anwb.common.incRef = incRefNop;
    anwb.common.decRef = decRefNop;
    anwb.width = imgHandle->iWidth;
    anwb.height = imgHandle->iHeight;
    anwb.stride = align(anwb.width, 16);
    anwb.usage = imgHandle->usage;
    anwb.format = imgHandle->iFormat;
    anwb.handle = (buffer_handle_t)imgHandle;
    printf("Create ANativeWindowBuffer out of buffer handle\n");

#if 0 
    yuvTexBuffer = new GraphicBuffer(yuvTexWidth, yuvTexHeight, yuvTexFormat,
            yuvTexUsage);
    int yuvTexStrideY = yuvTexBuffer->getStride();
    int yuvTexOffsetV = yuvTexStrideY * yuvTexHeight;
    int yuvTexStrideV = (yuvTexStrideY/2 + 0xf) & ~0xf;
    int yuvTexOffsetU = yuvTexOffsetV + yuvTexStrideV * yuvTexHeight/2;
    int yuvTexStrideU = yuvTexStrideV;
    char* buf = NULL;
    err = yuvTexBuffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&buf));
    if (err != 0) {
        fprintf(stderr, "yuvTexBuffer->lock(...) failed: %d\n", err);
        return false;
    }
    for (int x = 0; x < yuvTexWidth; x++) {
        for (int y = 0; y < yuvTexHeight; y++) {
            int parityX = (x / blockWidth) & 1;
            int parityY = (y / blockHeight) & 1;
            unsigned char intensity = (parityX ^ parityY) ? 63 : 191;
            buf[yuvTexOffsetY + (y * yuvTexStrideY) + x] = intensity;
            if (x < yuvTexWidth / 2 && y < yuvTexHeight / 2) {
                buf[yuvTexOffsetU + (y * yuvTexStrideU) + x] = intensity;
                if (yuvTexSameUV) {
                    buf[yuvTexOffsetV + (y * yuvTexStrideV) + x] = intensity;
                } else if (x < yuvTexWidth / 4 && y < yuvTexHeight / 4) {
                    buf[yuvTexOffsetV + (y*2 * yuvTexStrideV) + x*2 + 0] =
                    buf[yuvTexOffsetV + (y*2 * yuvTexStrideV) + x*2 + 1] =
                    buf[yuvTexOffsetV + ((y*2+1) * yuvTexStrideV) + x*2 + 0] =
                    buf[yuvTexOffsetV + ((y*2+1) * yuvTexStrideV) + x*2 + 1] = intensity;
                }
            }
        }
    }

    err = yuvTexBuffer->unlock();
    if (err != 0) {
        fprintf(stderr, "yuvTexBuffer->unlock() failed: %d\n", err);
        return false;
    }
#endif

    EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
            (EGLClientBuffer)&anwb, 0);
    checkEglError("eglCreateImageKHR");
    if (img == EGL_NO_IMAGE_KHR) {
        return false;
    }
    printf("Created EGLImage out of ANativeWindowBuffer\n");

    glGenTextures(1, &yuvTex);
    checkGlError("glGenTextures");
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, yuvTex);
    checkGlError("glBindTexture");
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
    checkGlError("glEGLImageTargetTexture2DOES");

    return true;
}

const GLfloat gTriangleVertices[] = {
    -0.5f, 0.5f,
    -0.5f, -0.5f,
    0.5f, -0.5f,
    0.5f, 0.5f,
};

void renderFrame() {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    glUseProgram(gProgram);
    checkGlError("glUseProgram");

    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, gTriangleVertices);
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray(gvPositionHandle);
    checkGlError("glEnableVertexAttribArray");

    glUniform1i(gYuvTexSamplerHandle, 0);
    checkGlError("glUniform1i");
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, yuvTex);
    checkGlError("glBindTexture");

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    checkGlError("glDrawArrays");
}

void printEGLConfiguration(EGLDisplay dpy, EGLConfig config) {

#define X(VAL) {VAL, #VAL}
    struct {EGLint attribute; const char* name;} names[] = {
    X(EGL_BUFFER_SIZE),
    X(EGL_ALPHA_SIZE),
    X(EGL_BLUE_SIZE),
    X(EGL_GREEN_SIZE),
    X(EGL_RED_SIZE),
    X(EGL_DEPTH_SIZE),
    X(EGL_STENCIL_SIZE),
    X(EGL_CONFIG_CAVEAT),
    X(EGL_CONFIG_ID),
    X(EGL_LEVEL),
    X(EGL_MAX_PBUFFER_HEIGHT),
    X(EGL_MAX_PBUFFER_PIXELS),
    X(EGL_MAX_PBUFFER_WIDTH),
    X(EGL_NATIVE_RENDERABLE),
    X(EGL_NATIVE_VISUAL_ID),
    X(EGL_NATIVE_VISUAL_TYPE),
    X(EGL_SAMPLES),
    X(EGL_SAMPLE_BUFFERS),
    X(EGL_SURFACE_TYPE),
    X(EGL_TRANSPARENT_TYPE),
    X(EGL_TRANSPARENT_RED_VALUE),
    X(EGL_TRANSPARENT_GREEN_VALUE),
    X(EGL_TRANSPARENT_BLUE_VALUE),
    X(EGL_BIND_TO_TEXTURE_RGB),
    X(EGL_BIND_TO_TEXTURE_RGBA),
    X(EGL_MIN_SWAP_INTERVAL),
    X(EGL_MAX_SWAP_INTERVAL),
    X(EGL_LUMINANCE_SIZE),
    X(EGL_ALPHA_MASK_SIZE),
    X(EGL_COLOR_BUFFER_TYPE),
    X(EGL_RENDERABLE_TYPE),
    X(EGL_CONFORMANT),
   };
#undef X

    for (size_t j = 0; j < sizeof(names) / sizeof(names[0]); j++) {
        EGLint value = -1;
        EGLint returnVal = eglGetConfigAttrib(dpy, config, names[j].attribute, &value);
        EGLint error = eglGetError();
        if (returnVal && error == EGL_SUCCESS) {
            printf(" %s: ", names[j].name);
            printf("%d (0x%x)", value, value);
        }
    }
    printf("\n");
}

int main(int argc, char** argv) {
    EGLBoolean returnValue;
    EGLConfig myConfig = {0};

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint s_configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE };
    EGLint majorVersion;
    EGLint minorVersion;
    EGLContext context;
    EGLSurface surface;
    EGLint w, h;

    EGLDisplay dpy;

    checkEglError("<init>");
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEglError("eglGetDisplay");
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        return 0;
    }

    returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
    checkEglError("eglInitialize", returnValue);
    fprintf(stderr, "EGL version %d.%d\n", majorVersion, minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        return 0;
    }

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();
    returnValue = EGLUtils::selectConfigForNativeWindow(dpy, s_configAttribs, window, &myConfig);
    if (returnValue) {
        printf("EGLUtils::selectConfigForNativeWindow() returned %d", returnValue);
        return 1;
    }

    checkEglError("EGLUtils::selectConfigForNativeWindow");

    printf("Chose this configuration:\n");
    printEGLConfiguration(dpy, myConfig);

    surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
    checkEglError("eglCreateWindowSurface");
    if (surface == EGL_NO_SURFACE) {
        printf("gelCreateWindowSurface failed.\n");
        return 1;
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    checkEglError("eglCreateContext");
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return 1;
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    checkEglError("eglMakeCurrent", returnValue);
    if (returnValue != EGL_TRUE) {
        return 1;
    }
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    checkEglError("eglQuerySurface");
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    checkEglError("eglQuerySurface");
    GLint dim = w < h ? w : h;

    fprintf(stderr, "Window dimensions: %d x %d\n", w, h);

    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Extensions", GL_EXTENSIONS);

    if(!setupGraphics(w, h)) {
        fprintf(stderr, "Could not set up graphics.\n");
        return 1;
    }
    printf("setupGraphics complete\n");

    if(!setupYuvTexSurface(dpy, context)) {
        fprintf(stderr, "Could not set up texture surface.\n");
        return 1;
    }
    printf("setupYuvTexSurface complete\n");

    for (;;) {
        renderFrame();
        eglSwapBuffers(dpy, surface);
        checkEglError("eglSwapBuffers");
    }

    return 0;
}