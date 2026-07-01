#pragma once
#include <cstdint>
void setPaletteEntry(int idx, uint8_t r, uint8_t g, uint8_t b);
#include <cstdint>
#include <functional>
#include "bus.hpp"
#include "vendor/json.hpp"

class Ppu : public PpuPort {
public:
    uint32_t framebuffer[256 * 240] = {};
    int  frameCount = 0;
    bool frameReady = false;
    int  scanline   = 261;
    int  dot        = 0;

    Ppu(Mapper* mapper, std::function<void()> onNmi);
    void tick();
    void reset();

    // PpuPort
    uint8_t readRegister(int reg) override;
    void    writeRegister(int reg, uint8_t value) override;
    void    writeOam(int index, uint8_t value) override;
    int     oamAddr() const override { return oamAddr_; }

    nlohmann::json getState() const;
    void setState(const nlohmann::json& s);

private:
    Mapper*              mapper_;
    std::function<void()> onNmi_;

    uint8_t ctrl_   = 0;
    uint8_t mask_   = 0;
    uint8_t status_ = 0;
    int     oamAddr_= 0;

    uint16_t v_ = 0, t_ = 0;
    uint8_t  fineX_ = 0;
    bool     w_ = false;

    uint8_t readBuffer_ = 0;
    uint8_t openBus_    = 0;

    uint8_t vram_   [0x1000] = {};
    uint8_t palette_[32]     = {};
    uint8_t oam_    [256]    = {};

    bool oddFrame_ = false;

    uint8_t ntByte_ = 0, atByte_ = 0, bgLo_ = 0, bgHi_ = 0;
    uint16_t shiftPatLo_  = 0, shiftPatHi_  = 0;
    uint16_t shiftAttrLo_ = 0, shiftAttrHi_ = 0;

    int     spriteCount_  = 0;
    int32_t spriteX_   [8] = {};
    uint8_t spritePatLo_[8] = {};
    uint8_t spritePatHi_[8] = {};
    uint8_t spriteAttr_ [8] = {};
    uint8_t spriteIndex_[8] = {};

    int     ntOffset(uint16_t addr) const;
    uint8_t busRead(uint16_t addr);
    void    busWrite(uint16_t addr, uint8_t value);
    uint8_t readPalette(uint16_t addr) const;
    void    writePalette(uint16_t addr, uint8_t value);

    void incrementX();
    void incrementY();
    void copyX();
    void copyY();
    bool renderingEnabled() const { return (mask_ & 0x18) != 0; }

    void loadShifters();
    void shift();
    void bgFetch();
    void evaluateSprites(int line);
    void renderPixel();
};
