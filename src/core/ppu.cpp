#include "ppu.hpp"
#include <algorithm>
#include <cstring>

// Master palette (RGB) → ABGR (little-endian RGBA for putImageData)
static const uint32_t MASTER_PALETTE[64] = {
    0xff666666,0xff88002a,0xffa71214,0xffa4003b,0xff7e005c,0xff40006e,0xff00066c,0xff001d56,
    0xff003533,0xff00480b,0xff005200,0xff084f00,0xff4d4000,0xff000000,0xff000000,0xff000000,
    0xffadadad,0xffd95f15,0xffff4042,0xfffe2775,0xffcc1aa0,0xff7b1eb7,0xff2031b5,0xff004e99,
    0xff006d6b,0xff008738,0xff00930c,0xff328f00,0xff8d7c00,0xff000000,0xff000000,0xff000000,
    0xfffeffff,0xffffb064,0xffff9092,0xffff76c6,0xffff6af3,0xffcc6efe,0xff7081fe,0xff229ffe,  // note: ABGR order
    0xff00bfbc,0xff00d888,0xff30e45c,0xff82e045,0xffdecd48,0xff4f4f4f,0xff000000,0xff000000,
    0xfffeffff,0xffffdfc0,0xffffd2d3,0xffffc8e8,0xffffc2fb,0xffeac4fe,0xffc5cffe,0xffa5d8f7,
    0xff94e5e4,0xff96efcf,0xffabf4bd,0xffccf3b3,0xfff2ebb5,0xffb8b8b8,0xff000000,0xff000000,
};

// Build the table at runtime from RGB values (same source as FableNES ppu.ts)
static uint32_t PALETTE_ABGR[64];

struct PaletteInit {
    PaletteInit() {
        static const uint32_t RGB[64] = {
            0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,
            0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0x000000,0x000000,0x000000,
            0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,
            0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0x000000,0x000000,0x000000,
            0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,
            0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0x000000,0x000000,
            0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,
            0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0x000000,0x000000,
        };
        for (int i = 0; i < 64; i++) {
            uint32_t rgb = RGB[i];
            uint8_t r = (rgb >> 16) & 0xff;
            uint8_t g = (rgb >> 8)  & 0xff;
            uint8_t b =  rgb        & 0xff;
            PALETTE_ABGR[i] = (0xffu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    }
} static paletteInit;

void setPaletteEntry(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= 0 && idx < 64)
        PALETTE_ABGR[idx] = (0xffu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static uint8_t reverseByte(uint8_t b) {
    b = ((b & 0xf0) >> 4) | ((b & 0x0f) << 4);
    b = ((b & 0xcc) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xaa) >> 1) | ((b & 0x55) << 1);
    return b;
}

Ppu::Ppu(Mapper* mapper, std::function<void()> onNmi)
    : mapper_(mapper), onNmi_(onNmi) {}

void Ppu::reset() {
    ctrl_ = mask_ = 0;
    w_ = false;
    readBuffer_ = 0;
    scanline = 261;
    dot = 0;
    oddFrame_ = false;
}

// ---------------------------------------------------------------------------
// CPU-visible registers
// ---------------------------------------------------------------------------
uint8_t Ppu::readRegister(int reg) {
    switch (reg) {
        case 2: {
            uint8_t v = (status_ & 0xe0) | (openBus_ & 0x1f);
            status_ &= ~0x80;
            w_ = false;
            return v;
        }
        case 4: return oam_[oamAddr_];
        case 7: {
            uint16_t addr = v_ & 0x3fff;
            uint8_t  value;
            if (addr >= 0x3f00) {
                value = readPalette(addr);
                readBuffer_ = busRead(addr & 0x2fff);
            } else {
                value = readBuffer_;
                readBuffer_ = busRead(addr);
            }
            v_ = (v_ + ((ctrl_ & 0x04) ? 32 : 1)) & 0x7fff;
            return value;
        }
        default: return openBus_;
    }
}

void Ppu::writeRegister(int reg, uint8_t value) {
    openBus_ = value;
    switch (reg) {
        case 0: {
            uint8_t prevNmi = ctrl_ & 0x80;
            ctrl_ = value;
            t_ = (t_ & ~0x0c00u) | ((uint16_t)(value & 0x03) << 10);
            if (!prevNmi && (value & 0x80) && (status_ & 0x80)) onNmi_();
            break;
        }
        case 1: mask_ = value; break;
        case 3: oamAddr_ = value; break;
        case 4: oam_[oamAddr_] = value; oamAddr_ = (oamAddr_ + 1) & 0xff; break;
        case 5:
            if (!w_) { fineX_ = value & 0x07; t_ = (t_ & ~0x001fu) | (value >> 3); }
            else      { t_ = (t_ & ~0x73e0u) | ((uint16_t)(value & 0x07) << 12) | ((uint16_t)(value >> 3) << 5); }
            w_ = !w_;
            break;
        case 6:
            if (!w_) t_ = (t_ & 0x00ffu) | ((uint16_t)(value & 0x3f) << 8);
            else      { t_ = (t_ & 0x7f00u) | value; v_ = t_; }
            w_ = !w_;
            break;
        case 7: {
            uint16_t addr = v_ & 0x3fff;
            if (addr >= 0x3f00) writePalette(addr, value);
            else busWrite(addr, value);
            v_ = (v_ + ((ctrl_ & 0x04) ? 32 : 1)) & 0x7fff;
            break;
        }
    }
}

void Ppu::writeOam(int index, uint8_t value) { oam_[index] = value; }

// ---------------------------------------------------------------------------
// Internal bus
// ---------------------------------------------------------------------------
int Ppu::ntOffset(uint16_t addr) const {
    int table = (addr >> 10) & 3;
    int phys;
    switch (mapper_->mirroring()) {
        case Mirroring::Vertical:         phys = table & 1; break;
        case Mirroring::Horizontal:       phys = table >> 1; break;
        case Mirroring::SingleScreenLow:  phys = 0; break;
        case Mirroring::SingleScreenHigh: phys = 1; break;
        default:                          phys = table; break;
    }
    return phys * 0x400 + (addr & 0x3ff);
}

uint8_t Ppu::busRead(uint16_t addr) {
    addr &= 0x3fff;
    if (addr < 0x2000) return mapper_->ppuRead(addr);
    return vram_[ntOffset(addr)];
}

void Ppu::busWrite(uint16_t addr, uint8_t value) {
    addr &= 0x3fff;
    if (addr < 0x2000) mapper_->ppuWrite(addr, value);
    else vram_[ntOffset(addr)] = value;
}

uint8_t Ppu::readPalette(uint16_t addr) const {
    int i = addr & 0x1f;
    if (i >= 0x10 && (i & 0x03) == 0) i -= 0x10;
    uint8_t v = palette_[i];
    if (mask_ & 0x01) v &= 0x30;
    return v;
}
void Ppu::writePalette(uint16_t addr, uint8_t value) {
    int i = addr & 0x1f;
    if (i >= 0x10 && (i & 0x03) == 0) i -= 0x10;
    palette_[i] = value & 0x3f;
}

// ---------------------------------------------------------------------------
// Loopy helpers
// ---------------------------------------------------------------------------
void Ppu::incrementX() {
    if ((v_ & 0x001f) == 31) { v_ &= ~0x001fu; v_ ^= 0x0400; } else v_++;
}
void Ppu::incrementY() {
    if ((v_ & 0x7000) != 0x7000) {
        v_ += 0x1000;
    } else {
        v_ &= ~0x7000u;
        int y = (v_ & 0x03e0) >> 5;
        if      (y == 29) { y = 0; v_ ^= 0x0800; }
        else if (y == 31)   y = 0;
        else                y++;
        v_ = (v_ & ~0x03e0u) | (y << 5);
    }
}
void Ppu::copyX() { v_ = (v_ & ~0x041fu) | (t_ & 0x041f); }
void Ppu::copyY() { v_ = (v_ & ~0x7be0u) | (t_ & 0x7be0); }

// ---------------------------------------------------------------------------
// Background
// ---------------------------------------------------------------------------
void Ppu::loadShifters() {
    shiftPatLo_  = (shiftPatLo_  & 0xff00) | bgLo_;
    shiftPatHi_  = (shiftPatHi_  & 0xff00) | bgHi_;
    shiftAttrLo_ = (shiftAttrLo_ & 0xff00) | ((atByte_ & 1) ? 0xff : 0);
    shiftAttrHi_ = (shiftAttrHi_ & 0xff00) | ((atByte_ & 2) ? 0xff : 0);
}
void Ppu::shift() {
    shiftPatLo_  = (shiftPatLo_  << 1) & 0xffff;
    shiftPatHi_  = (shiftPatHi_  << 1) & 0xffff;
    shiftAttrLo_ = (shiftAttrLo_ << 1) & 0xffff;
    shiftAttrHi_ = (shiftAttrHi_ << 1) & 0xffff;
}
void Ppu::bgFetch() {
    switch ((dot - 1) & 7) {
        case 0:
            loadShifters();
            ntByte_ = busRead(0x2000 | (v_ & 0x0fff));
            break;
        case 2: {
            uint8_t at = busRead(0x23c0 | (v_ & 0x0c00) | ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07));
            if (v_ & 0x0040) at >>= 4;
            if (v_ & 0x0002) at >>= 2;
            atByte_ = at & 3;
            break;
        }
        case 4: {
            uint16_t base = (ctrl_ & 0x10) ? 0x1000 : 0;
            bgLo_ = busRead(base + ntByte_ * 16 + ((v_ >> 12) & 7));
            break;
        }
        case 6: {
            uint16_t base = (ctrl_ & 0x10) ? 0x1000 : 0;
            bgHi_ = busRead(base + ntByte_ * 16 + ((v_ >> 12) & 7) + 8);
            break;
        }
        case 7: incrementX(); break;
    }
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------
void Ppu::evaluateSprites(int line) {
    int height = (ctrl_ & 0x20) ? 16 : 8;
    spriteCount_ = 0;
    bool overflow = false;

    for (int i = 0; i < 64; i++) {
        int y   = oam_[i * 4];
        int row = line - 1 - y;
        if (row < 0 || row >= height) continue;
        if (spriteCount_ == 8) { overflow = true; break; }
        int n = spriteCount_++;
        uint8_t tile = oam_[i*4+1];
        uint8_t attr = oam_[i*4+2];
        spriteX_[n]     = oam_[i*4+3];
        spriteAttr_[n]  = attr;
        spriteIndex_[n] = i;

        int r = (attr & 0x80) ? (height - 1 - row) : row;
        uint16_t patternAddr;
        if (height == 16) {
            uint16_t bank = (tile & 1) ? 0x1000 : 0;
            uint8_t  t    = tile & 0xfe;
            if (r >= 8) { t++; r -= 8; }
            patternAddr = bank + t * 16 + r;
        } else {
            uint16_t bank = (ctrl_ & 0x08) ? 0x1000 : 0;
            patternAddr = bank + tile * 16 + r;
        }
        uint8_t lo = busRead(patternAddr);
        uint8_t hi = busRead(patternAddr + 8);
        if (attr & 0x40) { lo = reverseByte(lo); hi = reverseByte(hi); }
        spritePatLo_[n] = lo;
        spritePatHi_[n] = hi;
    }
    if (overflow) status_ |= 0x20;
}

// ---------------------------------------------------------------------------
// Pixel composition
// ---------------------------------------------------------------------------
void Ppu::renderPixel() {
    int x = dot - 1;
    int y = scanline;

    int     bgPixel = 0, bgPal = 0;
    if ((mask_ & 0x08) && (x >= 8 || (mask_ & 0x02))) {
        uint16_t bit = 0x8000 >> fineX_;
        bgPixel = ((shiftPatLo_ & bit) ? 1 : 0) | ((shiftPatHi_ & bit) ? 2 : 0);
        bgPal   = ((shiftAttrLo_& bit) ? 1 : 0) | ((shiftAttrHi_& bit) ? 2 : 0);
    }

    int  spPixel = 0, spPal = 0;
    bool spBehind = false, spIsZero = false;
    if ((mask_ & 0x10) && (x >= 8 || (mask_ & 0x04))) {
        for (int i = 0; i < spriteCount_; i++) {
            int offset = x - spriteX_[i];
            if (offset < 0 || offset > 7) continue;
            int px = ((spritePatLo_[i] >> (7 - offset)) & 1) |
                     (((spritePatHi_[i] >> (7 - offset)) & 1) << 1);
            if (px == 0) continue;
            spPixel  = px;
            spPal    = (spriteAttr_[i] & 0x03) + 4;
            spBehind = (spriteAttr_[i] & 0x20) != 0;
            spIsZero = spriteIndex_[i] == 0;
            break;
        }
    }

    if (spIsZero && spPixel != 0 && bgPixel != 0 && x != 255)
        status_ |= 0x40;

    int paletteIndex;
    if      (bgPixel == 0 && spPixel == 0) paletteIndex = 0;
    else if (bgPixel == 0)                 paletteIndex = spPal * 4 + spPixel;
    else if (spPixel == 0)                 paletteIndex = bgPal * 4 + bgPixel;
    else paletteIndex = spBehind ? (bgPal * 4 + bgPixel) : (spPal * 4 + spPixel);

    uint8_t color = readPalette(0x3f00 + paletteIndex) & 0x3f;
    framebuffer[y * 256 + x] = PALETTE_ABGR[color];
}

// ---------------------------------------------------------------------------
// Main clock
// ---------------------------------------------------------------------------
void Ppu::tick() {
    bool visible   = scanline < 240;
    bool preRender = scanline == 261;

    if ((visible || preRender) && renderingEnabled()) {
        if ((dot >= 2 && dot <= 257) || (dot >= 322 && dot <= 337)) shift();
        if ((dot >= 1 && dot <= 256) || (dot >= 321 && dot <= 336)) bgFetch();
        if (dot == 256) incrementY();
        if (dot == 257) { loadShifters(); copyX(); }
        if (preRender && dot >= 280 && dot <= 304) copyY();

        if (dot == 257 && visible)   evaluateSprites(scanline + 1);
        if (preRender && dot == 257) spriteCount_ = 0;

        // MMC3 A12 approximation
        if (dot == 260) mapper_->notifyA12Rise();
    }

    if (visible && dot >= 1 && dot <= 256) {
        if (renderingEnabled()) {
            renderPixel();
        } else {
            uint16_t addr = (v_ & 0x3fff) >= 0x3f00 ? (v_ & 0x3fff) : 0x3f00;
            framebuffer[scanline * 256 + dot - 1] = PALETTE_ABGR[readPalette(addr) & 0x3f];
        }
    }

    if (scanline == 241 && dot == 1) {
        status_ |= 0x80;
        frameReady = true;
        frameCount++;
        if (ctrl_ & 0x80) onNmi_();
    }
    if (preRender && dot == 1) status_ &= ~0xe0;

    dot++;
    if (preRender && dot == 340 && oddFrame_ && renderingEnabled())
        dot = 341; // skip last dot on odd frames
    if (dot > 340) {
        dot = 0;
        scanline++;
        if (scanline > 261) {
            scanline   = 0;
            oddFrame_  = !oddFrame_;
        }
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
nlohmann::json Ppu::getState() const {
    nlohmann::json s;
    s["ctrl"]   = ctrl_; s["mask"] = mask_; s["status"] = status_; s["oamAddr"] = oamAddr_;
    s["v"] = v_; s["t"] = t_; s["fineX"] = fineX_; s["w"] = w_;
    s["readBuffer"] = readBuffer_; s["openBus"] = openBus_;
    s["vram"]    = std::vector<uint8_t>(vram_,    vram_    + 0x1000);
    s["palette"] = std::vector<uint8_t>(palette_, palette_ + 32);
    s["oam"]     = std::vector<uint8_t>(oam_,     oam_     + 256);
    s["scanline"] = scanline; s["dot"] = dot; s["oddFrame"] = oddFrame_;
    s["frameCount"] = frameCount;
    s["spriteCount"] = spriteCount_;
    s["spriteX"]     = std::vector<int32_t>(spriteX_,    spriteX_    + 8);
    s["spritePatLo"] = std::vector<uint8_t>(spritePatLo_,spritePatLo_+ 8);
    s["spritePatHi"] = std::vector<uint8_t>(spritePatHi_,spritePatHi_+ 8);
    s["spriteAttr"]  = std::vector<uint8_t>(spriteAttr_, spriteAttr_ + 8);
    s["spriteIndex"] = std::vector<uint8_t>(spriteIndex_,spriteIndex_+ 8);
    s["ntByte"] = ntByte_; s["atByte"] = atByte_; s["bgLo"] = bgLo_; s["bgHi"] = bgHi_;
    s["shiftPatLo"] = shiftPatLo_; s["shiftPatHi"] = shiftPatHi_;
    s["shiftAttrLo"]= shiftAttrLo_; s["shiftAttrHi"]= shiftAttrHi_;
    return s;
}
void Ppu::setState(const nlohmann::json& s) {
    ctrl_   = s["ctrl"].get<uint8_t>();
    mask_   = s["mask"].get<uint8_t>();
    status_ = s["status"].get<uint8_t>();
    oamAddr_= s["oamAddr"].get<int>();
    v_      = s["v"].get<uint16_t>();
    t_      = s["t"].get<uint16_t>();
    fineX_  = s["fineX"].get<uint8_t>();
    w_      = s["w"].get<bool>();
    readBuffer_ = s["readBuffer"].get<uint8_t>();
    openBus_    = s["openBus"].get<uint8_t>();
    auto vr = s["vram"].get<std::vector<uint8_t>>();    std::copy(vr.begin(),vr.end(),vram_);
    auto pa = s["palette"].get<std::vector<uint8_t>>(); std::copy(pa.begin(),pa.end(),palette_);
    auto oa = s["oam"].get<std::vector<uint8_t>>();     std::copy(oa.begin(),oa.end(),oam_);
    scanline    = s["scanline"].get<int>();
    dot         = s["dot"].get<int>();
    oddFrame_   = s["oddFrame"].get<bool>();
    frameCount  = s["frameCount"].get<int>();
    spriteCount_= s["spriteCount"].get<int>();
    auto sx  = s["spriteX"].get<std::vector<int32_t>>();    std::copy(sx.begin(),sx.end(),spriteX_);
    auto spl = s["spritePatLo"].get<std::vector<uint8_t>>(); std::copy(spl.begin(),spl.end(),spritePatLo_);
    auto sph = s["spritePatHi"].get<std::vector<uint8_t>>(); std::copy(sph.begin(),sph.end(),spritePatHi_);
    auto sa  = s["spriteAttr"].get<std::vector<uint8_t>>();  std::copy(sa.begin(),sa.end(),spriteAttr_);
    auto si  = s["spriteIndex"].get<std::vector<uint8_t>>(); std::copy(si.begin(),si.end(),spriteIndex_);
    ntByte_     = s["ntByte"].get<uint8_t>();
    atByte_     = s["atByte"].get<uint8_t>();
    bgLo_       = s["bgLo"].get<uint8_t>();
    bgHi_       = s["bgHi"].get<uint8_t>();
    shiftPatLo_ = s["shiftPatLo"].get<uint16_t>();
    shiftPatHi_ = s["shiftPatHi"].get<uint16_t>();
    shiftAttrLo_= s["shiftAttrLo"].get<uint16_t>();
    shiftAttrHi_= s["shiftAttrHi"].get<uint16_t>();
}
