#pragma once
#include "cartridge.hpp"
#include "mapper.hpp"
#include "controller.hpp"
#include "bus.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include "apu.hpp"
#include <memory>
#include <string>

class Nes {
public:
    Cartridge               cart;
    std::unique_ptr<Mapper> mapper;
    Controller              controller1, controller2;
    Bus                     bus;
    Cpu                     cpu;
    Ppu                     ppu;
    Apu                     apu;

    Nes(const uint8_t* romData, size_t romLen, int audioSampleRate = 44100);
    void reset();
    uint32_t* framebuffer();
    int  stepInstruction();
    void stepFrame();
    std::string serialize() const;
    void deserialize(const std::string& json);
};
