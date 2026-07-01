#pragma once
#include <cstdint>
#include "bus.hpp"
#include "vendor/json.hpp"

class Cpu {
public:
    uint8_t  a = 0, x = 0, y = 0, sp = 0xfd;
    uint16_t pc = 0;
    uint8_t  p  = 0x24; // U | I
    uint64_t totalCycles = 0;
    bool     jammed = false;

    explicit Cpu(CpuBus* bus);
    void powerOn();
    void reset();
    int  step();        // returns cycles consumed
    void nmi();
    void setIrqLine(bool level);

    nlohmann::json getState() const;
    void setState(const nlohmann::json& s);

private:
    CpuBus* bus;
    bool nmiPending = false;
    bool irqLine    = false;

    uint8_t  fetch();
    uint16_t fetch16();
    uint16_t read16(uint16_t addr);
    void     push(uint8_t v);
    uint8_t  pull();
    void     setZN(uint8_t v);
    void     setFlag(uint8_t flag, bool on);
    void     serviceInterrupt(uint16_t vector);
    int      branch(bool cond, uint16_t target);

    void     adc(uint8_t value);
    void     sbc(uint8_t value);
    void     compare(uint8_t reg, uint8_t value);
    uint8_t  doASL(uint8_t v);
    uint8_t  doLSR(uint8_t v);
    uint8_t  doROL(uint8_t v);
    uint8_t  doROR(uint8_t v);
};
