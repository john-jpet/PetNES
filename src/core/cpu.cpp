#include "cpu.hpp"
#include <stdexcept>
#include <cstring>

// Status flag bits
static constexpr uint8_t FC = 0x01;
static constexpr uint8_t FZ = 0x02;
static constexpr uint8_t FI = 0x04;
static constexpr uint8_t FD = 0x08;
static constexpr uint8_t FB = 0x10;
static constexpr uint8_t FU = 0x20;
static constexpr uint8_t FV = 0x40;
static constexpr uint8_t FN = 0x80;

// Addressing modes
enum class Mode : uint8_t {
    IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL
};

// Operations
enum class Op : uint8_t {
    ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC,
    CLD, CLI, CLV, CMP, CPX, CPY, DEC, DEX, DEY, EOR, INC, INX, INY, JMP,
    JSR, LDA, LDX, LDY, LSR, NOP, ORA, PHA, PHP, PLA, PLP, ROL, ROR, RTI,
    RTS, SBC, SEC, SED, SEI, STA, STX, STY, TAX, TAY, TSX, TXA, TXS, TYA,
    // Unofficial
    LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, ANC, ALR, ARR, SBX, LAS, SHY,
    SHX, SHA, TAS, XAA, JAM
};

struct OpcodeInfo {
    Op      op;
    Mode    mode;
    uint8_t cycles;
    bool    page; // +1 on page cross
};

static OpcodeInfo TABLE[256];

static void def(int code, Op op, Mode mode, uint8_t cycles, bool page = false) {
    TABLE[code] = { op, mode, cycles, page };
}

struct TableInit {
    TableInit() {
        // fill with JAM so unset entries don't crash
        for (int i = 0; i < 256; i++) TABLE[i] = { Op::JAM, Mode::IMP, 2, false };

        // Official
        def(0x69,Op::ADC,Mode::IMM,2); def(0x65,Op::ADC,Mode::ZP,3);  def(0x75,Op::ADC,Mode::ZPX,4);
        def(0x6d,Op::ADC,Mode::ABS,4); def(0x7d,Op::ADC,Mode::ABX,4,true); def(0x79,Op::ADC,Mode::ABY,4,true);
        def(0x61,Op::ADC,Mode::IZX,6); def(0x71,Op::ADC,Mode::IZY,5,true);
        def(0x29,Op::AND,Mode::IMM,2); def(0x25,Op::AND,Mode::ZP,3);  def(0x35,Op::AND,Mode::ZPX,4);
        def(0x2d,Op::AND,Mode::ABS,4); def(0x3d,Op::AND,Mode::ABX,4,true); def(0x39,Op::AND,Mode::ABY,4,true);
        def(0x21,Op::AND,Mode::IZX,6); def(0x31,Op::AND,Mode::IZY,5,true);
        def(0x0a,Op::ASL,Mode::ACC,2); def(0x06,Op::ASL,Mode::ZP,5);  def(0x16,Op::ASL,Mode::ZPX,6);
        def(0x0e,Op::ASL,Mode::ABS,6); def(0x1e,Op::ASL,Mode::ABX,7);
        def(0x90,Op::BCC,Mode::REL,2); def(0xb0,Op::BCS,Mode::REL,2); def(0xf0,Op::BEQ,Mode::REL,2);
        def(0x30,Op::BMI,Mode::REL,2); def(0xd0,Op::BNE,Mode::REL,2); def(0x10,Op::BPL,Mode::REL,2);
        def(0x50,Op::BVC,Mode::REL,2); def(0x70,Op::BVS,Mode::REL,2);
        def(0x24,Op::BIT,Mode::ZP,3);  def(0x2c,Op::BIT,Mode::ABS,4);
        def(0x00,Op::BRK,Mode::IMP,7);
        def(0x18,Op::CLC,Mode::IMP,2); def(0xd8,Op::CLD,Mode::IMP,2); def(0x58,Op::CLI,Mode::IMP,2);
        def(0xb8,Op::CLV,Mode::IMP,2);
        def(0xc9,Op::CMP,Mode::IMM,2); def(0xc5,Op::CMP,Mode::ZP,3);  def(0xd5,Op::CMP,Mode::ZPX,4);
        def(0xcd,Op::CMP,Mode::ABS,4); def(0xdd,Op::CMP,Mode::ABX,4,true); def(0xd9,Op::CMP,Mode::ABY,4,true);
        def(0xc1,Op::CMP,Mode::IZX,6); def(0xd1,Op::CMP,Mode::IZY,5,true);
        def(0xe0,Op::CPX,Mode::IMM,2); def(0xe4,Op::CPX,Mode::ZP,3);  def(0xec,Op::CPX,Mode::ABS,4);
        def(0xc0,Op::CPY,Mode::IMM,2); def(0xc4,Op::CPY,Mode::ZP,3);  def(0xcc,Op::CPY,Mode::ABS,4);
        def(0xc6,Op::DEC,Mode::ZP,5);  def(0xd6,Op::DEC,Mode::ZPX,6); def(0xce,Op::DEC,Mode::ABS,6);
        def(0xde,Op::DEC,Mode::ABX,7);
        def(0xca,Op::DEX,Mode::IMP,2); def(0x88,Op::DEY,Mode::IMP,2);
        def(0x49,Op::EOR,Mode::IMM,2); def(0x45,Op::EOR,Mode::ZP,3);  def(0x55,Op::EOR,Mode::ZPX,4);
        def(0x4d,Op::EOR,Mode::ABS,4); def(0x5d,Op::EOR,Mode::ABX,4,true); def(0x59,Op::EOR,Mode::ABY,4,true);
        def(0x41,Op::EOR,Mode::IZX,6); def(0x51,Op::EOR,Mode::IZY,5,true);
        def(0xe6,Op::INC,Mode::ZP,5);  def(0xf6,Op::INC,Mode::ZPX,6); def(0xee,Op::INC,Mode::ABS,6);
        def(0xfe,Op::INC,Mode::ABX,7);
        def(0xe8,Op::INX,Mode::IMP,2); def(0xc8,Op::INY,Mode::IMP,2);
        def(0x4c,Op::JMP,Mode::ABS,3); def(0x6c,Op::JMP,Mode::IND,5);
        def(0x20,Op::JSR,Mode::ABS,6);
        def(0xa9,Op::LDA,Mode::IMM,2); def(0xa5,Op::LDA,Mode::ZP,3);  def(0xb5,Op::LDA,Mode::ZPX,4);
        def(0xad,Op::LDA,Mode::ABS,4); def(0xbd,Op::LDA,Mode::ABX,4,true); def(0xb9,Op::LDA,Mode::ABY,4,true);
        def(0xa1,Op::LDA,Mode::IZX,6); def(0xb1,Op::LDA,Mode::IZY,5,true);
        def(0xa2,Op::LDX,Mode::IMM,2); def(0xa6,Op::LDX,Mode::ZP,3);  def(0xb6,Op::LDX,Mode::ZPY,4);
        def(0xae,Op::LDX,Mode::ABS,4); def(0xbe,Op::LDX,Mode::ABY,4,true);
        def(0xa0,Op::LDY,Mode::IMM,2); def(0xa4,Op::LDY,Mode::ZP,3);  def(0xb4,Op::LDY,Mode::ZPX,4);
        def(0xac,Op::LDY,Mode::ABS,4); def(0xbc,Op::LDY,Mode::ABX,4,true);
        def(0x4a,Op::LSR,Mode::ACC,2); def(0x46,Op::LSR,Mode::ZP,5);  def(0x56,Op::LSR,Mode::ZPX,6);
        def(0x4e,Op::LSR,Mode::ABS,6); def(0x5e,Op::LSR,Mode::ABX,7);
        def(0xea,Op::NOP,Mode::IMP,2);
        def(0x09,Op::ORA,Mode::IMM,2); def(0x05,Op::ORA,Mode::ZP,3);  def(0x15,Op::ORA,Mode::ZPX,4);
        def(0x0d,Op::ORA,Mode::ABS,4); def(0x1d,Op::ORA,Mode::ABX,4,true); def(0x19,Op::ORA,Mode::ABY,4,true);
        def(0x01,Op::ORA,Mode::IZX,6); def(0x11,Op::ORA,Mode::IZY,5,true);
        def(0x48,Op::PHA,Mode::IMP,3); def(0x08,Op::PHP,Mode::IMP,3);
        def(0x68,Op::PLA,Mode::IMP,4); def(0x28,Op::PLP,Mode::IMP,4);
        def(0x2a,Op::ROL,Mode::ACC,2); def(0x26,Op::ROL,Mode::ZP,5);  def(0x36,Op::ROL,Mode::ZPX,6);
        def(0x2e,Op::ROL,Mode::ABS,6); def(0x3e,Op::ROL,Mode::ABX,7);
        def(0x6a,Op::ROR,Mode::ACC,2); def(0x66,Op::ROR,Mode::ZP,5);  def(0x76,Op::ROR,Mode::ZPX,6);
        def(0x6e,Op::ROR,Mode::ABS,6); def(0x7e,Op::ROR,Mode::ABX,7);
        def(0x40,Op::RTI,Mode::IMP,6); def(0x60,Op::RTS,Mode::IMP,6);
        def(0xe9,Op::SBC,Mode::IMM,2); def(0xe5,Op::SBC,Mode::ZP,3);  def(0xf5,Op::SBC,Mode::ZPX,4);
        def(0xed,Op::SBC,Mode::ABS,4); def(0xfd,Op::SBC,Mode::ABX,4,true); def(0xf9,Op::SBC,Mode::ABY,4,true);
        def(0xe1,Op::SBC,Mode::IZX,6); def(0xf1,Op::SBC,Mode::IZY,5,true);
        def(0x38,Op::SEC,Mode::IMP,2); def(0xf8,Op::SED,Mode::IMP,2); def(0x78,Op::SEI,Mode::IMP,2);
        def(0x85,Op::STA,Mode::ZP,3);  def(0x95,Op::STA,Mode::ZPX,4); def(0x8d,Op::STA,Mode::ABS,4);
        def(0x9d,Op::STA,Mode::ABX,5); def(0x99,Op::STA,Mode::ABY,5); def(0x81,Op::STA,Mode::IZX,6);
        def(0x91,Op::STA,Mode::IZY,6);
        def(0x86,Op::STX,Mode::ZP,3);  def(0x96,Op::STX,Mode::ZPY,4); def(0x8e,Op::STX,Mode::ABS,4);
        def(0x84,Op::STY,Mode::ZP,3);  def(0x94,Op::STY,Mode::ZPX,4); def(0x8c,Op::STY,Mode::ABS,4);
        def(0xaa,Op::TAX,Mode::IMP,2); def(0xa8,Op::TAY,Mode::IMP,2); def(0xba,Op::TSX,Mode::IMP,2);
        def(0x8a,Op::TXA,Mode::IMP,2); def(0x9a,Op::TXS,Mode::IMP,2); def(0x98,Op::TYA,Mode::IMP,2);

        // Unofficial NOPs
        for (int c : {0x1a,0x3a,0x5a,0x7a,0xda,0xfa}) def(c,Op::NOP,Mode::IMP,2);
        for (int c : {0x80,0x82,0x89,0xc2,0xe2})       def(c,Op::NOP,Mode::IMM,2);
        for (int c : {0x04,0x44,0x64})                  def(c,Op::NOP,Mode::ZP,3);
        for (int c : {0x14,0x34,0x54,0x74,0xd4,0xf4})  def(c,Op::NOP,Mode::ZPX,4);
        def(0x0c,Op::NOP,Mode::ABS,4);
        for (int c : {0x1c,0x3c,0x5c,0x7c,0xdc,0xfc})  def(c,Op::NOP,Mode::ABX,4,true);

        // LAX
        def(0xa7,Op::LAX,Mode::ZP,3);   def(0xb7,Op::LAX,Mode::ZPY,4); def(0xaf,Op::LAX,Mode::ABS,4);
        def(0xbf,Op::LAX,Mode::ABY,4,true); def(0xa3,Op::LAX,Mode::IZX,6); def(0xb3,Op::LAX,Mode::IZY,5,true);
        def(0xab,Op::LAX,Mode::IMM,2);
        // SAX
        def(0x87,Op::SAX,Mode::ZP,3);   def(0x97,Op::SAX,Mode::ZPY,4); def(0x8f,Op::SAX,Mode::ABS,4);
        def(0x83,Op::SAX,Mode::IZX,6);
        // SBC alias
        def(0xeb,Op::SBC,Mode::IMM,2);
        // DCP
        def(0xc7,Op::DCP,Mode::ZP,5);   def(0xd7,Op::DCP,Mode::ZPX,6); def(0xcf,Op::DCP,Mode::ABS,6);
        def(0xdf,Op::DCP,Mode::ABX,7);  def(0xdb,Op::DCP,Mode::ABY,7); def(0xc3,Op::DCP,Mode::IZX,8);
        def(0xd3,Op::DCP,Mode::IZY,8);
        // ISB
        def(0xe7,Op::ISB,Mode::ZP,5);   def(0xf7,Op::ISB,Mode::ZPX,6); def(0xef,Op::ISB,Mode::ABS,6);
        def(0xff,Op::ISB,Mode::ABX,7);  def(0xfb,Op::ISB,Mode::ABY,7); def(0xe3,Op::ISB,Mode::IZX,8);
        def(0xf3,Op::ISB,Mode::IZY,8);
        // SLO
        def(0x07,Op::SLO,Mode::ZP,5);   def(0x17,Op::SLO,Mode::ZPX,6); def(0x0f,Op::SLO,Mode::ABS,6);
        def(0x1f,Op::SLO,Mode::ABX,7);  def(0x1b,Op::SLO,Mode::ABY,7); def(0x03,Op::SLO,Mode::IZX,8);
        def(0x13,Op::SLO,Mode::IZY,8);
        // RLA
        def(0x27,Op::RLA,Mode::ZP,5);   def(0x37,Op::RLA,Mode::ZPX,6); def(0x2f,Op::RLA,Mode::ABS,6);
        def(0x3f,Op::RLA,Mode::ABX,7);  def(0x3b,Op::RLA,Mode::ABY,7); def(0x23,Op::RLA,Mode::IZX,8);
        def(0x33,Op::RLA,Mode::IZY,8);
        // SRE
        def(0x47,Op::SRE,Mode::ZP,5);   def(0x57,Op::SRE,Mode::ZPX,6); def(0x4f,Op::SRE,Mode::ABS,6);
        def(0x5f,Op::SRE,Mode::ABX,7);  def(0x5b,Op::SRE,Mode::ABY,7); def(0x43,Op::SRE,Mode::IZX,8);
        def(0x53,Op::SRE,Mode::IZY,8);
        // RRA
        def(0x67,Op::RRA,Mode::ZP,5);   def(0x77,Op::RRA,Mode::ZPX,6); def(0x6f,Op::RRA,Mode::ABS,6);
        def(0x7f,Op::RRA,Mode::ABX,7);  def(0x7b,Op::RRA,Mode::ABY,7); def(0x63,Op::RRA,Mode::IZX,8);
        def(0x73,Op::RRA,Mode::IZY,8);
        // Misc unofficial
        def(0x0b,Op::ANC,Mode::IMM,2); def(0x2b,Op::ANC,Mode::IMM,2);
        def(0x4b,Op::ALR,Mode::IMM,2); def(0x6b,Op::ARR,Mode::IMM,2);
        def(0xcb,Op::SBX,Mode::IMM,2); def(0xbb,Op::LAS,Mode::ABY,4,true);
        def(0x9c,Op::SHY,Mode::ABX,5); def(0x9e,Op::SHX,Mode::ABY,5);
        def(0x93,Op::SHA,Mode::IZY,6); def(0x9f,Op::SHA,Mode::ABY,5);
        def(0x9b,Op::TAS,Mode::ABY,5); def(0x8b,Op::XAA,Mode::IMM,2);
        // JAM
        for (int c : {0x02,0x12,0x22,0x32,0x42,0x52,0x62,0x72,0x92,0xb2,0xd2,0xf2})
            def(c,Op::JAM,Mode::IMP,2);
    }
} static tableInitInstance;

// ---------------------------------------------------------------------------

Cpu::Cpu(CpuBus* b) : bus(b) {}

void Cpu::powerOn() {
    a = x = y = 0;
    sp = 0xfd;
    p  = FU | FI;
    pc = read16(0xfffc);
    totalCycles = 7;
    nmiPending  = false;
    jammed      = false;
}

void Cpu::reset() {
    sp = (sp - 3) & 0xff;
    p |= FI;
    pc = read16(0xfffc);
    nmiPending = false;
    jammed     = false;
    totalCycles += 7;
}

void Cpu::nmi()              { nmiPending = true; }
void Cpu::setIrqLine(bool l) { irqLine = l; }

uint8_t  Cpu::fetch()         { uint8_t v = bus->read(pc); pc = (pc + 1) & 0xffff; return v; }
uint16_t Cpu::fetch16()       { return fetch() | (fetch() << 8); }
uint16_t Cpu::read16(uint16_t a) { return bus->read(a) | (bus->read((a + 1) & 0xffff) << 8); }
void     Cpu::push(uint8_t v) { bus->write(0x100 | sp, v); sp = (sp - 1) & 0xff; }
uint8_t  Cpu::pull()          { sp = (sp + 1) & 0xff; return bus->read(0x100 | sp); }

void Cpu::setZN(uint8_t v) {
    p = (p & ~(FZ | FN)) | (v == 0 ? FZ : 0) | (v & FN);
}
void Cpu::setFlag(uint8_t flag, bool on) {
    if (on) p |= flag; else p &= ~flag;
}

void Cpu::serviceInterrupt(uint16_t vector) {
    push(pc >> 8);
    push(pc & 0xff);
    push((p | FU) & ~FB);
    p |= FI;
    pc = read16(vector);
    totalCycles += 7;
}

int Cpu::branch(bool cond, uint16_t target) {
    if (!cond) return 0;
    bool crossed = (pc & 0xff00) != (target & 0xff00);
    pc = target;
    return crossed ? 2 : 1;
}

void Cpu::adc(uint8_t value) {
    int sum = a + value + (p & FC);
    setFlag(FC, sum > 0xff);
    setFlag(FV, (~(a ^ value) & (a ^ sum) & 0x80) != 0);
    a = sum & 0xff;
    setZN(a);
}
void Cpu::sbc(uint8_t value) { adc(value ^ 0xff); }
void Cpu::compare(uint8_t reg, uint8_t value) {
    uint8_t diff = (reg - value) & 0xff;
    setFlag(FC, reg >= value);
    setZN(diff);
}
uint8_t Cpu::doASL(uint8_t v) { setFlag(FC,(v&0x80)!=0); uint8_t r=(v<<1)&0xff; setZN(r); return r; }
uint8_t Cpu::doLSR(uint8_t v) { setFlag(FC,(v&1)!=0);    uint8_t r=v>>1;          setZN(r); return r; }
uint8_t Cpu::doROL(uint8_t v) {
    uint8_t ci = p & FC;
    setFlag(FC,(v&0x80)!=0);
    uint8_t r = ((v<<1)|ci)&0xff;
    setZN(r); return r;
}
uint8_t Cpu::doROR(uint8_t v) {
    uint8_t ci = (p & FC) << 7;
    setFlag(FC,(v&1)!=0);
    uint8_t r = (v>>1)|ci;
    setZN(r); return r;
}

int Cpu::step() {
    if (jammed) return 2;

    if (nmiPending) {
        nmiPending = false;
        serviceInterrupt(0xfffa);
        return 7;
    }
    if (irqLine && (p & FI) == 0) {
        serviceInterrupt(0xfffe);
        return 7;
    }

    uint8_t opcode = bus->read(pc);
    pc = (pc + 1) & 0xffff;
    const OpcodeInfo& d = TABLE[opcode];

    uint16_t addr        = 0;
    bool     pageCrossed = false;

    switch (d.mode) {
        case Mode::IMP:
        case Mode::ACC: break;
        case Mode::IMM: addr = pc; pc = (pc+1)&0xffff; break;
        case Mode::ZP:  addr = fetch(); break;
        case Mode::ZPX: addr = (fetch() + x) & 0xff; break;
        case Mode::ZPY: addr = (fetch() + y) & 0xff; break;
        case Mode::ABS: addr = fetch16(); break;
        case Mode::ABX: {
            uint16_t base = fetch16();
            addr = (base + x) & 0xffff;
            pageCrossed = (base & 0xff00) != (addr & 0xff00);
            break;
        }
        case Mode::ABY: {
            uint16_t base = fetch16();
            addr = (base + y) & 0xffff;
            pageCrossed = (base & 0xff00) != (addr & 0xff00);
            break;
        }
        case Mode::IND: {
            uint16_t ptr = fetch16();
            uint8_t  lo  = bus->read(ptr);
            uint8_t  hi  = bus->read((ptr & 0xff00) | ((ptr+1) & 0xff));
            addr = (hi << 8) | lo;
            break;
        }
        case Mode::IZX: {
            uint8_t zp = (fetch() + x) & 0xff;
            addr = bus->read(zp) | (bus->read((zp+1)&0xff) << 8);
            break;
        }
        case Mode::IZY: {
            uint8_t  zp   = fetch();
            uint16_t base = bus->read(zp) | (bus->read((zp+1)&0xff) << 8);
            addr = (base + y) & 0xffff;
            pageCrossed = (base & 0xff00) != (addr & 0xff00);
            break;
        }
        case Mode::REL: {
            uint8_t off = fetch();
            addr = (pc + (int8_t)off) & 0xffff;
            break;
        }
    }

    int cycles = d.cycles;
    if (d.page && pageCrossed) cycles++;

    // Execute
    switch (d.op) {
        case Op::ADC: adc(bus->read(addr)); break;
        case Op::SBC: sbc(bus->read(addr)); break;
        case Op::AND: a &= bus->read(addr); setZN(a); break;
        case Op::ORA: a |= bus->read(addr); setZN(a); break;
        case Op::EOR: a ^= bus->read(addr); setZN(a); break;
        case Op::ASL:
            if (d.mode==Mode::ACC) a=doASL(a);
            else bus->write(addr,doASL(bus->read(addr)));
            break;
        case Op::LSR:
            if (d.mode==Mode::ACC) a=doLSR(a);
            else bus->write(addr,doLSR(bus->read(addr)));
            break;
        case Op::ROL:
            if (d.mode==Mode::ACC) a=doROL(a);
            else bus->write(addr,doROL(bus->read(addr)));
            break;
        case Op::ROR:
            if (d.mode==Mode::ACC) a=doROR(a);
            else bus->write(addr,doROR(bus->read(addr)));
            break;
        case Op::BCC: cycles += branch((p&FC)==0, addr); break;
        case Op::BCS: cycles += branch((p&FC)!=0, addr); break;
        case Op::BNE: cycles += branch((p&FZ)==0, addr); break;
        case Op::BEQ: cycles += branch((p&FZ)!=0, addr); break;
        case Op::BPL: cycles += branch((p&FN)==0, addr); break;
        case Op::BMI: cycles += branch((p&FN)!=0, addr); break;
        case Op::BVC: cycles += branch((p&FV)==0, addr); break;
        case Op::BVS: cycles += branch((p&FV)!=0, addr); break;
        case Op::BIT: {
            uint8_t v = bus->read(addr);
            setFlag(FZ,(a&v)==0); setFlag(FN,v&FN); setFlag(FV,v&FV);
            break;
        }
        case Op::BRK:
            pc = (pc+1)&0xffff;
            push(pc>>8); push(pc&0xff);
            push(p|FB|FU);
            p |= FI;
            pc = read16(0xfffe);
            break;
        case Op::CLC: p &= ~FC; break; case Op::SEC: p |= FC; break;
        case Op::CLI: p &= ~FI; break; case Op::SEI: p |= FI; break;
        case Op::CLD: p &= ~FD; break; case Op::SED: p |= FD; break;
        case Op::CLV: p &= ~FV; break;
        case Op::CMP: compare(a, bus->read(addr)); break;
        case Op::CPX: compare(x, bus->read(addr)); break;
        case Op::CPY: compare(y, bus->read(addr)); break;
        case Op::DEC: { uint8_t v=(bus->read(addr)-1)&0xff; bus->write(addr,v); setZN(v); break; }
        case Op::INC: { uint8_t v=(bus->read(addr)+1)&0xff; bus->write(addr,v); setZN(v); break; }
        case Op::DEX: x=(x-1)&0xff; setZN(x); break;
        case Op::DEY: y=(y-1)&0xff; setZN(y); break;
        case Op::INX: x=(x+1)&0xff; setZN(x); break;
        case Op::INY: y=(y+1)&0xff; setZN(y); break;
        case Op::JMP: pc=addr; break;
        case Op::JSR: { uint16_t ret=(pc-1)&0xffff; push(ret>>8); push(ret&0xff); pc=addr; break; }
        case Op::RTS: { uint8_t lo=pull(),hi=pull(); pc=((hi<<8|lo)+1)&0xffff; break; }
        case Op::RTI: { p=(pull()&~FB)|FU; uint8_t lo=pull(),hi=pull(); pc=(hi<<8)|lo; break; }
        case Op::LDA: a=bus->read(addr); setZN(a); break;
        case Op::LDX: x=bus->read(addr); setZN(x); break;
        case Op::LDY: y=bus->read(addr); setZN(y); break;
        case Op::STA: bus->write(addr,a); break;
        case Op::STX: bus->write(addr,x); break;
        case Op::STY: bus->write(addr,y); break;
        case Op::NOP:
            if (d.mode!=Mode::IMP && d.mode!=Mode::IMM) bus->read(addr);
            break;
        case Op::PHA: push(a); break;
        case Op::PHP: push(p|FB|FU); break;
        case Op::PLA: a=pull(); setZN(a); break;
        case Op::PLP: p=(pull()&~FB)|FU; break;
        case Op::TAX: x=a; setZN(x); break;
        case Op::TAY: y=a; setZN(y); break;
        case Op::TSX: x=sp; setZN(x); break;
        case Op::TXA: a=x; setZN(a); break;
        case Op::TXS: sp=x; break;
        case Op::TYA: a=y; setZN(a); break;

        // Unofficial
        case Op::LAX: a=x=bus->read(addr); setZN(a); break;
        case Op::SAX: bus->write(addr,a&x); break;
        case Op::DCP: { uint8_t v=(bus->read(addr)-1)&0xff; bus->write(addr,v); compare(a,v); break; }
        case Op::ISB: { uint8_t v=(bus->read(addr)+1)&0xff; bus->write(addr,v); sbc(v); break; }
        case Op::SLO: { uint8_t v=doASL(bus->read(addr)); bus->write(addr,v); a|=v; setZN(a); break; }
        case Op::RLA: { uint8_t v=doROL(bus->read(addr)); bus->write(addr,v); a&=v; setZN(a); break; }
        case Op::SRE: { uint8_t v=doLSR(bus->read(addr)); bus->write(addr,v); a^=v; setZN(a); break; }
        case Op::RRA: { uint8_t v=doROR(bus->read(addr)); bus->write(addr,v); adc(v); break; }
        case Op::ANC: a&=bus->read(addr); setZN(a); setFlag(FC,(a&FN)!=0); break;
        case Op::ALR: a&=bus->read(addr); a=doLSR(a); break;
        case Op::ARR: {
            a &= bus->read(addr);
            a  = ((a>>1)|((p&FC)<<7))&0xff;
            setZN(a);
            setFlag(FC,(a&0x40)!=0);
            setFlag(FV,(((a>>6)^(a>>5))&1)!=0);
            break;
        }
        case Op::SBX: {
            uint8_t v=bus->read(addr);
            int r=(a&x)-v;
            setFlag(FC,r>=0);
            x=r&0xff; setZN(x);
            break;
        }
        case Op::LAS: { uint8_t v=bus->read(addr)&sp; a=x=sp=v; setZN(v); break; }
        case Op::SHY: bus->write(addr,y&(((addr>>8)+1)&0xff)); break;
        case Op::SHX: bus->write(addr,x&(((addr>>8)+1)&0xff)); break;
        case Op::SHA: bus->write(addr,a&x&(((addr>>8)+1)&0xff)); break;
        case Op::TAS: sp=a&x; bus->write(addr,sp&(((addr>>8)+1)&0xff)); break;
        case Op::XAA: a=x&bus->read(addr); setZN(a); break;
        case Op::JAM: jammed=true; pc=(pc-1)&0xffff; break;
    }

    totalCycles += cycles;
    return cycles;
}

nlohmann::json Cpu::getState() const {
    return {
        {"a",a},{"x",x},{"y",y},{"sp",sp},{"pc",pc},{"p",p},
        {"totalCycles",totalCycles},{"nmiPending",nmiPending},
        {"irqLine",irqLine},{"jammed",jammed}
    };
}
void Cpu::setState(const nlohmann::json& s) {
    a           = s["a"].get<uint8_t>();
    x           = s["x"].get<uint8_t>();
    y           = s["y"].get<uint8_t>();
    sp          = s["sp"].get<uint8_t>();
    pc          = s["pc"].get<uint16_t>();
    p           = s["p"].get<uint8_t>();
    totalCycles = s["totalCycles"].get<uint64_t>();
    nmiPending  = s["nmiPending"].get<bool>();
    irqLine     = s["irqLine"].get<bool>();
    jammed      = s["jammed"].get<bool>();
}
