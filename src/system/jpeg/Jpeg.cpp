#include "jpeg/Jpeg.h"
#include "jpeg/jpeglib.h"
#include "os/Debug.h"

namespace {
    void JpegInitDestination(jpeg_compress_struct *s) {
        jpeg_destination_mgr *dest = s->dest;
        MILO_ASSERT(dest, 0x8b);
        // dest->init_destination;
    }
    unsigned char JpegEmptyOutputBuffer(jpeg_compress_struct *s) {
        MILO_ASSERT(false, 0x94);
        return 0;
    }
    void JpegTermDestination(jpeg_compress_struct *s) {
        jpeg_destination_mgr *dest = s->dest;
        MILO_ASSERT(dest, 0x9c);
        // dest->term_destination;
    }
};

bool LoadBitmapIntoJpeg(char *data, int width, int height, int depth, void *destBuffer, int &outSize) {
    JSAMPROW rowPtr;
    jpeg_destination_mgr destMgr;
    jpeg_compress_struct cinfo;
    jpeg_error_mgr errorMgr;
    int bytesPerRow;

    cinfo.err = jpeg_std_error(&errorMgr);
    jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION, sizeof(jpeg_compress_struct));

    *(long long*)&destMgr = 0;
    *(long long*)(((char*)&destMgr) + 8) = 0;
    destMgr.next_output_byte = (JOCTET *)destBuffer;
    *(long long*)(((char*)&destMgr) + 16) = 0;
    destMgr.free_in_buffer = 2;
    *(long long*)(((char*)&destMgr) + 24) = 0;

    destMgr.init_destination = JpegInitDestination;
    destMgr.empty_output_buffer = JpegEmptyOutputBuffer;
    destMgr.term_destination = JpegTermDestination;

    cinfo.dest = &destMgr;

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
    outSize = (int)destMgr.next_output_byte - (int)destBuffer;

    return true;
}
