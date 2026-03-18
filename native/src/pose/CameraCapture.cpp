// Platform-native webcam capture
// Linux: V4L2  |  macOS/Windows: stub (use OpenCV fallback)

#include "pose/CameraCapture.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#define USE_V4L2
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#endif

CameraCapture::CameraCapture() {}

CameraCapture::~CameraCapture() {
    Close();
}

#ifdef USE_V4L2

// V4L2 buffer state
struct V4L2Data {
    void *buffers[4];
    size_t bufferLengths[4];
    int numBuffers;
};

bool CameraCapture::Open(int cameraIndex, int width, int height) {
    if (mOpen) return true;

    char devPath[32];
    snprintf(devPath, sizeof(devPath), "/dev/video%d", cameraIndex);

    mFd = open(devPath, O_RDWR | O_NONBLOCK);
    if (mFd < 0) {
        fprintf(stderr, "CameraCapture: failed to open %s\n", devPath);
        return false;
    }

    // Set format to YUYV (most webcams support it)
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
        // Try MJPEG as fallback
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
            fprintf(stderr, "CameraCapture: failed to set format on %s\n", devPath);
            close(mFd);
            mFd = -1;
            return false;
        }
    }

    mWidth = fmt.fmt.pix.width;
    mHeight = fmt.fmt.pix.height;

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "CameraCapture: VIDIOC_REQBUFS failed\n");
        close(mFd);
        mFd = -1;
        return false;
    }

    auto *data = new V4L2Data();
    data->numBuffers = req.count;
    mPlatformData = data;

    for (int i = 0; i < (int)req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(mFd, VIDIOC_QUERYBUF, &buf);

        data->bufferLengths[i] = buf.length;
        data->buffers[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, mFd, buf.m.offset);

        // Queue buffer
        ioctl(mFd, VIDIOC_QBUF, &buf);
    }

    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(mFd, VIDIOC_STREAMON, &type);

    // Allocate RGB output buffer
    mFrameBufferSize = mWidth * mHeight * 3;
    mFrameBuffer = new uint8_t[mFrameBufferSize];

    mOpen = true;
    printf("CameraCapture: opened %s (%dx%d)\n", devPath, mWidth, mHeight);
    return true;
}

void CameraCapture::Close() {
    if (!mOpen) return;

    if (mFd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(mFd, VIDIOC_STREAMOFF, &type);

        if (mPlatformData) {
            auto *data = (V4L2Data *)mPlatformData;
            for (int i = 0; i < data->numBuffers; i++) {
                munmap(data->buffers[i], data->bufferLengths[i]);
            }
            delete data;
            mPlatformData = nullptr;
        }

        close(mFd);
        mFd = -1;
    }

    delete[] mFrameBuffer;
    mFrameBuffer = nullptr;
    mOpen = false;
}

// Convert YUYV to RGB24
static void YUYVtoRGB(const uint8_t *yuyv, uint8_t *rgb, int width, int height) {
    for (int i = 0; i < width * height / 2; i++) {
        int y0 = yuyv[0], u = yuyv[1], y1 = yuyv[2], v = yuyv[3];
        yuyv += 4;

        int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;

        auto clamp = [](int v) -> uint8_t {
            return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        };

        rgb[0] = clamp((298 * c0 + 409 * e + 128) >> 8);
        rgb[1] = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);
        rgb[2] = clamp((298 * c0 + 516 * d + 128) >> 8);
        rgb[3] = clamp((298 * c1 + 409 * e + 128) >> 8);
        rgb[4] = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
        rgb[5] = clamp((298 * c1 + 516 * d + 128) >> 8);
        rgb += 6;
    }
}

const uint8_t *CameraCapture::CaptureFrame() {
    if (!mOpen) return nullptr;

    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(mFd, VIDIOC_DQBUF, &buf) < 0) {
        return nullptr; // no frame ready (non-blocking)
    }

    auto *data = (V4L2Data *)mPlatformData;
    const uint8_t *raw = (const uint8_t *)data->buffers[buf.index];

    // Convert YUYV to RGB (TODO: handle MJPEG format)
    YUYVtoRGB(raw, mFrameBuffer, mWidth, mHeight);

    // Re-queue buffer
    ioctl(mFd, VIDIOC_QBUF, &buf);

    return mFrameBuffer;
}

#else // !USE_V4L2

// Stub for non-Linux platforms
bool CameraCapture::Open(int, int, int) {
    fprintf(stderr, "CameraCapture: not implemented on this platform "
                    "(build with V4L2 on Linux, or use DC3_POSE=external)\n");
    return false;
}
void CameraCapture::Close() {}
const uint8_t *CameraCapture::CaptureFrame() { return nullptr; }

#endif // USE_V4L2
