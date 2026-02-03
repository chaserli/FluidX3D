cmake_minimum_required(VERSION 3.20)
project(FluidX3D LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_GRAPHICS "Enable GRAPHICS support" ON)
option(ENABLE_INTERACTIVE_GRAPHICS "Enable INTERACTIVE_GRAPHICS support" ON)
option(ENABLE_INTERACTIVE_GRAPHICS_ASCII "Enable INTERACTIVE_GRAPHICS_ASCII support" OFF)

set(SRC_CPP
    src/graphics.cpp
    src/info.cpp
    src/kernel.cpp
    src/lbm.cpp
    src/lodepng.cpp
    src/main.cpp
    src/metal_codegen.cpp
    src/setup.cpp
    src/shapes.cpp
)

set(SRC_MM
    src/graphics_macos.mm
    src/metal.mm
)

set(SOURCES ${SRC_CPP} ${SRC_MM})

set(COMMON_COMPILE_OPTIONS -O -pthread -Wno-comment -stdlib=libc++)
set(COMMON_OBJCXX_OPTIONS -fobjc-arc)
set(COMMON_LINK_LIBS
    "-framework Cocoa"
    "-framework CoreGraphics"
    "-framework Metal"
)

function(add_setup_target target_name setup_macro graphics_mode required_defs)
    if(graphics_mode STREQUAL "INTERACTIVE")
        if(NOT ENABLE_INTERACTIVE_GRAPHICS)
            return()
        endif()
    elseif(graphics_mode STREQUAL "GRAPHICS")
        if(NOT ENABLE_GRAPHICS)
            return()
        endif()
    elseif(graphics_mode STREQUAL "GRAPHICS_OR_INTERACTIVE")
        if(NOT ENABLE_GRAPHICS AND NOT ENABLE_INTERACTIVE_GRAPHICS)
            return()
        endif()
    endif()

    add_executable(${target_name} ${SOURCES})
    target_compile_options(${target_name} PRIVATE ${COMMON_COMPILE_OPTIONS})
    target_compile_options(${target_name} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:${COMMON_OBJCXX_OPTIONS}>)
    target_link_libraries(${target_name} PRIVATE ${COMMON_LINK_LIBS})
    target_compile_definitions(${target_name} PRIVATE ${setup_macro} ${required_defs})
    list(FIND required_defs "D2Q9" has_d2q9)
    list(FIND required_defs "D3Q15" has_d3q15)
    list(FIND required_defs "D3Q27" has_d3q27)
    if(has_d2q9 EQUAL -1 AND has_d3q15 EQUAL -1 AND has_d3q27 EQUAL -1)
        target_compile_definitions(${target_name} PRIVATE D3Q19)
    endif()
    list(FIND required_defs "SRT" has_srt)
    list(FIND required_defs "TRT" has_trt)
    if(has_srt EQUAL -1 AND has_trt EQUAL -1)
        target_compile_definitions(${target_name} PRIVATE SRT)
    endif()
    if(graphics_mode STREQUAL "INTERACTIVE")
        target_compile_definitions(${target_name} PRIVATE INTERACTIVE_GRAPHICS)
    elseif(graphics_mode STREQUAL "GRAPHICS")
        target_compile_definitions(${target_name} PRIVATE GRAPHICS)
    elseif(graphics_mode STREQUAL "GRAPHICS_OR_INTERACTIVE")
        if(ENABLE_INTERACTIVE_GRAPHICS)
            target_compile_definitions(${target_name} PRIVATE INTERACTIVE_GRAPHICS)
        else()
            target_compile_definitions(${target_name} PRIVATE GRAPHICS)
        endif()
    elseif(graphics_mode STREQUAL "OPTIONAL_INTERACTIVE")
        if(ENABLE_INTERACTIVE_GRAPHICS)
            target_compile_definitions(${target_name} PRIVATE INTERACTIVE_GRAPHICS)
        endif()
    endif()
    if(ENABLE_INTERACTIVE_GRAPHICS_ASCII)
        target_compile_definitions(${target_name} PRIVATE INTERACTIVE_GRAPHICS_ASCII)
    endif()
endfunction()

set(SETUP_SPECS
    "benchmark|SETUP_BENCHMARK|NONE|BENCHMARK"
    "3d-taylor-green-vortices|SETUP_3D_TAYLOR_GREEN_VORTICES|INTERACTIVE|"
    "2d-taylor-green-vortices-use-d2q9|SETUP_2D_TAYLOR_GREEN_VORTICES_USE_D2Q9|INTERACTIVE|D2Q9"
    "poiseuille-flow-validation|SETUP_POISEUILLE_FLOW_VALIDATION|NONE|VOLUME_FORCE"
    "stokes-drag-validation|SETUP_STOKES_DRAG_VALIDATION|NONE|FORCE_FIELD,EQUILIBRIUM_BOUNDARIES"
    "cylinder-in-rectangular-duct|SETUP_CYLINDER_IN_RECTANGULAR_DUCT|INTERACTIVE|VOLUME_FORCE"
    "taylor-couette-flow|SETUP_TAYLOR_COUETTE_FLOW|INTERACTIVE|MOVING_BOUNDARIES"
    "lid-driven-cavity|SETUP_LID_DRIVEN_CAVITY|INTERACTIVE|MOVING_BOUNDARIES"
    "2d-karman-vortex-street|SETUP_2D_KARMAN_VORTEX_STREET|INTERACTIVE|D2Q9,FP16S,EQUILIBRIUM_BOUNDARIES"
    "particle-test|SETUP_PARTICLE_TEST|INTERACTIVE|VOLUME_FORCE,FORCE_FIELD,MOVING_BOUNDARIES,PARTICLES"
    "delta-wing|SETUP_DELTA_WING|INTERACTIVE|FP16S,EQUILIBRIUM_BOUNDARIES,SUBGRID"
    "hydraulic-jump|SETUP_HYDRAULIC_JUMP|INTERACTIVE|FP16S,VOLUME_FORCE,EQUILIBRIUM_BOUNDARIES,MOVING_BOUNDARIES,SURFACE,SUBGRID"
    "dam-break|SETUP_DAM_BREAK|INTERACTIVE|FP16S,VOLUME_FORCE,SURFACE"
    "liquid-metal-on-a-speaker|SETUP_LIQUID_METAL_ON_A_SPEAKER|INTERACTIVE|FP16S,VOLUME_FORCE,MOVING_BOUNDARIES,SURFACE"
    "breaking-waves-on-beach|SETUP_BREAKING_WAVES_ON_BEACH|INTERACTIVE|FP16S,VOLUME_FORCE,EQUILIBRIUM_BOUNDARIES,SURFACE"
    "river|SETUP_RIVER|INTERACTIVE|FP16S,VOLUME_FORCE,SURFACE"
    "raindrop-impact|SETUP_RAINDROP_IMPACT|GRAPHICS_OR_INTERACTIVE|FP16C,VOLUME_FORCE,EQUILIBRIUM_BOUNDARIES,SURFACE"
    "bursting-bubble|SETUP_BURSTING_BUBBLE|INTERACTIVE|FP16C,VOLUME_FORCE,SURFACE"
    "cube-with-changing-gravity|SETUP_CUBE_WITH_CHANGING_GRAVITY|INTERACTIVE|FP16S,VOLUME_FORCE,SURFACE"
    "periodic-faucet-mass-conservation-test|SETUP_PERIODIC_FAUCET_MASS_CONSERVATION_TEST|INTERACTIVE|FP16S,VOLUME_FORCE,SURFACE"
    "two-colliding-droplets-in-force-field|SETUP_TWO_COLLIDING_DROPLETS_IN_FORCE_FIELD|INTERACTIVE|FP16S,VOLUME_FORCE,FORCE_FIELD,SURFACE"
    "rayleigh-benard-convection|SETUP_RAYLEIGH_BENARD_CONVECTION|INTERACTIVE|FP16S,VOLUME_FORCE,TEMPERATURE"
    "thermal-convection|SETUP_THERMAL_CONVECTION|INTERACTIVE|FP16S,VOLUME_FORCE,TEMPERATURE"
)

foreach(spec IN LISTS SETUP_SPECS)
    string(REPLACE "|" ";" fields ${spec})
    list(GET fields 0 target)
    list(GET fields 1 macro)
    list(GET fields 2 graphics_mode)
    list(GET fields 3 defs_csv)
    if(defs_csv STREQUAL "")
        set(defs_list "")
    else()
        string(REPLACE "," ";" defs_list ${defs_csv})
    endif()
    add_setup_target("${target}" ${macro} ${graphics_mode} "${defs_list}")
endforeach()
