#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#endif

#include "graphics.hpp"

#if defined(INTERACTIVE_GRAPHICS) && defined(__APPLE__)

extern void key_bindings(const int key);

static NSWindow* mac_window = nil;
static NSView* mac_view = nil;
static double mac_scale = 1.0;

static CGColorSpaceRef mac_color_space = nullptr;
static CGImageRef mac_image = nullptr;
static const void* mac_bitmap_ptr = nullptr;
static size_t mac_bitmap_w = 0;
static size_t mac_bitmap_h = 0;
static std::atomic_bool mac_ignore_next_mouse = false;

static void mac_update_image() {
	const void* bitmap = (const void*)camera.bitmap;
	const size_t width = (size_t)camera.width;
	const size_t height = (size_t)camera.height;
	if(!mac_color_space) mac_color_space = CGColorSpaceCreateDeviceRGB();
	if(bitmap==nullptr||width==0||height==0) return;
	if(bitmap!=mac_bitmap_ptr||width!=mac_bitmap_w||height!=mac_bitmap_h) {
		if(mac_image) CGImageRelease(mac_image);
		CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, bitmap, width*height*4u, nullptr);
		mac_image = CGImageCreate(
			width, height,
			8, 32, width*4u,
			mac_color_space,
			((CGBitmapInfo)kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst),
			provider,
			nullptr,
			false,
			kCGRenderingIntentDefault
		);
		CGDataProviderRelease(provider);
		mac_bitmap_ptr = bitmap;
		mac_bitmap_w = width;
		mac_bitmap_h = height;
	}
}

static void mac_hide_cursor() {
	CGDisplayHideCursor(kCGDirectMainDisplay);
}

static void mac_show_cursor() {
	CGDisplayShowCursor(kCGDirectMainDisplay);
}

static void mac_warp_cursor_center() {
	if(!mac_window) return;
	const NSRect frame = [mac_window frame];
	const CGPoint center = CGPointMake(NSMidX(frame), NSMidY(frame));
	CGWarpMouseCursorPosition(center);
	mac_ignore_next_mouse = true;
}

static int mac_key_from_event(NSEvent* event) {
	NSString* chars = [event charactersIgnoringModifiers];
	if(chars == nil || [chars length]==0) return 0;
	const unichar c = [chars characterAtIndex:0];
	switch(c) {
		case NSUpArrowFunctionKey: return -38;
		case NSDownArrowFunctionKey: return -40;
		case NSLeftArrowFunctionKey: return -37;
		case NSRightArrowFunctionKey: return -39;
		case NSPageUpFunctionKey: return -33;
		case NSPageDownFunctionKey: return -34;
		case NSHomeFunctionKey: return -36;
		case NSEndFunctionKey: return -35;
		case NSDeleteCharacter: return 127;
		case NSBackspaceCharacter: return 8;
		case NSEnterCharacter: return 10;
		case NSCarriageReturnCharacter: return 10;
		default: break;
	}
	if(c>='a' && c<='z') return (int)(c-32);
	return (int)c;
}

@interface FluidX3DView : NSView
@end

@interface FluidX3DWindow : NSWindow
@end

@implementation FluidX3DWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@implementation FluidX3DView
- (BOOL)isFlipped {
	return YES;
}
- (BOOL)acceptsFirstResponder {
	return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
	(void)dirtyRect;
	if(!camera.allow_rendering) return;
	mac_update_image();
	if(!mac_image) return;
	CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
	CGContextSaveGState(ctx);
	CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
	CGContextTranslateCTM(ctx, 0.0, self.bounds.size.height);
	CGContextScaleCTM(ctx, 1.0, -1.0);
	CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, self.bounds.size.width, self.bounds.size.height), mac_image);
	CGContextRestoreGState(ctx);
}
- (void)keyDown:(NSEvent*)event {
	const int key = mac_key_from_event(event);
	if(key==0) return;
	if(key=='U') {
		if(!camera.lockmouse) {
			mac_show_cursor();
		} else {
			mac_hide_cursor();
			mac_warp_cursor_center();
		}
	}
	camera.set_key_state(key, true);
	key_bindings(key);
}
- (void)keyUp:(NSEvent*)event {
	const int key = mac_key_from_event(event);
	if(key==0) return;
	camera.set_key_state(key, false);
}
- (void)mouseMoved:(NSEvent*)event {
	if(mac_ignore_next_mouse) { mac_ignore_next_mouse = false; return; }
	if(camera.lockmouse) return;
	const CGFloat dx = [event deltaX] * mac_scale;
	const CGFloat dy = [event deltaY] * mac_scale;
	camera.input_mouse_dragged((int)dx, (int)dy);
}
- (void)mouseDragged:(NSEvent*)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [self mouseMoved:event]; }
- (void)scrollWheel:(NSEvent*)event {
	if(event.scrollingDeltaY>0.0) camera.input_scroll_up();
	else if(event.scrollingDeltaY<0.0) camera.input_scroll_down();
}
- (void)mouseDown:(NSEvent*)event {
	(void)event;
	if(!camera.lockmouse) {
		mac_show_cursor();
	} else {
		mac_hide_cursor();
		mac_warp_cursor_center();
	}
	camera.input_key('U');
}
- (void)rightMouseDown:(NSEvent*)event { [self mouseDown:event]; }
- (void)otherMouseDown:(NSEvent*)event { [self mouseDown:event]; }
@end

static void mac_update_frame(const double frametime) {
	main_label(frametime);
	[mac_view setNeedsDisplay:YES];
	[mac_view displayIfNeeded];
}

int main(int argc, char* argv[]) {
	main_arguments = get_main_arguments(argc, argv);

	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		NSScreen* screen = [NSScreen mainScreen];
		const NSRect screen_frame = [screen frame];
		mac_scale = [screen backingScaleFactor];

		const uint width = (uint)(screen_frame.size.width*mac_scale);
		const uint height = (uint)(screen_frame.size.height*mac_scale);
		const uint fps_limit = (uint)max((int)[screen maximumFramesPerSecond], 60);

		camera = Camera(width, height, fps_limit);

		mac_window = [[FluidX3DWindow alloc] initWithContentRect:screen_frame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered
			defer:NO];
		[mac_window setLevel:NSMainMenuWindowLevel+1];
		[mac_window setOpaque:YES];
		[mac_window setHidesOnDeactivate:NO];
		[mac_window setAcceptsMouseMovedEvents:YES];
		[mac_window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenPrimary];

		mac_view = [[FluidX3DView alloc] initWithFrame:NSMakeRect(0.0, 0.0, screen_frame.size.width, screen_frame.size.height)];
		[mac_window setContentView:mac_view];
		[mac_window makeFirstResponder:mac_view];
		[mac_window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];

		mac_hide_cursor();
		mac_warp_cursor_center();

		thread compute_thread(main_physics);

		Clock clock;
		double frametime = 1.0;
		while(running) {
			@autoreleasepool {
				NSEvent* event = nil;
				do {
					event = [NSApp nextEventMatchingMask:NSEventMaskAny
						untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
						inMode:NSDefaultRunLoopMode
						dequeue:YES];
					if(event) [NSApp sendEvent:event];
				} while(event);
				[NSApp updateWindows];

				camera.rendring_frame.lock();
				camera.update_state(fmax(1.0/(double)camera.fps_limit, frametime));
				main_graphics();
				mac_update_frame(frametime);
				camera.rendring_frame.unlock();
			}
			frametime = clock.stop();
			sleep(1.0/(double)camera.fps_limit-frametime);
			clock.start();
		}

		mac_show_cursor();
		if(mac_image) CGImageRelease(mac_image);
		if(mac_color_space) CGColorSpaceRelease(mac_color_space);
		compute_thread.join();
	}
	return 0;
}

#endif // INTERACTIVE_GRAPHICS && __APPLE__
