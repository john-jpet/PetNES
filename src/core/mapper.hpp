#pragma once
#include "cartridge.hpp"
#include "vendor/json.hpp"
#include <memory>

class Mapper {
public:
    virtual ~Mapper() = default;
    virtual uint8_t cpuRead(uint16_t addr) = 0;
    virtual void    cpuWrite(uint16_t addr, uint8_t val) = 0;
    virtual uint8_t ppuRead(uint16_t addr) = 0;
    virtual void    ppuWrite(uint16_t addr, uint8_t val) = 0;
    virtual Mirroring mirroring() const = 0;
    virtual bool irqAsserted() const { return false; }
    virtual void notifyA12Rise() {}
    virtual uint8_t* batteryRam() { return nullptr; }
    virtual size_t   batteryRamSize() const { return 0; }
    virtual nlohmann::json getState() const = 0;
    virtual void setState(const nlohmann::json& s) = 0;
};

std::unique_ptr<Mapper> createMapper(const Cartridge& cart);
