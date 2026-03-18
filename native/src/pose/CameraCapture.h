#pragma once
// Platform-native webcam capture
// Linux: V4L2  |  macOS: AVFoundation  |  Fallback: disabled
//
// Provides raw RGB frames for pose estimation.

#include <cstdint>
#include <string>

class CameraCapture {
public:
    CameraCapture();
    ~CameraCapture();

    // Open camera device. Returns false if camera unavailable.
    bool Open(int cameraIndex = 0, int width = 640, int height = 480);
    void Close();
    bool IsOpen() const { return mOpen; }

    int Width() const { return mWidth; }
    int Height() const { return mHeight; }

    // Capture a frame. Returns pointer to RGB24 data (width*height*3 bytes)
    // or nullptr if no frame available. Data valid until next CaptureFrame() call.
    const uint8_t *CaptureFrame();

private:
    bool mOpen = false;
    int mWidth = 0;
    int mHeight = 0;
    int mFd = -1;  // V4L2 file descriptor (Linux)

    uint8_t *mFrameBuffer = nullptr;
    int mFrameBufferSize = 0;

    // Platform-specific state
    void *mPlatformData = nullptr;
};
