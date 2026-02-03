#pragma once

#if !defined(__APPLE__)
#error "Metal backend is only supported on macOS."
#endif

#include "utilities.hpp"
#include <vector>
#include <cstring>

// Keep workgroup size aligned with existing OpenCL defaults for now.
constexpr uint WORKGROUP_SIZE = 64u;

// Minimal event placeholder for API compatibility.
using Event = int;

struct MetalContext;
struct MetalBuffer;
struct MetalPipeline;

struct MetalArg {
	enum class Kind { Buffer, Bytes };
	Kind kind = Kind::Bytes;
	MetalBuffer* buffer = nullptr;
	vector<uchar> bytes;
};

// C API implemented in metal.mm
MetalContext* metal_create_context(void* mtl_device, const char* source);
void metal_destroy_context(MetalContext* ctx);
void* metal_get_device_from_context(MetalContext* ctx);
MetalBuffer* metal_create_buffer(MetalContext* ctx, ulong size_bytes);
void metal_release_buffer(MetalBuffer* buffer);
void* metal_buffer_contents(MetalBuffer* buffer);
void metal_buffer_copy_to(MetalBuffer* buffer, const void* src, ulong size_bytes, ulong offset_bytes);
void metal_buffer_copy_from(MetalBuffer* buffer, void* dst, ulong size_bytes, ulong offset_bytes);
MetalPipeline* metal_create_pipeline(MetalContext* ctx, const char* function_name);
void metal_release_pipeline(MetalPipeline* pipeline);
void metal_dispatch(MetalContext* ctx, MetalPipeline* pipeline, ulong threads, uint threads_per_group, const MetalArg* args, uint arg_count);
void metal_finish(MetalContext* ctx);
vector<void*> metal_get_devices();
string metal_get_device_name(void* mtl_device);
ulong metal_get_device_memory(void* mtl_device);
uint metal_get_device_max_buffer_length(void* mtl_device);
bool metal_device_uses_shared_memory(void* mtl_device);
uint metal_get_device_threads_per_group(void* mtl_device);

string get_opencl_c_code_string();
string get_metal_msl_code(const string& device_defines, const string& graphics_defines);

struct Device_Info {
	void* mtl_device = nullptr;
	uint id = 0u;
	string name = "";
	string vendor = "Apple";
	string driver_version = "Metal";
	string metal_version = "Metal";
	uint memory = 0u; // in MB
	uint memory_used = 0u; // in MB
	uint max_global_buffer = 0u; // in MB
	uint max_constant_buffer = 0u; // in KB
	uint compute_units = 0u;
	uint clock_frequency = 0u;
	bool is_cpu = false;
	bool is_gpu = true;
	bool uses_ram = true;
	float tflops = 1.0f;
	inline Device_Info() {}
	inline Device_Info(void* mtl_device, const uint id) {
		this->mtl_device = mtl_device;
		this->id = id;
		name = metal_get_device_name(mtl_device);
		const ulong bytes = metal_get_device_memory(mtl_device);
		memory = (uint)(bytes/1048576ull);
		max_global_buffer = (uint)(metal_get_device_max_buffer_length(mtl_device)/1048576ull);
		uses_ram = metal_device_uses_shared_memory(mtl_device);
		compute_units = metal_get_device_threads_per_group(mtl_device);
	}
};

inline void print_device_info(const Device_Info& d) {
	println("\r|----------------.------------------------------------------------------------|");
	println("| Device ID      | "+alignl(58, to_string(d.id)                             )+" |");
	println("| Device Name    | "+alignl(58, d.name                                      )+" |");
	println("| Device Vendor  | "+alignl(58, d.vendor                                    )+" |");
	println("| Device Driver  | "+alignl(58, d.driver_version+" (macOS)"                )+" |");
	println("| Compute Units  | "+alignl(58, to_string(d.compute_units)                  )+" |");
	println("| Memory         | "+alignl(58, to_string(d.memory)+" MB "+(d.uses_ram ? "" : "V")+"RAM")+" |");
	println("| Buffer Limits  | "+alignl(58, to_string(d.max_global_buffer)+" MB global" )+" |");
	println("|----------------'------------------------------------------------------------|");
}

inline vector<Device_Info> get_devices(const bool print_info=true) {
	vector<Device_Info> devices;
	const vector<void*> mtl_devices = metal_get_devices();
	uint id = 0u;
	for(void* dev : mtl_devices) devices.push_back(Device_Info(dev, id++));
	if((uint)devices.size()==0u) {
		print_error("No Metal devices are available.");
	}
	if(print_info) {
		println("\r|----------------.------------------------------------------------------------|");
		for(uint i=0u; i<(uint)devices.size(); i++) println("| Device ID "+alignr(4u, i)+" | "+alignl(58u, devices[i].name)+" |");
		println("|----------------'------------------------------------------------------------|");
	}
	return devices;
}
inline Device_Info select_device_with_most_flops(const vector<Device_Info>& devices=get_devices()) {
	float best_value = -1.0f;
	uint best_i = 0u;
	for(uint i=0u; i<(uint)devices.size(); i++) {
		if(devices[i].tflops>best_value) { best_value = devices[i].tflops; best_i = i; }
	}
	return devices[best_i];
}
inline Device_Info select_device_with_most_memory(const vector<Device_Info>& devices=get_devices()) {
	uint best_value = 0u;
	uint best_i = 0u;
	for(uint i=0u; i<(uint)devices.size(); i++) {
		if(devices[i].memory>best_value) { best_value = devices[i].memory; best_i = i; }
	}
	return devices[best_i];
}
inline Device_Info select_device_with_id(const uint id, const vector<Device_Info>& devices=get_devices()) {
	if(id<(uint)devices.size()) return devices[id];
	print_error("Your selected Device ID ("+to_string(id)+") is wrong.");
	return devices[0];
}

class Device {
private:
	MetalContext* ctx = nullptr;
	bool exists = false;
public:
	Device_Info info;
	inline Device(const Device_Info& info, const string& msl_code=get_metal_msl_code("", "")) {
		print_device_info(info);
		this->info = info;
		this->ctx = metal_create_context(info.mtl_device, msl_code.c_str());
		this->exists = (ctx!=nullptr);
		if(!this->exists) print_error("Metal device initialization failed.");
	}
	inline Device() {}
	Device(const Device&)=delete;
	Device& operator=(const Device&)=delete;
	inline Device(Device&& other) noexcept { *this = std::move(other); }
	inline Device& operator=(Device&& other) noexcept {
		if(this!=&other) {
			if(ctx) metal_destroy_context(ctx);
			ctx = other.ctx;
			exists = other.exists;
			info = other.info;
			other.ctx = nullptr;
			other.exists = false;
		}
		return *this;
	}
	inline ~Device() { if(ctx) metal_destroy_context(ctx); }
	inline void finish_queue() { metal_finish(ctx); }
	inline MetalContext* get_context() const { return ctx; }
	inline bool is_initialized() const { return exists; }
};

template<typename T> class Memory {
private:
	ulong N = 0ull;
	uint d = 1u;
	bool host_buffer_exists = false;
	bool device_buffer_exists = false;
	bool external_host_buffer = false;
	bool is_zero_copy = false;
	T* host_buffer = nullptr;
	T* host_buffer_unaligned = nullptr;
	MetalBuffer* device_buffer = nullptr;
	Device* device = nullptr;
	inline void initialize_auxiliary_pointers() {
		/********/ x = s0 = host_buffer; /******/ if(d>0x4u) s4 = host_buffer+N*0x4ull; if(d>0x8u) s8 = host_buffer+N*0x8ull; if(d>0xCu) sC = host_buffer+N*0xCull;
		if(d>0x1u) y = s1 = host_buffer+N; /****/ if(d>0x5u) s5 = host_buffer+N*0x5ull; if(d>0x9u) s9 = host_buffer+N*0x9ull; if(d>0xDu) sD = host_buffer+N*0xDull;
		if(d>0x2u) z = s2 = host_buffer+N*0x2ull; if(d>0x6u) s6 = host_buffer+N*0x6ull; if(d>0xAu) sA = host_buffer+N*0xAull; if(d>0xEu) sE = host_buffer+N*0xEull;
		if(d>0x3u) w = s3 = host_buffer+N*0x3ull; if(d>0x7u) s7 = host_buffer+N*0x7ull; if(d>0xBu) sB = host_buffer+N*0xBull; if(d>0xFu) sF = host_buffer+N*0xFull;
	}
	inline void allocate_host_buffer(Device& device, const bool allocate_host, const bool allow_zero_copy) {
		if(allocate_host) {
			const ulong alignment = allow_zero_copy&&device.info.uses_ram ? 4096ull : 64ull;
			const ulong padding   = allow_zero_copy&&device.info.uses_ram ?   64ull :  0ull;
			host_buffer_unaligned = new T[N*(ulong)d+(alignment+padding)/sizeof(T)];
			host_buffer = (T*)((((ulong)host_buffer_unaligned+alignment-1ull)/alignment)*alignment);
			initialize_auxiliary_pointers();
			host_buffer_exists = true;
		}
	}
	inline void allocate_device_buffer(Device& device, const bool allocate_device, const bool allow_zero_copy) {
		this->device = &device;
		if(allocate_device) {
			device.info.memory_used += (uint)(capacity()/1048576ull);
			if(device.info.memory_used>device.info.memory && device.info.memory>0u) {
				print_error("Device \""+device.info.name+"\" does not have enough memory. Allocating another "+to_string((uint)(capacity()/1048576ull))+" MB would use a total of "+to_string(device.info.memory_used)+" MB / "+to_string(device.info.memory)+" MB.");
			}
			is_zero_copy = false;
			device_buffer = metal_create_buffer(device.get_context(), capacity());
			if(!device_buffer) print_error("Metal buffer allocation failed.");
			device_buffer_exists = true;
		}
		(void)allow_zero_copy;
	}
public:
	T *x=nullptr, *y=nullptr, *z=nullptr, *w=nullptr;
	T *s0=nullptr, *s1=nullptr, *s2=nullptr, *s3=nullptr, *s4=nullptr, *s5=nullptr, *s6=nullptr, *s7=nullptr, *s8=nullptr, *s9=nullptr, *sA=nullptr, *sB=nullptr, *sC=nullptr, *sD=nullptr, *sE=nullptr, *sF=nullptr;
	inline Memory(Device& device, const ulong N, const uint dimensions=1u, const bool allocate_host=true, const bool allocate_device=true, const T value=(T)0, const bool allow_zero_copy=true) {
		if(!device.is_initialized()) print_error("No Metal Device selected. Call Device constructor.");
		if(N*(ulong)dimensions==0ull) print_error("Memory size must be larger than 0.");
		this->N = N;
		this->d = dimensions;
		allocate_host_buffer(device, allocate_host, allow_zero_copy);
		allocate_device_buffer(device, allocate_device, allow_zero_copy);
		reset(value);
	}
	inline Memory(Device& device, const ulong N, const uint dimensions, T* const host_buffer, const bool allocate_device=true, const bool allow_zero_copy=true) {
		if(!device.is_initialized()) print_error("No Metal Device selected. Call Device constructor.");
		if(N*(ulong)dimensions==0ull) print_error("Memory size must be larger than 0.");
		this->N = N;
		this->d = dimensions;
		this->host_buffer = host_buffer;
		initialize_auxiliary_pointers();
		host_buffer_exists = true;
		external_host_buffer = true;
		allocate_device_buffer(device, allocate_device, allow_zero_copy);
		write_to_device();
	}
	inline Memory() {}
	inline ~Memory() { delete_buffers(); }
	inline Memory& operator=(Memory&& memory) noexcept {
		delete_buffers();
		N = memory.length();
		d = memory.dimensions();
		device = memory.device;
		if(memory.host_buffer_exists) {
			host_buffer = memory.exchange_host_buffer(nullptr);
			host_buffer_unaligned = memory.exchange_host_buffer_unaligned(nullptr);
			initialize_auxiliary_pointers();
			external_host_buffer = memory.external_host_buffer;
			host_buffer_exists = true;
		}
		if(memory.device_buffer_exists) {
			device_buffer = memory.exchange_device_buffer(nullptr);
			device->info.memory_used += (uint)(capacity()/1048576ull);
			is_zero_copy = memory.is_zero_copy;
			device_buffer_exists = true;
		}
		return *this;
	}
	inline T* const exchange_host_buffer(T* const host_buffer) { T* const swap = this->host_buffer; this->host_buffer = host_buffer; return swap; }
	inline T* const exchange_host_buffer_unaligned(T* const host_buffer_unaligned) { T* const swap = this->host_buffer_unaligned; this->host_buffer_unaligned = host_buffer_unaligned; return swap; }
	inline MetalBuffer* const exchange_device_buffer(MetalBuffer* const buffer) { MetalBuffer* const swap = this->device_buffer; this->device_buffer = buffer; return swap; }
	inline void add_host_buffer() {
		if(!host_buffer_exists&&device_buffer_exists) {
			host_buffer = new T[N*(ulong)d];
			initialize_auxiliary_pointers();
			read_from_device();
			host_buffer_exists = true;
		} else if(!device_buffer_exists) {
			print_error("There is no existing device buffer, so can't add host buffer.");
		}
	}
	inline void add_device_buffer(const bool allow_zero_copy=true) {
		if(!device_buffer_exists&&host_buffer_exists) {
			allocate_device_buffer(*device, true, allow_zero_copy);
			write_to_device();
		} else if(!host_buffer_exists) {
			print_error("There is no existing host buffer, so can't add device buffer.");
		}
	}
	inline void delete_host_buffer() {
		host_buffer_exists = false;
		if(!external_host_buffer) {
			host_buffer = nullptr;
			delete[] host_buffer_unaligned;
		}
		if(!device_buffer_exists) { N = 0ull; d = 1u; }
	}
	inline void delete_device_buffer() {
		if(device_buffer_exists) device->info.memory_used -= (uint)(capacity()/1048576ull);
		device_buffer_exists = false;
		if(device_buffer) metal_release_buffer(device_buffer);
		device_buffer = nullptr;
		if(!host_buffer_exists) { N = 0ull; d = 1u; }
	}
	inline void delete_buffers() { delete_device_buffer(); delete_host_buffer(); }
	inline void reset(const T value=(T)0) {
		if(host_buffer_exists) std::fill(host_buffer, host_buffer+range(), value);
		write_to_device();
	}
	inline const ulong length() const { return N; }
	inline const uint dimensions() const { return d; }
	inline const ulong range() const { return N*(ulong)d; }
	inline const ulong capacity() const { return N*(ulong)d*sizeof(T); }
	inline T* const data() { return host_buffer; }
	inline const T* const data() const { return host_buffer; }
	inline T* const operator()() { return host_buffer; }
	inline const T* const operator()() const { return host_buffer; }
	inline T& operator[](const ulong i) { return host_buffer[i]; }
	inline const T& operator[](const ulong i) const { return host_buffer[i]; }
	inline const T operator()(const ulong i) const { return host_buffer[i]; }
	inline const T operator()(const ulong i, const uint dimension) const { return host_buffer[i+(ulong)dimension*N]; }
	inline void read_from_device(const bool blocking=true, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		if(host_buffer_exists&&device_buffer_exists&&!is_zero_copy) {
			if(blocking&&device) device->finish_queue(); // ensure GPU work completes before CPU readback
			metal_buffer_copy_from(device_buffer, (void*)host_buffer, capacity(), 0ull);
		}
		(void)blocking; (void)event_waitlist; (void)event_returned;
	}
	inline void write_to_device(const bool blocking=true, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		if(host_buffer_exists&&device_buffer_exists&&!is_zero_copy) {
			if(blocking&&device) device->finish_queue(); // avoid CPU writes racing with GPU work
			metal_buffer_copy_to(device_buffer, (void*)host_buffer, capacity(), 0ull);
		}
		(void)blocking; (void)event_waitlist; (void)event_returned;
	}
	inline void read_from_device(const ulong offset, const ulong length, const bool blocking=true, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		if(host_buffer_exists&&device_buffer_exists&&!is_zero_copy) {
			const ulong safe_offset=min(offset, range()), safe_length=min(length, range()-safe_offset);
			if(safe_length>0ull) {
				if(blocking&&device) device->finish_queue();
				metal_buffer_copy_from(device_buffer, (void*)(host_buffer+safe_offset), safe_length*sizeof(T), safe_offset*sizeof(T));
			}
		}
		(void)blocking; (void)event_waitlist; (void)event_returned;
	}
	inline void write_to_device(const ulong offset, const ulong length, const bool blocking=true, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		if(host_buffer_exists&&device_buffer_exists&&!is_zero_copy) {
			const ulong safe_offset=min(offset, range()), safe_length=min(length, range()-safe_offset);
			if(safe_length>0ull) {
				if(blocking&&device) device->finish_queue();
				metal_buffer_copy_to(device_buffer, (void*)(host_buffer+safe_offset), safe_length*sizeof(T), safe_offset*sizeof(T));
			}
		}
		(void)blocking; (void)event_waitlist; (void)event_returned;
	}
	inline void enqueue_read_from_device(const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) { read_from_device(false, event_waitlist, event_returned); }
	inline void enqueue_write_to_device(const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) { write_to_device(false, event_waitlist, event_returned); }
	inline void enqueue_read_from_device(const ulong offset, const ulong length, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) { read_from_device(offset, length, false, event_waitlist, event_returned); }
	inline void enqueue_write_to_device(const ulong offset, const ulong length, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) { write_to_device(offset, length, false, event_waitlist, event_returned); }
	inline void finish_queue() { if(device) device->finish_queue(); }
	inline MetalBuffer* get_metal_buffer() const { return device_buffer; }
};

class Kernel {
private:
	ulong N = 0ull;
	uint number_of_parameters = 0u;
	string name = "";
	MetalPipeline* pipeline = nullptr;
	Device* device = nullptr;
	uint threads_per_group = WORKGROUP_SIZE;
	vector<MetalArg> args;
	template<typename T> inline void link_parameter(const uint position, const Memory<T>& memory) {
		if(position>=args.size()) args.resize(position+1u);
		args[position].kind = MetalArg::Kind::Buffer;
		args[position].buffer = memory.get_metal_buffer();
	}
	template<typename T> inline void link_parameter(const uint position, const T& constant) {
		if(position>=args.size()) args.resize(position+1u);
		args[position].kind = MetalArg::Kind::Bytes;
		args[position].bytes.resize(sizeof(T));
		memcpy(args[position].bytes.data(), &constant, sizeof(T));
	}
	inline void link_parameters(const uint starting_position) { number_of_parameters = max(number_of_parameters, starting_position); }
	template<class T, class... U> inline void link_parameters(const uint starting_position, const T& parameter, const U&... parameters) {
		link_parameter(starting_position, parameter);
		link_parameters(starting_position+1u, parameters...);
	}
public:
	template<class... T> inline Kernel(const Device& device, const ulong N, const string& name, const T&... parameters) {
		if(!device.is_initialized()) print_error("No Metal Device selected. Call Device constructor.");
		this->device = (Device*)&device;
		this->name = name;
		pipeline = metal_create_pipeline(device.get_context(), name.c_str());
		link_parameters(0u, parameters...);
		set_ranges(N);
	}
	template<class... T> inline Kernel(const Device& device, const ulong N, const uint workgroup_size, const string& name, const T&... parameters) {
		if(!device.is_initialized()) print_error("No Metal Device selected. Call Device constructor.");
		this->device = (Device*)&device;
		this->name = name;
		pipeline = metal_create_pipeline(device.get_context(), name.c_str());
		link_parameters(0u, parameters...);
		set_ranges(N, (ulong)workgroup_size);
	}
	inline Kernel() {}
	inline Kernel& set_ranges(const ulong N, const ulong workgroup_size=(ulong)WORKGROUP_SIZE) {
		this->N = N;
		this->threads_per_group = (uint)workgroup_size;
		return *this;
	}
	inline const ulong range() const { return N; }
	inline uint get_number_of_parameters() const { return number_of_parameters; }
	template<class... T> inline Kernel& add_parameters(const T&... parameters) {
		link_parameters(number_of_parameters, parameters...);
		return *this;
	}
	template<class... T> inline Kernel& set_parameters(const uint starting_position, const T&... parameters) {
		link_parameters(starting_position, parameters...);
		return *this;
	}
	inline Kernel& enqueue_run(const uint t=1u, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		for(uint i=0u; i<t; i++) {
			metal_dispatch(device->get_context(), pipeline, N, threads_per_group, args.data(), (uint)args.size());
		}
		(void)event_waitlist; (void)event_returned;
		return *this;
	}
	inline Kernel& run(const uint t=1u, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		enqueue_run(t, event_waitlist, event_returned);
		finish_queue();
		return *this;
	}
	inline Kernel& operator()(const uint t=1u, const vector<Event>* event_waitlist=nullptr, Event* event_returned=nullptr) {
		return run(t, event_waitlist, event_returned);
	}
	inline Kernel& finish_queue() {
		if(device) device->finish_queue();
		return *this;
	}
};
