#include "jpeg/Jpeg.h"
#include "jpeg/jpeglib.h"
#include "os/Debug.h"

namespace {
    // Extended destination manager with additional fields for buffer tracking
    struct ExtendedDestMgr {
        jpeg_destination_mgr pub;  // Standard manager (0x0-0x13)
        JOCTET *bufferStart;       // Original buffer pointer (0x14)
        size_t bufferSize;         // Total buffer size (0x18)
    };

    void JpegInitDestination(jpeg_compress_struct *s) {
        ExtendedDestMgr *dest = (ExtendedDestMgr *)s->dest;
        MILO_ASSERT(dest, 0x8b);
        dest->pub.next_output_byte = dest->bufferStart;
        dest->pub.free_in_buffer = dest->bufferSize;
    }
    unsigned char JpegEmptyOutputBuffer(jpeg_compress_struct *s) {
        MILO_ASSERT(false, 0x94);
        return 0;
    }
    void JpegTermDestination(jpeg_compress_struct *s) {
        ExtendedDestMgr *dest = (ExtendedDestMgr *)s->dest;
        MILO_ASSERT(dest, 0x9c);
        // No cleanup needed for this simple destination manager
    }
};

bool LoadBitmapIntoJpeg(char *data, int width, int height, int depth, void *destBuffer, int &outSize) {
    JSAMPROW rowPtr;
    ExtendedDestMgr destMgr;
    jpeg_compress_struct cinfo;
    jpeg_error_mgr errorMgr;
    int bytesPerRow;

    cinfo.err = jpeg_std_error(&errorMgr);
    jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION, sizeof(jpeg_compress_struct));

    // Zero-initialize in 8-byte chunks (codegen-sensitive order)
    *(long long*)&destMgr = 0;
    *(long long*)(((char*)&destMgr) + 8) = 0;
    // Note: bufferStart and bufferSize are set up at specific offsets
    // JpegInitDestination will copy these to pub.next_output_byte and pub.free_in_buffer
    destMgr.bufferStart = (JOCTET *)destBuffer;
    *(long long*)(((char*)&destMgr) + 16) = 0;
    destMgr.bufferSize = 2;
    *(long long*)(((char*)&destMgr) + 24) = 0;

    destMgr.pub.init_destination = JpegInitDestination;
    destMgr.pub.empty_output_buffer = JpegEmptyOutputBuffer;
    destMgr.pub.term_destination = JpegTermDestination;

    cinfo.dest = &destMgr.pub;

    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);

    bytesPerRow = depth * width;

    if (height > 0) {
        do {
            rowPtr = (JSAMPROW)data;
            jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&rowPtr, 1);
            height--;
            data += bytesPerRow;
        } while (height != 0);
    }

    jpeg_finish_compress(&cinfo);
    outSize = (int)destMgr.pub.next_output_byte - (int)destBuffer;

    return true;
}
