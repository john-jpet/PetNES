#pragma once
#include <cstdint>
#include "mapper.hpp"
#include "controller.hpp"
#include "vendor/json.hpp"

struct CpuBus {
    virtual uint8_t read(uint16_t addr)               = 0;
    virtual void    write(uint16_t addr, uint8_t val)  = 0;
    virtual ~CpuBus() = default;
};

struct PpuPort {
    virtual uint8_t readRegister(int reg) = 0;
    virtual void    writeRegister(int reg, uint8_t value) = 0;
    virtual void    writeOam(int index, uint8_t value) = 0;
    virtual int     oamAddr() const = 0;
    virtual ~PpuPort() = default;
};

struct ApuPort {
    virtual uint8_t readStatus() = 0;
    virtual void    writeRegister(uint16_t addr, uint8_t value) = 0;
    virtual ~ApuPort() = default;
};

class Bus : public CpuBus {
public:
    uint8_t ram[0x800] = {};
    int     dmaStall   = 0;

    PpuPort*    ppu = nullptr;
    ApuPort*    apu = nullptr;
    Mapper*     mapper;
    Controller* controller1;
    Controller* controller2;

    Bus(Mapper* mapper, Controller* c1, Controller* c2);
    uint8_t read(uint16_t addr);
    void    write(uint16_t addr, uint8_t value);
    nlohmann::json getState() const;
    void setState(const nlohmann::json& s);

private:
    void oamDma(uint8_t page);
};
