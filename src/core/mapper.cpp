#include "mapper.hpp"
#include <stdexcept>
#include <algorithm>

// ---------------------------------------------------------------------------
// BaseMapper
// ---------------------------------------------------------------------------
class BaseMapper : public Mapper {
protected:
    const Cartridge& cart;
    std::vector<uint8_t> prgRam;
    std::vector<uint8_t> chr;
    Mirroring mirror;

    BaseMapper(const Cartridge& c)
        : cart(c)
        , prgRam(c.prgRamSize, 0)
        , chr(c.chrIsRam ? std::vector<uint8_t>(0x2000, 0) : c.chrRom)
        , mirror(c.mirroring)
    {}

public:
    Mirroring mirroring() const override { return mirror; }

    uint8_t* batteryRam() override {
        return cart.hasBattery ? prgRam.data() : nullptr;
    }
    size_t batteryRamSize() const override {
        return cart.hasBattery ? prgRam.size() : 0;
    }

    nlohmann::json getState() const override {
        nlohmann::json s;
        s["prgRam"] = prgRam;
        s["chrRam"] = cart.chrIsRam ? nlohmann::json(chr) : nlohmann::json(nullptr);
        s["mirror"] = (int)mirror;
        return s;
    }
    void setState(const nlohmann::json& s) override {
        auto pr = s["prgRam"].get<std::vector<uint8_t>>();
        std::copy(pr.begin(), pr.end(), prgRam.begin());
        if (cart.chrIsRam && !s["chrRam"].is_null()) {
            auto cr = s["chrRam"].get<std::vector<uint8_t>>();
            std::copy(cr.begin(), cr.end(), chr.begin());
        }
        mirror = (Mirroring)s["mirror"].get<int>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 0: NROM
// ---------------------------------------------------------------------------
class NROM : public BaseMapper {
    uint32_t prgMask;
public:
    NROM(const Cartridge& c) : BaseMapper(c) {
        prgMask = c.prgRom.size() > 0x4000 ? 0x7fff : 0x3fff;
    }
    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000) return cart.prgRom[addr & prgMask];
        if (addr >= 0x6000) return prgRam[addr - 0x6000];
        return 0;
    }
    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr >= 0x6000 && addr < 0x8000) prgRam[addr - 0x6000] = val;
    }
    uint8_t ppuRead(uint16_t addr) override { return chr[addr & 0x1fff]; }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[addr & 0x1fff] = val;
    }
    nlohmann::json getState() const override { return BaseMapper::getState(); }
    void setState(const nlohmann::json& s) override { BaseMapper::setState(s); }
};

// ---------------------------------------------------------------------------
// Mapper 1: MMC1
// ---------------------------------------------------------------------------
class MMC1 : public BaseMapper {
    uint8_t shiftReg = 0x10;
    uint8_t control  = 0x0c;
    uint8_t chrBank0 = 0, chrBank1 = 0, prgBank = 0;
    bool prgRamEnable = true;

    int prgBankCount() const { return (int)(cart.prgRom.size() >> 14); }

    void applyMirroring() {
        switch (control & 3) {
            case 0: mirror = Mirroring::SingleScreenLow;  break;
            case 1: mirror = Mirroring::SingleScreenHigh; break;
            case 2: mirror = Mirroring::Vertical;         break;
            case 3: mirror = Mirroring::Horizontal;       break;
        }
    }

    int chrOffset(uint16_t addr) const {
        int chrLen = (int)chr.size();
        if (control & 0x10) {
            int bank = (addr < 0x1000) ? chrBank0 : chrBank1;
            return ((bank * 0x1000) + (addr & 0x0fff)) % chrLen;
        }
        return (((chrBank0 & 0x1e) * 0x1000) + (addr & 0x1fff)) % chrLen;
    }

public:
    MMC1(const Cartridge& c) : BaseMapper(c) { applyMirroring(); }

    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000) {
            int mode = (control >> 2) & 3;
            int bank, offset = addr & 0x3fff;
            if (mode < 2) {
                bank = (prgBank & 0x0e) | ((addr >> 14) & 1);
            } else if (mode == 2) {
                bank = (addr < 0xc000) ? 0 : (prgBank & 0x0f);
            } else {
                bank = (addr < 0xc000) ? (prgBank & 0x0f) : prgBankCount() - 1;
            }
            return cart.prgRom[((bank % prgBankCount()) << 14) | offset];
        }
        if (addr >= 0x6000) return prgRamEnable ? prgRam[addr - 0x6000] : 0;
        return 0;
    }

    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr < 0x6000) return;
        if (addr < 0x8000) {
            if (prgRamEnable) prgRam[addr - 0x6000] = val;
            return;
        }
        if (val & 0x80) {
            shiftReg = 0x10;
            control |= 0x0c;
            applyMirroring();
            return;
        }
        bool complete = (shiftReg & 1) != 0;
        shiftReg = (shiftReg >> 1) | ((val & 1) << 4);
        if (!complete) return;
        uint8_t data = shiftReg;
        shiftReg = 0x10;
        switch ((addr >> 13) & 3) {
            case 0: control = data; applyMirroring(); break;
            case 1: chrBank0 = data; break;
            case 2: chrBank1 = data; break;
            case 3: prgBank = data & 0x0f; prgRamEnable = (data & 0x10) == 0; break;
        }
    }

    uint8_t ppuRead(uint16_t addr) override { return chr[chrOffset(addr)]; }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[chrOffset(addr)] = val;
    }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState();
        s["shiftReg"] = shiftReg; s["control"] = control;
        s["chrBank0"] = chrBank0; s["chrBank1"] = chrBank1;
        s["prgBank"]  = prgBank;  s["prgRamEnable"] = prgRamEnable;
        return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s);
        shiftReg     = s["shiftReg"].get<uint8_t>();
        control      = s["control"].get<uint8_t>();
        chrBank0     = s["chrBank0"].get<uint8_t>();
        chrBank1     = s["chrBank1"].get<uint8_t>();
        prgBank      = s["prgBank"].get<uint8_t>();
        prgRamEnable = s["prgRamEnable"].get<bool>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 2: UxROM
// ---------------------------------------------------------------------------
class UxROM : public BaseMapper {
    int prgBank = 0;
public:
    UxROM(const Cartridge& c) : BaseMapper(c) {}

    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0xc000)
            return cart.prgRom[cart.prgRom.size() - 0x4000 + (addr & 0x3fff)];
        if (addr >= 0x8000) {
            int banks = (int)(cart.prgRom.size() >> 14);
            return cart.prgRom[((prgBank % banks) << 14) | (addr & 0x3fff)];
        }
        if (addr >= 0x6000) return prgRam[addr - 0x6000];
        return 0;
    }
    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr >= 0x8000)
            prgBank = val & cpuRead(addr); // bus conflict
        else if (addr >= 0x6000)
            prgRam[addr - 0x6000] = val;
    }
    uint8_t ppuRead(uint16_t addr) override { return chr[addr & 0x1fff]; }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[addr & 0x1fff] = val;
    }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState(); s["prgBank"] = prgBank; return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s); prgBank = s["prgBank"].get<int>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 3: CNROM
// ---------------------------------------------------------------------------
class CNROM : public BaseMapper {
    int chrBank = 0;
    uint32_t prgMask;
public:
    CNROM(const Cartridge& c) : BaseMapper(c) {
        prgMask = c.prgRom.size() > 0x4000 ? 0x7fff : 0x3fff;
    }
    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000) return cart.prgRom[addr & prgMask];
        if (addr >= 0x6000) return prgRam[addr - 0x6000];
        return 0;
    }
    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr >= 0x8000)
            chrBank = (val & cpuRead(addr)) & 3; // bus conflict
        else if (addr >= 0x6000)
            prgRam[addr - 0x6000] = val;
    }
    uint8_t ppuRead(uint16_t addr) override {
        return chr[((size_t)chrBank * 0x2000 + (addr & 0x1fff)) % chr.size()];
    }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[addr & 0x1fff] = val;
    }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState(); s["chrBank"] = chrBank; return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s); chrBank = s["chrBank"].get<int>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 4: MMC3
// ---------------------------------------------------------------------------
class MMC3 : public BaseMapper {
    uint8_t bankSelect = 0;
    uint8_t banks[8]   = {};
    uint8_t irqLatch   = 0;
    uint8_t irqCounter = 0;
    bool irqReloadPending = false;
    bool irqEnabled    = false;
    bool irqFlag       = false;
    bool prgRamEnabled = true;

    int prgBankCount8k() const { return (int)(cart.prgRom.size() >> 13); }

    int chrOffset(uint16_t addr) const {
        bool invert = (bankSelect & 0x80) != 0;
        uint16_t a  = invert ? (addr ^ 0x1000) : addr;
        int bank1k;
        if (a < 0x0800)       bank1k = (banks[0] & 0xfe) + ((a >> 10) & 1);
        else if (a < 0x1000)  bank1k = (banks[1] & 0xfe) + ((a >> 10) & 1);
        else                  bank1k = banks[2 + ((a - 0x1000) >> 10)];
        return ((bank1k << 10) | (a & 0x3ff)) % (int)chr.size();
    }

public:
    MMC3(const Cartridge& c) : BaseMapper(c) {}

    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000) {
            bool prgMode = (bankSelect & 0x40) != 0;
            int last = prgBankCount8k() - 1;
            int bank;
            switch ((addr >> 13) & 3) {
                case 0: bank = prgMode ? last - 1 : banks[6]; break;
                case 1: bank = banks[7]; break;
                case 2: bank = prgMode ? banks[6] : last - 1; break;
                default: bank = last; break;
            }
            return cart.prgRom[((bank % (last + 1)) << 13) | (addr & 0x1fff)];
        }
        if (addr >= 0x6000) return prgRamEnabled ? prgRam[addr - 0x6000] : 0;
        return 0;
    }

    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr < 0x6000) return;
        if (addr < 0x8000) {
            if (prgRamEnabled) prgRam[addr - 0x6000] = val;
            return;
        }
        bool odd = (addr & 1) != 0;
        if (addr < 0xa000) {
            if (!odd) bankSelect = val;
            else banks[bankSelect & 7] = val;
        } else if (addr < 0xc000) {
            if (!odd) {
                if (cart.mirroring != Mirroring::FourScreen)
                    mirror = (val & 1) ? Mirroring::Horizontal : Mirroring::Vertical;
            } else {
                prgRamEnabled = (val & 0x80) != 0;
            }
        } else if (addr < 0xe000) {
            if (!odd) irqLatch = val;
            else { irqCounter = 0; irqReloadPending = true; }
        } else {
            if (!odd) { irqEnabled = false; irqFlag = false; }
            else       { irqEnabled = true; }
        }
    }

    uint8_t ppuRead(uint16_t addr) override { return chr[chrOffset(addr)]; }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[chrOffset(addr)] = val;
    }

    void notifyA12Rise() override {
        if (irqCounter == 0 || irqReloadPending) {
            irqCounter = irqLatch;
            irqReloadPending = false;
        } else {
            irqCounter--;
        }
        if (irqCounter == 0 && irqEnabled) irqFlag = true;
    }
    bool irqAsserted() const override { return irqFlag; }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState();
        s["bankSelect"] = bankSelect;
        s["banks"]      = std::vector<uint8_t>(banks, banks + 8);
        s["irqLatch"]         = irqLatch;
        s["irqCounter"]       = irqCounter;
        s["irqReloadPending"] = irqReloadPending;
        s["irqEnabled"]       = irqEnabled;
        s["irqFlag"]          = irqFlag;
        s["prgRamEnabled"]    = prgRamEnabled;
        return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s);
        bankSelect = s["bankSelect"].get<uint8_t>();
        auto b = s["banks"].get<std::vector<uint8_t>>();
        std::copy(b.begin(), b.end(), banks);
        irqLatch         = s["irqLatch"].get<uint8_t>();
        irqCounter       = s["irqCounter"].get<uint8_t>();
        irqReloadPending = s["irqReloadPending"].get<bool>();
        irqEnabled       = s["irqEnabled"].get<bool>();
        irqFlag          = s["irqFlag"].get<bool>();
        prgRamEnabled    = s["prgRamEnabled"].get<bool>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 9: MMC2 (Punch-Out!!)
// Two CHR latches that switch banks when specific tile addresses are read.
// ---------------------------------------------------------------------------
class MMC2 : public BaseMapper {
    int prgBank    = 0;
    int chr0FD = 0, chr0FE = 0;  // $0000-$0FFF bank selections
    int chr1FD = 0, chr1FE = 0;  // $1000-$1FFF bank selections
    int latch0 = 0xFE, latch1 = 0xFE;

    int prgCount8k() const { return (int)(cart.prgRom.size() >> 13); }

public:
    MMC2(const Cartridge& c) : BaseMapper(c) {}

    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000) {
            int n = prgCount8k();
            int bank;
            switch ((addr >> 13) & 3) {
                case 0:  bank = prgBank % n; break; // $8000 switchable
                case 1:  bank = n - 3;       break; // $A000 fixed
                case 2:  bank = n - 2;       break; // $C000 fixed
                default: bank = n - 1;       break; // $E000 fixed (last)
            }
            return cart.prgRom[(bank << 13) | (addr & 0x1fff)];
        }
        if (addr >= 0x6000) return prgRam[addr - 0x6000];
        return 0;
    }

    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr < 0x6000) return;
        if (addr < 0x8000) { prgRam[addr - 0x6000] = val; return; }
        switch ((addr >> 12) & 0xf) {
            case 0xA: prgBank = val & 0x0f; break;
            case 0xB: chr0FD  = val & 0x1f; break;
            case 0xC: chr0FE  = val & 0x1f; break;
            case 0xD: chr1FD  = val & 0x1f; break;
            case 0xE: chr1FE  = val & 0x1f; break;
            case 0xF: mirror  = (val & 1) ? Mirroring::Horizontal : Mirroring::Vertical; break;
        }
    }

    uint8_t ppuRead(uint16_t addr) override {
        // Select bank based on current latch, then update latch after read
        int bank = (addr < 0x1000)
            ? (latch0 == 0xFE ? chr0FE : chr0FD)
            : (latch1 == 0xFE ? chr1FE : chr1FD);
        uint8_t val = chr[((size_t)bank << 12 | (addr & 0x0fff)) % chr.size()];

        // Latch update — must happen AFTER the read
        if      (addr >= 0x0FD8 && addr <= 0x0FDF) latch0 = 0xFD;
        else if (addr >= 0x0FE8 && addr <= 0x0FEF) latch0 = 0xFE;
        else if (addr >= 0x1FD8 && addr <= 0x1FDF) latch1 = 0xFD;
        else if (addr >= 0x1FE8 && addr <= 0x1FEF) latch1 = 0xFE;

        return val;
    }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[addr & 0x1fff] = val;
    }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState();
        s["prgBank"] = prgBank;
        s["chr0FD"] = chr0FD; s["chr0FE"] = chr0FE;
        s["chr1FD"] = chr1FD; s["chr1FE"] = chr1FE;
        s["latch0"] = latch0; s["latch1"] = latch1;
        return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s);
        prgBank = s["prgBank"].get<int>();
        chr0FD  = s["chr0FD"].get<int>(); chr0FE = s["chr0FE"].get<int>();
        chr1FD  = s["chr1FD"].get<int>(); chr1FE = s["chr1FE"].get<int>();
        latch0  = s["latch0"].get<int>(); latch1 = s["latch1"].get<int>();
    }
};

// ---------------------------------------------------------------------------
// Mapper 66: GxROM
// ---------------------------------------------------------------------------
class GxROM : public BaseMapper {
    int prgBank = 0, chrBank = 0;
public:
    GxROM(const Cartridge& c) : BaseMapper(c) {}

    uint8_t cpuRead(uint16_t addr) override {
        if (addr >= 0x8000)
            return cart.prgRom[((size_t)prgBank * 0x8000 + (addr - 0x8000)) % cart.prgRom.size()];
        if (addr >= 0x6000) return prgRam[addr - 0x6000];
        return 0;
    }
    void cpuWrite(uint16_t addr, uint8_t val) override {
        if (addr >= 0x8000) { prgBank = (val >> 4) & 0x03; chrBank = val & 0x03; }
        else if (addr >= 0x6000) prgRam[addr - 0x6000] = val;
    }
    uint8_t ppuRead(uint16_t addr) override {
        return chr[((size_t)chrBank * 0x2000 + (addr & 0x1fff)) % chr.size()];
    }
    void ppuWrite(uint16_t addr, uint8_t val) override {
        if (cart.chrIsRam) chr[addr & 0x1fff] = val;
    }

    nlohmann::json getState() const override {
        auto s = BaseMapper::getState(); s["prgBank"] = prgBank; s["chrBank"] = chrBank; return s;
    }
    void setState(const nlohmann::json& s) override {
        BaseMapper::setState(s);
        prgBank = s["prgBank"].get<int>();
        chrBank = s["chrBank"].get<int>();
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<Mapper> createMapper(const Cartridge& cart) {
    switch (cart.mapperId) {
        case  0: return std::make_unique<NROM>(cart);
        case  1: return std::make_unique<MMC1>(cart);
        case  2: return std::make_unique<UxROM>(cart);
        case  3: return std::make_unique<CNROM>(cart);
        case  4: return std::make_unique<MMC3>(cart);
        case  9: return std::make_unique<MMC2>(cart);
        case 66: return std::make_unique<GxROM>(cart);
        default:
            throw std::runtime_error("Unsupported mapper: " + std::to_string(cart.mapperId));
    }
}
