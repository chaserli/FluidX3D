#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreFoundation/CoreFoundation.h>

#include "metal.hpp"

struct MetalContext {
	void* device = nullptr;
	void* queue = nullptr;
	void* library = nullptr;
	void* last = nullptr;
};

struct MetalBuffer {
	void* buffer = nullptr;
};

struct MetalPipeline {
	void* pipeline = nullptr;
};

static id<MTLDevice> metal_default_device() {
	static id<MTLDevice> dev = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		dev = MTLCreateSystemDefaultDevice();
	});
	return dev;
}

vector<void*> metal_get_devices() {
	vector<void*> devices;
	id<MTLDevice> dev = metal_default_device();
	if(dev) devices.push_back((__bridge void*)dev);
	return devices;
}

string metal_get_device_name(void* mtl_device) {
	id<MTLDevice> dev = (__bridge id<MTLDevice>)mtl_device;
	if(!dev) return "Unknown";
	NSString* name = [dev name];
	return name ? string([name UTF8String]) : string("Metal Device");
}

ulong metal_get_device_memory(void* mtl_device) {
	id<MTLDevice> dev = (__bridge id<MTLDevice>)mtl_device;
	if(!dev) return 0ull;
	if([dev respondsToSelector:@selector(recommendedMaxWorkingSetSize)]) {
		return (ulong)[dev recommendedMaxWorkingSetSize];
	}
	return 0ull;
}

uint metal_get_device_max_buffer_length(void* mtl_device) {
	id<MTLDevice> dev = (__bridge id<MTLDevice>)mtl_device;
	if(!dev) return 0u;
	return (uint)[dev maxBufferLength];
}

bool metal_device_uses_shared_memory(void* mtl_device) {
	id<MTLDevice> dev = (__bridge id<MTLDevice>)mtl_device;
	if(!dev) return true;
	if([dev respondsToSelector:@selector(hasUnifiedMemory)]) {
		return (bool)[dev hasUnifiedMemory];
	}
	return true;
}

uint metal_get_device_threads_per_group(void* mtl_device) {
	id<MTLDevice> dev = (__bridge id<MTLDevice>)mtl_device;
	if(!dev) return WORKGROUP_SIZE;
	return (uint)[dev maxThreadsPerThreadgroup].width;
}

MetalContext* metal_create_context(void* mtl_device, const char* source) {
	@autoreleasepool {
		id<MTLDevice> dev = mtl_device ? (__bridge id<MTLDevice>)mtl_device : metal_default_device();
		if(!dev) return nullptr;

		MetalContext* ctx = new MetalContext();
		ctx->device = (__bridge_retained void*)dev;

		id<MTLCommandQueue> queue = [dev newCommandQueue];
		if(!queue) {
			print_error("Metal command queue creation failed.");
			metal_destroy_context(ctx);
			return nullptr;
		}
		ctx->queue = (__bridge_retained void*)queue;

		NSString* src = [NSString stringWithUTF8String:source ? source : ""];
		MTLCompileOptions* options = [MTLCompileOptions new];
		if([options respondsToSelector:@selector(setMathMode:)]) {
			options.mathMode = MTLMathModeFast;
		}
		if([options respondsToSelector:@selector(setLanguageVersion:)]) {
#if defined(MTLLanguageVersion3_0)
			options.languageVersion = MTLLanguageVersion3_0;
#elif defined(MTLLanguageVersion2_4)
			options.languageVersion = MTLLanguageVersion2_4;
#elif defined(MTLLanguageVersion2_3)
			options.languageVersion = MTLLanguageVersion2_3;
#elif defined(MTLLanguageVersion2_2)
			options.languageVersion = MTLLanguageVersion2_2;
#elif defined(MTLLanguageVersion2_1)
			options.languageVersion = MTLLanguageVersion2_1;
#elif defined(MTLLanguageVersion2_0)
			options.languageVersion = MTLLanguageVersion2_0;
#endif
		}
		NSError* error = nil;
		id<MTLLibrary> library = [dev newLibraryWithSource:src options:options error:&error];
		if(!library) {
			const char* msg = error ? [[error localizedDescription] UTF8String] : "unknown error";
			write_file("bin/metal_build_log.txt", string("Metal shader compilation failed: ")+msg);
			print_error(string("Metal shader compilation failed: ")+msg);
			metal_destroy_context(ctx);
			return nullptr;
		}
		ctx->library = (__bridge_retained void*)library;
		return ctx;
	}
}

void metal_destroy_context(MetalContext* ctx) {
	if(!ctx) return;
	if(ctx->last) { id tmp = (__bridge_transfer id)ctx->last; (void)tmp; ctx->last = nullptr; }
	if(ctx->library) { id tmp = (__bridge_transfer id)ctx->library; (void)tmp; ctx->library = nullptr; }
	if(ctx->queue) { id tmp = (__bridge_transfer id)ctx->queue; (void)tmp; ctx->queue = nullptr; }
	if(ctx->device) { id tmp = (__bridge_transfer id)ctx->device; (void)tmp; ctx->device = nullptr; }
	delete ctx;
}

void* metal_get_device_from_context(MetalContext* ctx) {
	if(!ctx) return nullptr;
	return ctx->device;
}

MetalBuffer* metal_create_buffer(MetalContext* ctx, ulong size_bytes) {
	if(!ctx||!ctx->device) return nullptr;
	id<MTLDevice> dev = (__bridge id<MTLDevice>)ctx->device;
	id<MTLBuffer> buf = [dev newBufferWithLength:(NSUInteger)size_bytes options:MTLResourceStorageModeShared];
	if(!buf) {
		print_error("Metal buffer allocation failed.");
		return nullptr;
	}
	MetalBuffer* buffer = new MetalBuffer();
	buffer->buffer = (__bridge_retained void*)buf;
	return buffer;
}

void metal_release_buffer(MetalBuffer* buffer) {
	if(!buffer) return;
	if(buffer->buffer) CFRelease(buffer->buffer);
	delete buffer;
}

void* metal_buffer_contents(MetalBuffer* buffer) {
	if(!buffer||!buffer->buffer) return nullptr;
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->buffer;
	return [buf contents];
}

void metal_buffer_copy_to(MetalBuffer* buffer, const void* src, ulong size_bytes, ulong offset_bytes) {
	if(!buffer||!buffer->buffer||!src) return;
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->buffer;
	void* base = [buf contents];
	if(!base) {
		print_error("Metal buffer contents are unavailable.");
		return;
	}
	void* dst = (char*)base + offset_bytes;
	memcpy(dst, src, (size_t)size_bytes);
}

void metal_buffer_copy_from(MetalBuffer* buffer, void* dst, ulong size_bytes, ulong offset_bytes) {
	if(!buffer||!buffer->buffer||!dst) return;
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->buffer;
	const void* base = [buf contents];
	if(!base) {
		print_error("Metal buffer contents are unavailable.");
		return;
	}
	const void* src = (char*)base + offset_bytes;
	memcpy(dst, src, (size_t)size_bytes);
}

MetalPipeline* metal_create_pipeline(MetalContext* ctx, const char* function_name) {
	if(!ctx||!ctx->device||!ctx->library) return nullptr;
	id<MTLDevice> dev = (__bridge id<MTLDevice>)ctx->device;
	id<MTLLibrary> lib = (__bridge id<MTLLibrary>)ctx->library;
	NSString* fname = [NSString stringWithUTF8String:function_name ? function_name : ""];
id<MTLFunction> func = [lib newFunctionWithName:fname];
	if(!func && [lib respondsToSelector:@selector(functionNames)]) {
		NSArray<NSString*>* names = [lib functionNames];
		for(NSString* n in names) {
			if([n hasPrefix:fname]) {
				func = [lib newFunctionWithName:n];
				break;
			}
		}
		if(!func) {
			string list;
			for(NSString* n in names) {
				list += string([n UTF8String]) + "\n";
			}
			write_file("bin/metal_functions.txt", list);
		}
	}
	if(!func) func = [lib newFunctionWithName:@"noop"];
	if(!func) {
		print_error(string("Metal kernel not found: ")+(function_name?function_name:"(null)")+". See bin/metal_functions.txt for available functions.");
		return nullptr;
	}
	NSError* error = nil;
	id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:func error:&error];
	if(!pso) {
		const char* msg = error ? [[error localizedDescription] UTF8String] : "unknown error";
		print_error(string("Metal pipeline creation failed: ")+msg);
		return nullptr;
	}
	MetalPipeline* pipeline = new MetalPipeline();
	pipeline->pipeline = (__bridge_retained void*)pso;
	return pipeline;
}

void metal_release_pipeline(MetalPipeline* pipeline) {
	if(!pipeline) return;
	if(pipeline->pipeline) CFRelease(pipeline->pipeline);
	delete pipeline;
}

void metal_dispatch(MetalContext* ctx, MetalPipeline* pipeline, ulong threads, uint threads_per_group, const MetalArg* args, uint arg_count) {
	if(!ctx||!ctx->queue||!pipeline||!pipeline->pipeline) return;
	id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)ctx->queue;
	id<MTLComputePipelineState> pso = (__bridge id<MTLComputePipelineState>)pipeline->pipeline;
	id<MTLCommandBuffer> cb = [queue commandBuffer];
	id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
	[enc setComputePipelineState:pso];
	for(uint i=0u; i<arg_count; i++) {
		if(args[i].kind==MetalArg::Kind::Buffer) {
			id<MTLBuffer> buf = args[i].buffer ? (__bridge id<MTLBuffer>)args[i].buffer->buffer : nil;
			[enc setBuffer:buf offset:0 atIndex:i];
		} else {
			const void* bytes = args[i].bytes.data();
			const NSUInteger len = (NSUInteger)args[i].bytes.size();
			if(len>0u) [enc setBytes:bytes length:len atIndex:i];
		}
	}
	const uint tpg = max(threads_per_group, 1u);
	MTLSize grid = MTLSizeMake((NSUInteger)threads, 1, 1);
	MTLSize tg = MTLSizeMake((NSUInteger)tpg, 1, 1);
	[enc dispatchThreads:grid threadsPerThreadgroup:tg];
	[enc endEncoding];
	[cb commit];
	if(ctx->last) {
		id tmp = (__bridge_transfer id)ctx->last;
		(void)tmp;
		ctx->last = nullptr;
	}
	ctx->last = (__bridge_retained void*)cb;
}

void metal_finish(MetalContext* ctx) {
	if(!ctx||!ctx->last) return;
	id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)ctx->last;
	[cb waitUntilCompleted];
	id tmp = (__bridge_transfer id)ctx->last;
	(void)tmp;
	ctx->last = nullptr;
}
