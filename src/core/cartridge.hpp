#pragma once
#include <cstdint>
#include <vector>

enum class Mirroring { Horizontal, Vertical, SingleScreenLow, SingleScreenHigh, FourScreen };

struct Cartridge {
    std::vector<uint8_t> prgRom;
    std::vector<uint8_t> chrRom;
    bool chrIsRam = false;
    int mapperId = 0;
    Mirroring mirroring = Mirroring::Horizontal;
    bool hasBattery = false;
    size_t prgRamSize = 0x2000;
};

Cartridge parseINes(const uint8_t* data, size_t len);
