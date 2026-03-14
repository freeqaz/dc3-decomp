// MetalSurface.mm — Objective-C++ helper for creating a CAMetalLayer
// from an NSWindow. Required because CAMetalLayer is an ObjC class
// that can't be created from pure C++.

#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

extern "C" void* CreateMetalLayerForWindow(void* nsWindow) {
    NSWindow* window = (__bridge NSWindow*)nsWindow;
    CAMetalLayer* layer = [CAMetalLayer layer];
    [window.contentView setWantsLayer:YES];
    [window.contentView setLayer:layer];
    return (__bridge void*)layer;
}
