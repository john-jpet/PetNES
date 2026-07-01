#include "nes.hpp"

Nes::Nes(const uint8_t* romData, size_t romLen, int audioSampleRate)
    : cart(parseINes(romData, romLen))
    , mapper(createMapper(cart))
    , bus(mapper.get(), &controller1, &controller2)
    , cpu(&bus)
    , ppu(mapper.get(), [this]{ cpu.nmi(); })
    , apu(audioSampleRate)
{
    apu.setMemoryReader([this](uint16_t a){ return bus.read(a); });
    bus.ppu = &ppu;
    bus.apu = &apu;
    cpu.powerOn();
}

void Nes::reset() {
    cpu.reset();
    ppu.reset();
}

uint32_t* Nes::framebuffer() { return ppu.framebuffer; }

int Nes::stepInstruction() {
    int cycles = cpu.step();
    if (bus.dmaStall > 0) {
        int stall = bus.dmaStall + (int)(cpu.totalCycles & 1);
        bus.dmaStall = 0;
        cpu.totalCycles += stall;
        cycles += stall;
    }
    for (int i = 0; i < cycles; i++) apu.tick();
    int dmcStall = apu.takeStallCycles();
    if (dmcStall > 0) {
        cpu.totalCycles += dmcStall;
        cycles += dmcStall;
    }
    for (int i = 0, n = cycles * 3; i < n; i++) ppu.tick();
    cpu.setIrqLine(mapper->irqAsserted() || apu.irqAsserted());
    return cycles;
}

void Nes::stepFrame() {
    ppu.frameReady = false;
    while (!ppu.frameReady) stepInstruction();
}

std::string Nes::serialize() const {
    nlohmann::json s;
    s["version"]     = 1;
    s["cpu"]         = cpu.getState();
    s["ppu"]         = ppu.getState();
    s["apu"]         = apu.getState();
    s["bus"]         = bus.getState();
    s["mapper"]      = mapper->getState();
    s["controller1"] = controller1.getState();
    s["controller2"] = controller2.getState();
    return s.dump();
}

void Nes::deserialize(const std::string& json) {
    auto s = nlohmann::json::parse(json);
    cpu.setState(s["cpu"]);
    ppu.setState(s["ppu"]);
    apu.setState(s["apu"]);
    bus.setState(s["bus"]);
    mapper->setState(s["mapper"]);
    controller1.setState(s["controller1"]);
    controller2.setState(s["controller2"]);
}
