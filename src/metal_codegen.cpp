#include "metal.hpp"
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <string_view>
#include <array>

namespace {

using std::string_view;

constexpr bool metal_is_identifier_char(const char c) {
	return (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';
}

constexpr bool metal_is_ws(const char c) {
	return c==' '||c=='\t'||c=='\n'||c=='\r';
}

string metal_strip_opencl_pragmas(const string& src) {
	string out;
	out.reserve(src.size());
	size_t i = 0u;
	while(i<src.size()) {
		size_t line_end = src.find('\n', i);
		if(line_end==string::npos) line_end = src.size();
		const string_view line(src.data()+i, line_end-i);
		if(!(line.rfind("#pragma OPENCL", 0u)==0u)) {
			out.append(line);
			out.push_back('\n');
		}
		i = line_end+1u;
	}
	return out;
}

struct MetalStringHash {
	using is_transparent = void;
	inline size_t operator()(const string_view s) const noexcept {
		// FNV-1a to avoid libc++'s internal __hash_memory symbol.
		uint64_t h = 1469598103934665603ull;
		for(unsigned char c : s) {
			h ^= (uint64_t)c;
			h *= 1099511628211ull;
		}
		return (size_t)h;
	}
	inline size_t operator()(const string& s) const noexcept { return operator()(string_view(s)); }
};

struct MetalStringEq {
	using is_transparent = void;
	inline bool operator()(const string_view a, const string_view b) const noexcept { return a==b; }
	inline bool operator()(const string& a, const string& b) const noexcept { return a==b; }
	inline bool operator()(const string& a, const string_view b) const noexcept { return string_view(a)==b; }
	inline bool operator()(const string_view a, const string& b) const noexcept { return a==string_view(b); }
};

struct MetalDefineSet {
	std::unordered_set<string, MetalStringHash, MetalStringEq> names;
	inline void add(const string& name) { names.insert(name); }
	inline bool is_defined(const string_view name) const { return names.find(name)!=names.end(); }
};

MetalDefineSet metal_collect_defines(const string& device_defines, const string& graphics_defines) {
	MetalDefineSet defs;
	auto add_from = [&](const string& src) {
		size_t i = 0u;
		while(i<src.size()) {
			size_t line_end = src.find('\n', i);
			if(line_end==string::npos) line_end = src.size();
			const string_view line(src.data()+i, line_end-i);
			size_t p = line.find_first_not_of(" \t\r");
			if(p!=string_view::npos && line.compare(p, 7u, "#define")==0u) {
				size_t name_start = line.find_first_not_of(" \t", p+7u);
				if(name_start!=string_view::npos) {
					size_t name_end = name_start;
					while(name_end<line.size() && metal_is_identifier_char(line[name_end])) name_end++;
					if(name_end>name_start) defs.add(string(line.substr(name_start, name_end-name_start)));
				}
			}
			i = line_end+1u;
		}
	};
	add_from(device_defines);
	add_from(graphics_defines);
	return defs;
}

string_view metal_trim_view(const string_view s) {
	size_t b = s.find_first_not_of(" \t\n\r");
	if(b==string_view::npos) return {};
	size_t e = s.find_last_not_of(" \t\n\r");
	return s.substr(b, e-b+1u);
}

string metal_trim(const string& s) {
	const string_view v = metal_trim_view(s);
	return string(v);
}

string metal_reflow_opencl_source(const string& src) {
	string out;
	out.reserve(src.size()+1024u);
	size_t i = 0u;
	while(i<src.size()) {
		size_t line_end = src.find('\n', i);
		if(line_end==string::npos) line_end = src.size();
		string_view line = metal_trim_view(string_view(src.data()+i, line_end-i));
		i = (line_end==src.size()) ? src.size() : line_end+1u;
		if(line.empty()) continue;
		if(line[0]=='#') {
			out.append(line);
			out.push_back('\n');
			continue;
		}
		string token(line);
		string punct;
		while(!token.empty()) {
			const char c = token.back();
			if(c==';' || c=='{' || c=='}') {
				punct.push_back(c);
				token.pop_back();
			} else {
				break;
			}
		}
		token = metal_trim(token);
		if(!token.empty()) {
			if(!out.empty() && out.back()!='\n' && out.back()!=' ') out.push_back(' ');
			out += token;
		}
		for(size_t p=0u; p<punct.size(); p++) {
			const char c = punct[punct.size()-1u-p];
			out.push_back(c);
			out.push_back('\n');
		}
	}
	return out;
}

size_t metal_skip_ws_and_comments(const string& src, size_t i) {
	while(i<src.size()) {
		if(metal_is_ws(src[i])) {
			i++;
			continue;
		}
		if(i+1u<src.size() && src[i]=='/' && src[i+1u]=='/') {
			size_t line_end = src.find('\n', i+2u);
			if(line_end==string::npos) return src.size();
			i = line_end+1u;
			continue;
		}
		if(i+1u<src.size() && src[i]=='/' && src[i+1u]=='*') {
			size_t end = src.find("*/", i+2u);
			if(end==string::npos) return src.size();
			i = end+2u;
			continue;
		}
		break;
	}
	return i;
}

size_t metal_find_kernel_void(const string& src, size_t start, size_t& void_end) {
	size_t i = start;
	while(i<src.size()) {
		i = metal_skip_ws_and_comments(src, i);
		if(i>=src.size()) break;
		if(!metal_is_identifier_char(src[i])) {
			i++;
			continue;
		}
		size_t j = i;
		while(j<src.size() && metal_is_identifier_char(src[j])) j++;
		const string tok = src.substr(i, j-i);
		if(tok=="kernel"||tok=="__kernel") {
			size_t k = metal_skip_ws_and_comments(src, j);
			while(k<src.size()) {
				if(!metal_is_identifier_char(src[k])) {
					k = metal_skip_ws_and_comments(src, k+1u);
					continue;
				}
				size_t l = k;
				while(l<src.size() && metal_is_identifier_char(src[l])) l++;
				const string next = src.substr(k, l-k);
				if(next=="void") {
					void_end = l;
					return i;
				}
				if(next=="__attribute__") {
					size_t a = metal_skip_ws_and_comments(src, l);
					if(a<src.size() && src[a]=='(') {
						int depth = 1;
						a++;
						while(a<src.size() && depth>0) {
							if(src[a]=='(') depth++;
							else if(src[a]==')') depth--;
							a++;
						}
						k = metal_skip_ws_and_comments(src, a);
						continue;
					}
				}
				break;
			}
		}
		i = j;
	}
	return string::npos;
}

string metal_normalize_directives(const string& src) {
	string out;
	out.reserve(src.size()+64u);
	bool line_start = true;
	bool in_line_comment = false;
	bool in_block_comment = false;
	for(size_t i=0u; i<src.size(); i++) {
		const char c = src[i];
		if(in_line_comment) {
			out.push_back(c);
			if(c=='\n') {
				in_line_comment = false;
				line_start = true;
			}
			continue;
		}
		if(in_block_comment) {
			out.push_back(c);
			if(c=='*' && i+1u<src.size() && src[i+1u]=='/') {
				out.push_back('/');
				i++;
				in_block_comment = false;
			}
			if(c=='\n') line_start = true;
			continue;
		}
		if(c=='/' && i+1u<src.size()) {
			if(src[i+1u]=='/') {
				out.push_back('/');
				out.push_back('/');
				i++;
				in_line_comment = true;
				line_start = false;
				continue;
			}
			if(src[i+1u]=='*') {
				out.push_back('/');
				out.push_back('*');
				i++;
				in_block_comment = true;
				line_start = false;
				continue;
			}
		}
		if(c=='#') {
			if(!line_start) out.push_back('\n');
			out.push_back('#');
			line_start = false;
			continue;
		}
		out.push_back(c);
		if(c=='\n') line_start = true;
		else if(c!=' ' && c!='\t' && c!='\r') line_start = false;
	}
	return out;
}

string metal_collapse_ws(const string& s) {
	string out;
	out.reserve(s.size());
	bool was_space = false;
	for(char c : s) {
		if(c==' '||c=='\t'||c=='\n'||c=='\r') {
			if(!was_space) out.push_back(' ');
			was_space = true;
		} else {
			out.push_back(c);
			was_space = false;
		}
	}
	return metal_trim(out);
}

string metal_rewrite_kernel_param(const string& raw_param, const uint buffer_index) {
	string param = metal_collapse_ws(raw_param);
	if(param.empty()) return param;
	const bool is_pointer = (param.find('*')!=string::npos);
	if(is_pointer) {
		return param+" [[buffer("+to_string(buffer_index)+")]]";
	}
	// scalar parameter: convert to constant reference
	size_t name_end = param.size();
	while(name_end>0u && !metal_is_identifier_char(param[name_end-1u])) name_end--;
	size_t name_start = name_end;
	while(name_start>0u && metal_is_identifier_char(param[name_start-1u])) name_start--;
	if(name_start==name_end) return param;
	string name = param.substr(name_start, name_end-name_start);
	string type_part = metal_trim(param.substr(0u, name_start));
	// strip common qualifiers
	string type_clean;
	type_clean.reserve(type_part.size());
	size_t i = 0u;
	while(i<type_part.size()) {
		while(i<type_part.size() && (type_part[i]==' '||type_part[i]=='\t')) i++;
		size_t j = i;
		while(j<type_part.size() && type_part[j]!=' ' && type_part[j]!='\t') j++;
		string tok = type_part.substr(i, j-i);
		if(tok!="const" && tok!="volatile" && tok!="restrict" && tok!="__restrict") {
			if(!type_clean.empty()) type_clean.push_back(' ');
			type_clean += tok;
		}
		i = j;
	}
	if(type_clean.empty()) return param;
	return "constant "+type_clean+"& "+name+" [[buffer("+to_string(buffer_index)+")]]";
}

string metal_rewrite_kernel_signatures(const string& src, const MetalDefineSet& defs) {
	string out;
	out.reserve(src.size()+1024u);
	size_t i = 0u;
	while(true) {
		size_t void_end = 0u;
		const size_t k = metal_find_kernel_void(src, i, void_end);
		if(k==string::npos) {
			out += src.substr(i);
			break;
		}
		const size_t paren = src.find('(', void_end);
		if(paren==string::npos) { out += src.substr(i); break; }
		out += src.substr(i, paren+1u-i);
		size_t depth = 1u;
		size_t j = paren+1u;
		for(; j<src.size(); j++) {
			const char c = src[j];
			if(c=='(') depth++;
			else if(c==')') { if(--depth==0u) break; }
		}
		string_view params = string_view(src).substr(paren+1u, j-(paren+1u));

		struct Cond { bool parent_active; bool condition; bool active; };
		vector<Cond> stack;
		auto current_active = [&]() -> bool {
			return stack.empty() ? true : stack.back().active;
		};
		auto is_defined = [&](const string_view name) -> bool {
			return defs.is_defined(name);
		};
		string current_param;
		vector<string> rewritten_params;
		uint buffer_index = 0u;

		auto flush_param = [&]() {
			string trimmed = metal_trim(current_param);
			if(!trimmed.empty() && current_active()) {
				rewritten_params.push_back(metal_rewrite_kernel_param(trimmed, buffer_index++));
			}
			current_param.clear();
		};

		size_t p = 0u;
		while(p<params.size()) {
			// handle line-based preprocessor directives
			if((p==0u || params[p-1u]=='\n')) {
				size_t line_start = p;
				size_t non_ws = params.find_first_not_of(" \t\r", line_start);
				if(non_ws!=string_view::npos && (non_ws==line_start || params[non_ws-1u]=='\n') && params[non_ws]=='#') {
					size_t line_end = params.find('\n', non_ws);
					if(line_end==string_view::npos) line_end = params.size();
					const string_view line = metal_trim_view(params.substr(non_ws, line_end-non_ws));
					if(line.rfind("#ifdef", 0u)==0u) {
						const string_view name = metal_trim_view(line.substr(6u));
						const bool parent_active = current_active();
						const bool condition = is_defined(name);
						stack.push_back({parent_active, condition, parent_active && condition});
					} else if(line.rfind("#ifndef", 0u)==0u) {
						const string_view name = metal_trim_view(line.substr(7u));
						const bool parent_active = current_active();
						const bool condition = !is_defined(name);
						stack.push_back({parent_active, condition, parent_active && condition});
					} else if(line.rfind("#else", 0u)==0u) {
						if(!stack.empty()) {
							Cond& top = stack.back();
							top.active = top.parent_active && !top.condition;
						}
					} else if(line.rfind("#endif", 0u)==0u) {
						if(!stack.empty()) stack.pop_back();
					} else if(line.rfind("#if", 0u)==0u || line.rfind("#elif", 0u)==0u) {
						// minimal handling of "#if defined(NAME)" forms
						const bool is_elif = (line.rfind("#elif", 0u)==0u);
						const string_view expr = metal_trim_view(line.substr(is_elif ? 5u : 3u));
						bool value = false;
						const size_t def_pos = expr.find("defined");
						if(def_pos!=string_view::npos) {
							size_t name_start = expr.find('(', def_pos);
							size_t name_end = expr.find(')', name_start==string_view::npos ? 0u : name_start+1u);
							if(name_start!=string_view::npos && name_end!=string_view::npos && name_end>name_start+1u) {
								const string_view name = metal_trim_view(expr.substr(name_start+1u, name_end-name_start-1u));
								value = is_defined(name);
								if(expr.find("!defined")!=string_view::npos) value = !value;
							}
						}
						if(is_elif) {
							if(!stack.empty()) {
								Cond& top = stack.back();
								top.condition = value;
								top.active = top.parent_active && value;
							}
						} else {
							const bool parent_active = current_active();
							stack.push_back({parent_active, value, parent_active && value});
						}
					}
					p = (line_end==params.size()) ? params.size() : line_end+1u;
					continue;
				}
			}

			if(!current_active()) {
				p++;
				continue;
			}

			// skip line comments
			if(p+1u<params.size() && params[p]=='/' && params[p+1u]=='/') {
				size_t line_end = params.find('\n', p);
				if(line_end==string_view::npos) break;
				p = line_end+1u;
				continue;
			}
			// skip block comments
			if(p+1u<params.size() && params[p]=='/' && params[p+1u]=='*') {
				size_t end = params.find("*/", p+2u);
				if(end==string_view::npos) break;
				p = end+2u;
				continue;
			}

			if(params[p]==',') {
				flush_param();
				p++;
				continue;
			}
			current_param.push_back(params[p]);
			p++;
		}
		flush_param();

		string joined;
		for(size_t idx=0u; idx<rewritten_params.size(); idx++) {
			if(idx>0u) joined += ", ";
			joined += rewritten_params[idx];
		}
		if(!joined.empty()) joined += ", ";
		joined += "uint3 mtl_gid [[thread_position_in_grid]], uint3 mtl_lid [[thread_position_in_threadgroup]], uint3 mtl_tgid [[threadgroup_position_in_grid]], uint3 mtl_tpg [[threads_per_threadgroup]]";
		out += joined;
		out += ")";
		i = j+1u;
	}
	return out;
}

string metal_convert_vector_constructs(const string& src) {
	static constexpr std::array<string_view, 27> types = {{
		"float2","float3","float4","int2","int3","int4","uint2","uint3","uint4",
		"short2","short3","short4","ushort2","ushort3","ushort4",
		"char2","char3","char4","uchar2","uchar3","uchar4",
		"half2","half3","half4"
	}};
	string out = src;
	for(const string_view type : types) {
		const size_t type_len = type.size();
		const string pattern = "("+string(type)+")";
		size_t pos = 0u;
		while(true) {
			pos = out.find(pattern, pos);
			if(pos==string::npos) break;
			size_t next = pos+pattern.size();
			while(next<out.size() && (out[next]==' '||out[next]=='\t'||out[next]=='\n'||out[next]=='\r')) next++;
			if(next<out.size() && out[next]=='(') {
				out.replace(pos, pattern.size(), type.data(), type_len);
				pos += type_len;
			} else {
				pos = next;
			}
		}
	}
	return out;
}

string metal_rename_reflect_refract(const string& src) {
	string out;
	out.reserve(src.size());
	for(size_t i=0u; i<src.size();) {
		if(i+7u<=src.size() && src.compare(i, 7u, "reflect")==0u) {
			const bool ok = (i==0u || !metal_is_identifier_char(src[i-1u])) && (i+7u<src.size() && src[i+7u]=='(');
			if(ok) { out += "reflect_cl"; i += 7u; continue; }
		}
		if(i+7u<=src.size() && src.compare(i, 7u, "refract")==0u) {
			const bool ok = (i==0u || !metal_is_identifier_char(src[i-1u])) && (i+7u<src.size() && src[i+7u]=='(');
			if(ok) { out += "refract_cl"; i += 7u; continue; }
		}
		out.push_back(src[i++]);
	}
	return out;
}

bool metal_is_address_space(const string_view tok) {
	static constexpr std::array<string_view, 11> spaces = {{
		"global","local","constant","private","device","thread","threadgroup",
		"__global","__local","__constant","__private"
	}};
	for(const auto s : spaces) {
		if(tok==s) return true;
	}
	return false;
}

string metal_replace_opencl_keywords(const string& src) {
	string out;
	out.reserve(src.size());
	size_t i = 0u;
	while(i<src.size()) {
		if(!metal_is_identifier_char(src[i])) {
			out.push_back(src[i++]);
			continue;
		}
		size_t j = i;
		while(j<src.size() && metal_is_identifier_char(src[j])) j++;
		const string_view tok(src.data()+i, j-i);
		if(tok=="global" || tok=="__global") out += "device";
		else if(tok=="local" || tok=="__local") out += "threadgroup";
		else if(tok=="private" || tok=="__private") out += "thread";
		else if(tok=="constant" || tok=="__constant") out += "constant";
		else if(tok=="__kernel") out += "kernel";
		else out.append(tok);
		i = j;
	}
	return out;
}

bool metal_is_type_token(const string_view tok) {
	static constexpr std::array<string_view, 40> types = {{
		"void","bool","char","uchar","short","ushort","int","uint","long","ulong",
		"float","half","float2","float3","float4","int2","int3","int4","uint2","uint3","uint4",
		"short2","short3","short4","ushort2","ushort3","ushort4","char2","char3","char4",
		"uchar2","uchar3","uchar4","half2","half3","half4",
		"ray","uxx","fpxx"
	}};
	for(const auto t : types) {
		if(tok==t) return true;
	}
	return false;
}

string metal_add_thread_qualifiers(const string& src) {
	string out;
	out.reserve(src.size()+1024u);
	size_t i = 0u;
	while(i<src.size()) {
		if(!metal_is_identifier_char(src[i])) {
			out.push_back(src[i++]);
			continue;
		}
		size_t j = i;
		while(j<src.size() && metal_is_identifier_char(src[j])) j++;
		const string_view tok(src.data()+i, j-i);
		bool inject_thread = false;
		if(metal_is_type_token(tok)) {
			size_t k = j;
			while(k<src.size() && (src[k]==' '||src[k]=='\t'||src[k]=='\n'||src[k]=='\r')) k++;
			if(k<src.size() && src[k]=='*') {
				bool has_address = false;
				size_t back = i;
				while(back>0u) {
					size_t b = back;
					while(b>0u && (src[b-1u]==' '||src[b-1u]=='\t'||src[b-1u]=='\n'||src[b-1u]=='\r')) b--;
					if(b==0u) break;
					char c = src[b-1u];
					if(metal_is_identifier_char(c)) {
						size_t t_end = b;
						size_t t_start = t_end;
						while(t_start>0u && metal_is_identifier_char(src[t_start-1u])) t_start--;
						const string_view prev(src.data()+t_start, t_end-t_start);
						if(prev=="const"||prev=="volatile"||prev=="restrict"||prev=="__restrict") {
							back = t_start;
							continue;
						}
						if(metal_is_address_space(prev)) { has_address = true; break; }
						break;
					} else {
						if(c=='('||c==','||c==';'||c=='{'||c=='}'||c==')'||c=='=') break;
						back = b-1u;
					}
				}
				if(!has_address) inject_thread = true;
			}
		}
		if(inject_thread) out += "thread ";
		out.append(tok);
		i = j;
	}
	return out;
}

} // namespace

string get_metal_msl_code(const string& device_defines, const string& graphics_defines) {
	static std::mutex cache_mutex;
	static std::unordered_map<string, string, MetalStringHash, MetalStringEq> cache;
	const string cache_key = device_defines + "\n" + graphics_defines;
	{
		std::lock_guard<std::mutex> lock(cache_mutex);
		auto it = cache.find(cache_key);
		if(it != cache.end()) return it->second;
	}
	string code = get_opencl_c_code_string();
	code = metal_reflow_opencl_source(code);
	code = metal_normalize_directives(code);
	code = metal_strip_opencl_pragmas(code);
	const MetalDefineSet defs = metal_collect_defines(device_defines, graphics_defines);
	code = metal_rename_reflect_refract(code);
	code = metal_convert_vector_constructs(code);
	code = metal_rewrite_kernel_signatures(code, defs);
	code = metal_replace_opencl_keywords(code);
	code = metal_add_thread_qualifiers(code);
	code = replace(code, "vertex[", "mc_vertex[");
	code = replace(code, "mc_mc_vertex", "mc_vertex");
	const string compat = R"(
		#include <metal_stdlib>
		using namespace metal;
		#define barrier(x) threadgroup_barrier(mem_flags::mem_threadgroup)
		#define CLK_LOCAL_MEM_FENCE 0
		#define CLK_GLOBAL_MEM_FENCE 0
		#define get_global_id(dim) (mtl_gid[(dim)])
		#define get_local_id(dim) (mtl_lid[(dim)])
		#define get_group_id(dim) (mtl_tgid[(dim)])
		#define get_local_size(dim) (mtl_tpg[(dim)])
		inline uint as_uint(float x) { return as_type<uint>(x); }
		inline int as_int(float x) { return as_type<int>(x); }
		inline float as_float(uint x) { return as_type<float>(x); }
		inline float as_float(int x) { return as_type<float>(x); }
		#define as_ushort(x) ((ushort)(x))
		inline uchar4 as_uchar4(int x) { return as_type<uchar4>(x); }
		inline int as_int(uchar4 x) { return as_type<int>(x); }
		inline uint atomic_add(volatile device uint* p, uint v) { return atomic_fetch_add_explicit((device atomic_uint*)p, v, memory_order_relaxed); }
		inline int atomic_add(volatile device int* p, int v) { return atomic_fetch_add_explicit((device atomic_int*)p, v, memory_order_relaxed); }
		inline uint atomic_xchg(volatile device uint* p, uint v) { return atomic_exchange_explicit((device atomic_uint*)p, v, memory_order_relaxed); }
		inline int atomic_xchg(volatile device int* p, int v) { return atomic_exchange_explicit((device atomic_int*)p, v, memory_order_relaxed); }
		inline uint atomic_cmpxchg(volatile device uint* p, uint c, uint v) { atomic_compare_exchange_weak_explicit((device atomic_uint*)p, &c, v, memory_order_relaxed, memory_order_relaxed); return c; }
		inline int atomic_cmpxchg(volatile device int* p, int c, int v) { atomic_compare_exchange_weak_explicit((device atomic_int*)p, &c, v, memory_order_relaxed, memory_order_relaxed); return c; }
		inline uint atomic_max(volatile device uint* p, uint v) {
			uint old = atomic_load_explicit((device atomic_uint*)p, memory_order_relaxed);
			while(old<v && !atomic_compare_exchange_weak_explicit((device atomic_uint*)p, &old, v, memory_order_relaxed, memory_order_relaxed)) {}
			return old;
		}
		inline int atomic_max(volatile device int* p, int v) {
			int old = atomic_load_explicit((device atomic_int*)p, memory_order_relaxed);
			while(old<v && !atomic_compare_exchange_weak_explicit((device atomic_int*)p, &old, v, memory_order_relaxed, memory_order_relaxed)) {}
			return old;
		}
		inline float acospi(float x) { return acos(x)*0.3183098861837907f; }
		inline float vload_half(uint offset, device const half* p) { return (float)p[offset]; }
		inline float vload_half(uint offset, constant const half* p) { return (float)p[offset]; }
		inline float vload_half(uint offset, thread const half* p) { return (float)p[offset]; }
		inline void vstore_half_rte(float x, uint offset, device half* p) { p[offset] = (half)x; }
		inline void vstore_half_rte(float x, uint offset, thread half* p) { p[offset] = (half)x; }
	)";
	const string full = compat+"\n"+device_defines+"\n"+graphics_defines+"\n"+code;
	write_file("bin/metal.msl", full);
	{
		std::lock_guard<std::mutex> lock(cache_mutex);
		cache.emplace(cache_key, full);
	}
	return full;
}
