cmake_minimum_required(VERSION 3.20)
project(PetNES CXX)
set(CMAKE_CXX_STANDARD 20)

add_library(petnes-core STATIC
    src/core/cartridge.cpp
    src/core/mapper.cpp
    src/core/controller.cpp
    src/core/bus.cpp
    src/core/cpu.cpp
    src/core/ppu.cpp
    src/core/apu.cpp
    src/core/nes.cpp
)
target_include_directories(petnes-core PUBLIC src/core)

if(EMSCRIPTEN)
    add_executable(petnes-wasm src/bindings.cpp)
    target_link_libraries(petnes-wasm PRIVATE petnes-core)
    set_target_properties(petnes-wasm PROPERTIES OUTPUT_NAME "petnes-core" SUFFIX ".js")
    target_link_options(petnes-wasm PRIVATE
        -sWASM=1
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        "-sEXPORT_NAME=PetNesModule"
        -sNO_FILESYSTEM=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=33554432
        -sMAXIMUM_MEMORY=268435456
        -sSTACK_SIZE=524288
        -sASSERTIONS=0
        -O3
        -flto
        "-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\",\"_nes_create\",\"_nes_destroy\",\"_nes_step_frame\",\"_nes_get_framebuffer\",\"_nes_get_framebuffer_size\",\"_nes_reset\",\"_nes_set_sample_rate\",\"_nes_controller_set\",\"_nes_apu_drain\",\"_nes_serialize\",\"_nes_deserialize\",\"_nes_free_string\",\"_nes_set_channel_mute\",\"_nes_set_palette_entry\"]"
        "-sEXPORTED_RUNTIME_METHODS=[\"HEAPU8\",\"HEAPF32\",\"UTF8ToString\",\"stringToUTF8\",\"lengthBytesUTF8\"]"
    )
else()
    add_executable(nestest-runner test/nestest_runner.cpp)
    target_link_libraries(nestest-runner PRIVATE petnes-core)
endif()
