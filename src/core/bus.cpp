#include "bus.hpp"

Bus::Bus(Mapper* m, Controller* c1, Controller* c2)
    : mapper(m), controller1(c1), controller2(c2) {}

uint8_t Bus::read(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7ff];
    if (addr < 0x4000) return ppu ? ppu->readRegister(addr & 7) : 0;
    if (addr == 0x4015) return apu ? apu->readStatus() : 0;
    if (addr == 0x4016) return (controller1->read() & 0x1f) | 0x40;
    if (addr == 0x4017) return (controller2->read() & 0x1f) | 0x40;
    if (addr < 0x4020) return 0;
    return mapper->cpuRead(addr);
}

void Bus::write(uint16_t addr, uint8_t value) {
    value &= 0xff;
    if (addr < 0x2000) {
        ram[addr & 0x7ff] = value;
    } else if (addr < 0x4000) {
        if (ppu) ppu->writeRegister(addr & 7, value);
    } else if (addr == 0x4014) {
        oamDma(value);
    } else if (addr == 0x4016) {
        controller1->write(value);
        controller2->write(value);
    } else if (addr < 0x4020) {
        if (apu) apu->writeRegister(addr, value);
    } else {
        mapper->cpuWrite(addr, value);
    }
}

void Bus::oamDma(uint8_t page) {
    if (!ppu) return;
    uint16_t base  = (uint16_t)page << 8;
    int      start = ppu->oamAddr();
    for (int i = 0; i < 256; i++)
        ppu->writeOam((start + i) & 0xff, read(base + i));
    dmaStall += 513;
}

nlohmann::json Bus::getState() const {
    nlohmann::json s;
    s["ram"]      = std::vector<uint8_t>(ram, ram + 0x800);
    s["dmaStall"] = dmaStall;
    return s;
}
void Bus::setState(const nlohmann::json& s) {
    auto r = s["ram"].get<std::vector<uint8_t>>();
    std::copy(r.begin(), r.end(), ram);
    dmaStall = s["dmaStall"].get<int>();
}
