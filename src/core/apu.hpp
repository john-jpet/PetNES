#pragma once
#include <cstdint>
#include <functional>
#include "bus.hpp"
#include "vendor/json.hpp"

class Apu : public ApuPort {
public:
    explicit Apu(int sampleRate = 44100);
    void tick();
    void setSampleRate(int sr);
    int  available() const;
    int  drainSamples(float* out, int maxSamples);
    int  takeStallCycles();
    bool irqAsserted() const;
    void setMemoryReader(std::function<uint8_t(uint16_t)> reader);

    // ApuPort
    uint8_t readStatus() override;
    void    writeRegister(uint16_t addr, uint8_t value) override;

    void setChannelMute(int ch, bool muted);

    nlohmann::json getState() const;
    void setState(const nlohmann::json& s);

private:
    bool mute_[5] = {}; // 0=pulse1 1=pulse2 2=tri 3=noise 4=dmc
    // --- Envelope ---
    struct Envelope {
        bool start = false, loop = false, constant = false;
        int volumeParam = 0, divider = 0, decay = 0;
        void clock();
        int  output() const { return constant ? volumeParam : decay; }
        nlohmann::json getState() const;
        void setState(const nlohmann::json& s);
    };

    // --- Pulse ---
    struct Pulse {
        bool enabled = false;
        int  lengthCounter = 0;
        Envelope envelope;
        int duty = 0, dutyPos = 0, timerPeriod = 0, timer = 0;
        bool sweepEnabled = false, sweepNegate = false, sweepReload = false;
        int  sweepPeriod = 0, sweepShift = 0, sweepDivider = 0;
        bool isPulse1;
        Pulse(bool p1) : isPulse1(p1) {}
        void writeCtrl(uint8_t v);
        void writeSweep(uint8_t v);
        void writeTimerLo(uint8_t v);
        void writeTimerHi(uint8_t v);
        void clockTimer();
        void clockSweep();
        void clockLength();
        int  sweepTarget() const;
        int  output() const;
        nlohmann::json getState() const;
        void setState(const nlohmann::json& s);
    };

    // --- Triangle ---
    struct Triangle {
        bool enabled = false;
        int  lengthCounter = 0, linearCounter = 0, linearReload = 0;
        bool linearReloadFlag = false, control = false;
        int  timerPeriod = 0, timer = 0, seqPos = 0;
        void writeCtrl(uint8_t v);
        void writeTimerLo(uint8_t v);
        void writeTimerHi(uint8_t v);
        void clockTimer();
        void clockLinear();
        void clockLength();
        int  output() const;
        nlohmann::json getState() const;
        void setState(const nlohmann::json& s);
    };

    // --- Noise ---
    struct Noise {
        bool enabled = false;
        int  lengthCounter = 0;
        Envelope envelope;
        bool mode = false;
        int  timerPeriod = 4, timer = 0, lfsr = 1;
        void writeCtrl(uint8_t v);
        void writePeriod(uint8_t v);
        void writeLength(uint8_t v);
        void clockTimer();
        void clockLength();
        int  output() const;
        nlohmann::json getState() const;
        void setState(const nlohmann::json& s);
    };

    // --- DMC ---
    struct Dmc {
        bool irqEnabled = false, irqFlag = false, loop = false;
        int  bytesRemaining = 0, outputLevel = 0, stallCycles = 0;
        int  timerPeriod = 428, timer = 0;
        int  sampleAddr = 0xc000, sampleLength = 1;
        int  currentAddr = 0xc000, sampleBuffer = -1;
        int  shiftReg = 0, bitsRemaining = 8;
        bool silence = true;
        std::function<uint8_t(uint16_t)> memoryReader;
        void writeCtrl(uint8_t v);
        void writeLoad(uint8_t v);
        void writeAddr(uint8_t v);
        void writeLength(uint8_t v);
        void restart();
        void fillBuffer();
        void clockTimer();
        nlohmann::json getState() const;
        void setState(const nlohmann::json& s);
    };

    Pulse    pulse1_{true}, pulse2_{false};
    Triangle triangle_;
    Noise    noise_;
    Dmc      dmc_;

    bool frameMode5_      = false;
    bool frameIrqInhibit_ = false;
    bool frameIrqFlag_    = false;
    int  frameCycle_      = 0;
    bool oddCycle_        = false;

    int    sampleRate_;
    double cyclesPerSample_;
    double sampleAcc_ = 0;

    static constexpr int RING = 16384;
    float ringBuf_[RING] = {};
    int   ringW_ = 0, ringR_ = 0;

    float hpLastIn_ = 0, hpLastOut_ = 0;

    void clockQuarter();
    void clockHalf();
    void emitSample();
};
