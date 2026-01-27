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
    jpeg_error_mgr errorMgr;
    jpeg_compress_struct cinfo;
    jpeg_destination_mgr destMgr;
    int rowIndex;
    int bytesPerRow;
    JSAMPROW samprow;
    int offset;

    // Get error manager
    jpeg_std_error(&errorMgr);

    // Create compression structure
    jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION, sizeof(jpeg_compress_struct));

    // Initialize destination manager structure (memset to 0 first)
    *(long long*)&destMgr = 0;
    *(long long*)(((char*)&destMgr) + 8) = 0;
    *(long long*)(((char*)&destMgr) + 16) = 0;
    *(long long*)(((char*)&destMgr) + 24) = 0;

    // Set destination callbacks
    destMgr.init_destination = JpegInitDestination;
    destMgr.empty_output_buffer = JpegEmptyOutputBuffer;
    destMgr.term_destination = JpegTermDestination;
    destMgr.next_output_byte = (JOCTET *)destBuffer;
    destMgr.free_in_buffer = 2;

    cinfo.dest = &destMgr;

    // Set compression parameters
    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);

    // Compute bytes per row
    bytesPerRow = depth * width;
    rowIndex = 0;

    // Write scanlines
    while (height > rowIndex) {
        offset = rowIndex * bytesPerRow;
        samprow = (JSAMPROW)(data + offset);
        jpeg_write_scanlines(&cinfo, &samprow, 1);
        rowIndex++;
    }

    jpeg_finish_compress(&cinfo);
    outSize = *(int*)&destMgr;

    return true;
}
