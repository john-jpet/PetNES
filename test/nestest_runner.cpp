/**
 * Headless nestest runner. Compares CPU state against the official nestest.log
 * line by line. Pass: nestest-runner <nestest.nes> <nestest.log>
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include "../src/core/cartridge.hpp"
#include "../src/core/mapper.hpp"
#include "../src/core/controller.hpp"
#include "../src/core/bus.hpp"
#include "../src/core/cpu.hpp"

// Minimal stubs so Bus compiles without real PPU/APU
struct NullPpu : PpuPort {
    uint8_t readRegister(int) override { return 0; }
    void writeRegister(int, uint8_t) override {}
    void writeOam(int, uint8_t) override {}
    int  oamAddr() const override { return 0; }
};
struct NullApu : ApuPort {
    uint8_t readStatus() override { return 0; }
    void writeRegister(uint16_t, uint8_t) override {}
};

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); exit(1); }
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read((char*)buf.data(), sz);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: nestest-runner <nestest.nes> <nestest.log>\n");
        return 1;
    }

    auto romData = readFile(argv[1]);
    Cartridge    cart  = parseINes(romData.data(), romData.size());
    auto         mapperPtr = createMapper(cart);
    Controller   c1, c2;
    Bus          bus(mapperPtr.get(), &c1, &c2);
    NullPpu ppu; NullApu apu;
    bus.ppu = &ppu; bus.apu = &apu;
    Cpu cpu(&bus);

    cpu.powerOn();
    cpu.pc          = 0xc000;
    cpu.p           = 0x24;
    cpu.sp          = 0xfd;
    cpu.totalCycles = 7;

    // Parse log lines: "C000  4C F5 C5  JMP $C5F5 ... A:00 X:00 Y:00 P:24 SP:FD CYC:  7"
    std::ifstream log(argv[2]);
    if (!log) { fprintf(stderr, "Cannot open %s\n", argv[2]); return 1; }

    // Regex to pull out pc, A, X, Y, P, SP, CYC
    static const std::regex re(
        R"(^([0-9A-F]{4}).*A:([0-9A-F]{2}) X:([0-9A-F]{2}) Y:([0-9A-F]{2}) P:([0-9A-F]{2}) SP:([0-9A-F]{2}) CYC:\s*(\d+))",
        std::regex::icase
    );

    int lineNo = 0, mismatches = 0;
    std::string line;
    while (std::getline(log, line)) {
        lineNo++;
        std::smatch m;
        if (!std::regex_search(line, m, re)) continue;

        uint16_t exp_pc  = (uint16_t)std::stoi(m[1], nullptr, 16);
        uint8_t  exp_a   = (uint8_t) std::stoi(m[2], nullptr, 16);
        uint8_t  exp_x   = (uint8_t) std::stoi(m[3], nullptr, 16);
        uint8_t  exp_y   = (uint8_t) std::stoi(m[4], nullptr, 16);
        uint8_t  exp_p   = (uint8_t) std::stoi(m[5], nullptr, 16);
        uint8_t  exp_sp  = (uint8_t) std::stoi(m[6], nullptr, 16);
        int      exp_cyc = std::stoi(m[7]);

        bool ok = cpu.pc == exp_pc && cpu.a == exp_a && cpu.x == exp_x &&
                  cpu.y == exp_y && cpu.p == exp_p && cpu.sp == exp_sp &&
                  (int)(cpu.totalCycles % 341) * 3 % 341 == exp_cyc % 341;

        // Simple cycle check: totalCycles should match
        bool cyc_ok = (int)cpu.totalCycles == exp_cyc;
        bool state_ok = cpu.pc == exp_pc && cpu.a == exp_a && cpu.x == exp_x &&
                        cpu.y == exp_y && cpu.p == exp_p && cpu.sp == exp_sp;

        if (!state_ok || !cyc_ok) {
            if (mismatches == 0) fprintf(stderr, "First mismatch at log line %d:\n", lineNo);
            if (mismatches < 5) {
                fprintf(stderr, "  Expected: PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X CYC=%d\n",
                    exp_pc, exp_a, exp_x, exp_y, exp_p, exp_sp, exp_cyc);
                fprintf(stderr, "  Got:      PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X CYC=%llu\n",
                    cpu.pc, cpu.a, cpu.x, cpu.y, cpu.p, cpu.sp, (unsigned long long)cpu.totalCycles);
            }
            mismatches++;
            if (mismatches >= 20) { fprintf(stderr, "Too many mismatches, stopping.\n"); break; }
        }

        cpu.step();
    }

    printf("nestest: %d lines checked, %d mismatches\n", lineNo, mismatches);
    printf("$02=%02X  $03=%02X\n", bus.read(0x02), bus.read(0x03));

    return mismatches == 0 ? 0 : 1;
}
