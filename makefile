SHELL := /bin/sh

CXX := clang++
CXXFLAGS := -std=c++23 -O -pthread -Wno-comment -stdlib=libc++
OBJCXXFLAGS := $(CXXFLAGS) -fobjc-arc
LDFLAGS := -framework Cocoa -framework CoreGraphics -framework Metal

SRC_CPP := src/graphics.cpp src/info.cpp src/kernel.cpp src/lbm.cpp src/lodepng.cpp src/main.cpp src/metal_codegen.cpp src/setup.cpp src/shapes.cpp
SRC_MM := src/graphics_macos.mm src/metal.mm

OBJ_CPP := $(SRC_CPP:src/%.cpp=temp/%.o)
OBJ_MM := $(SRC_MM:src/%.mm=temp/%.o)
OBJS := $(OBJ_CPP) $(OBJ_MM)

.PHONY: all clean

all: bin/FluidX3D

bin/FluidX3D: $(OBJS)
	@mkdir -p bin
	$(CXX) $(OBJS) -o $@ $(CXXFLAGS) $(LDFLAGS)

temp/%.o: src/%.cpp
	@mkdir -p temp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

temp/%.o: src/%.mm
	@mkdir -p temp
	$(CXX) -c $< -o $@ $(OBJCXXFLAGS)

clean:
	@rm -rf temp bin
