#include "cartridge.hpp"
#include <stdexcept>

Cartridge parseINes(const uint8_t* data, size_t len) {
    if (len < 16 || data[0] != 0x4e || data[1] != 0x45 || data[2] != 0x53 || data[3] != 0x1a)
        throw std::runtime_error("Not an iNES file (bad magic)");

    const uint8_t prgBanks = data[4];
    const uint8_t chrBanks = data[5];
    const uint8_t flags6   = data[6];
    const uint8_t flags7   = data[7];

    const bool hasTrainer = (flags6 & 0x04) != 0;
    const bool hasBattery = (flags6 & 0x02) != 0;
    const bool fourScreen = (flags6 & 0x08) != 0;

    Mirroring mirroring = fourScreen
        ? Mirroring::FourScreen
        : ((flags6 & 0x01) ? Mirroring::Vertical : Mirroring::Horizontal);

    const int mapperId = (flags7 & 0xf0) | (flags6 >> 4);

    size_t offset  = 16 + (hasTrainer ? 512 : 0);
    size_t prgSize = (size_t)prgBanks * 0x4000;
    size_t chrSize = (size_t)chrBanks * 0x2000;

    if (len < offset + prgSize + chrSize)
        throw std::runtime_error("iNES file truncated");

    Cartridge cart;
    cart.prgRom.assign(data + offset, data + offset + prgSize);
    offset += prgSize;
    cart.chrRom.assign(data + offset, data + offset + chrSize);
    cart.chrIsRam   = chrBanks == 0;
    cart.mapperId   = mapperId;
    cart.mirroring  = mirroring;
    cart.hasBattery = hasBattery;
    cart.prgRamSize = 0x2000;
    return cart;
}
