#include "apu.hpp"
#include <algorithm>
#include <cmath>

static const int LENGTH_TABLE[32] = {
    10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
    12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30,
};
static const uint8_t DUTY_TABLE[4][8] = {
    {0,1,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,1,1,1,1,0,0,0},
    {1,0,0,1,1,1,1,1},
};
static const int TRIANGLE_SEQ[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
};
static const int NOISE_PERIODS[16] = {
    4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
};
static const int DMC_PERIODS[16] = {
    428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54
};

static float PULSE_MIX[31];
static float TND_MIX[203];
struct MixInit {
    MixInit() {
        for (int i = 1; i < 31;  i++) PULSE_MIX[i] = 95.52f  / (8128.0f  / i + 100.0f);
        for (int i = 1; i < 203; i++) TND_MIX[i]   = 163.67f / (24329.0f / i + 100.0f);
    }
} static mixInit;

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------
void Apu::Envelope::clock() {
    if (start) {
        start = false; decay = 15; divider = volumeParam;
    } else if (divider > 0) {
        divider--;
    } else {
        divider = volumeParam;
        if (decay > 0) decay--;
        else if (loop) decay = 15;
    }
}
nlohmann::json Apu::Envelope::getState() const {
    return {{"start",start},{"loop",loop},{"constant",constant},
            {"volumeParam",volumeParam},{"divider",divider},{"decay",decay}};
}
void Apu::Envelope::setState(const nlohmann::json& s) {
    start       = s["start"].get<bool>();
    loop        = s["loop"].get<bool>();
    constant    = s["constant"].get<bool>();
    volumeParam = s["volumeParam"].get<int>();
    divider     = s["divider"].get<int>();
    decay       = s["decay"].get<int>();
}

// ---------------------------------------------------------------------------
// Pulse
// ---------------------------------------------------------------------------
void Apu::Pulse::writeCtrl(uint8_t v) {
    duty = (v >> 6) & 3;
    envelope.loop     = (v & 0x20) != 0;
    envelope.constant = (v & 0x10) != 0;
    envelope.volumeParam = v & 0x0f;
}
void Apu::Pulse::writeSweep(uint8_t v) {
    sweepEnabled = (v & 0x80) != 0;
    sweepPeriod  = (v >> 4) & 7;
    sweepNegate  = (v & 0x08) != 0;
    sweepShift   = v & 7;
    sweepReload  = true;
}
void Apu::Pulse::writeTimerLo(uint8_t v) {
    timerPeriod = (timerPeriod & 0x700) | v;
}
void Apu::Pulse::writeTimerHi(uint8_t v) {
    timerPeriod = (timerPeriod & 0xff) | ((v & 7) << 8);
    if (enabled) lengthCounter = LENGTH_TABLE[v >> 3];
    dutyPos = 0;
    envelope.start = true;
}
void Apu::Pulse::clockTimer() {
    if (timer == 0) { timer = timerPeriod; dutyPos = (dutyPos + 1) & 7; }
    else timer--;
}
int Apu::Pulse::sweepTarget() const {
    int change = timerPeriod >> sweepShift;
    if (sweepNegate) return timerPeriod - change - (isPulse1 ? 1 : 0);
    return timerPeriod + change;
}
void Apu::Pulse::clockSweep() {
    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0) {
        int target = sweepTarget();
        if (timerPeriod >= 8 && target <= 0x7ff && target >= 0)
            timerPeriod = target;
    }
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod; sweepReload = false;
    } else sweepDivider--;
}
void Apu::Pulse::clockLength() {
    if (lengthCounter > 0 && !envelope.loop) lengthCounter--;
}
int Apu::Pulse::output() const {
    if (!enabled || lengthCounter == 0) return 0;
    if (timerPeriod < 8 || sweepTarget() > 0x7ff) return 0;
    if (DUTY_TABLE[duty][dutyPos] == 0) return 0;
    return envelope.output();
}
nlohmann::json Apu::Pulse::getState() const {
    return {{"enabled",enabled},{"lengthCounter",lengthCounter},{"envelope",envelope.getState()},
            {"duty",duty},{"dutyPos",dutyPos},{"timerPeriod",timerPeriod},{"timer",timer},
            {"sweepEnabled",sweepEnabled},{"sweepPeriod",sweepPeriod},{"sweepNegate",sweepNegate},
            {"sweepShift",sweepShift},{"sweepDivider",sweepDivider},{"sweepReload",sweepReload}};
}
void Apu::Pulse::setState(const nlohmann::json& s) {
    enabled       = s["enabled"].get<bool>();
    lengthCounter = s["lengthCounter"].get<int>();
    envelope.setState(s["envelope"]);
    duty          = s["duty"].get<int>();
    dutyPos       = s["dutyPos"].get<int>();
    timerPeriod   = s["timerPeriod"].get<int>();
    timer         = s["timer"].get<int>();
    sweepEnabled  = s["sweepEnabled"].get<bool>();
    sweepPeriod   = s["sweepPeriod"].get<int>();
    sweepNegate   = s["sweepNegate"].get<bool>();
    sweepShift    = s["sweepShift"].get<int>();
    sweepDivider  = s["sweepDivider"].get<int>();
    sweepReload   = s["sweepReload"].get<bool>();
}

// ---------------------------------------------------------------------------
// Triangle
// ---------------------------------------------------------------------------
void Apu::Triangle::writeCtrl(uint8_t v) {
    control = (v & 0x80) != 0;
    linearReload = v & 0x7f;
}
void Apu::Triangle::writeTimerLo(uint8_t v) { timerPeriod = (timerPeriod & 0x700) | v; }
void Apu::Triangle::writeTimerHi(uint8_t v) {
    timerPeriod = (timerPeriod & 0xff) | ((v & 7) << 8);
    if (enabled) lengthCounter = LENGTH_TABLE[v >> 3];
    linearReloadFlag = true;
}
void Apu::Triangle::clockTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        if (lengthCounter > 0 && linearCounter > 0)
            seqPos = (seqPos + 1) & 31;
    } else timer--;
}
void Apu::Triangle::clockLinear() {
    if (linearReloadFlag) linearCounter = linearReload;
    else if (linearCounter > 0) linearCounter--;
    if (!control) linearReloadFlag = false;
}
void Apu::Triangle::clockLength() {
    if (lengthCounter > 0 && !control) lengthCounter--;
}
int Apu::Triangle::output() const {
    if (!enabled || lengthCounter == 0 || linearCounter == 0)
        return TRIANGLE_SEQ[seqPos];
    if (timerPeriod < 2) return 7;
    return TRIANGLE_SEQ[seqPos];
}
nlohmann::json Apu::Triangle::getState() const {
    return {{"enabled",enabled},{"lengthCounter",lengthCounter},{"linearCounter",linearCounter},
            {"linearReload",linearReload},{"linearReloadFlag",linearReloadFlag},{"control",control},
            {"timerPeriod",timerPeriod},{"timer",timer},{"seqPos",seqPos}};
}
void Apu::Triangle::setState(const nlohmann::json& s) {
    enabled          = s["enabled"].get<bool>();
    lengthCounter    = s["lengthCounter"].get<int>();
    linearCounter    = s["linearCounter"].get<int>();
    linearReload     = s["linearReload"].get<int>();
    linearReloadFlag = s["linearReloadFlag"].get<bool>();
    control          = s["control"].get<bool>();
    timerPeriod      = s["timerPeriod"].get<int>();
    timer            = s["timer"].get<int>();
    seqPos           = s["seqPos"].get<int>();
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------
void Apu::Noise::writeCtrl(uint8_t v) {
    envelope.loop     = (v & 0x20) != 0;
    envelope.constant = (v & 0x10) != 0;
    envelope.volumeParam = v & 0x0f;
}
void Apu::Noise::writePeriod(uint8_t v) {
    mode = (v & 0x80) != 0;
    timerPeriod = NOISE_PERIODS[v & 0x0f];
}
void Apu::Noise::writeLength(uint8_t v) {
    if (enabled) lengthCounter = LENGTH_TABLE[v >> 3];
    envelope.start = true;
}
void Apu::Noise::clockTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        int feedback = (lfsr & 1) ^ ((lfsr >> (mode ? 6 : 1)) & 1);
        lfsr = (lfsr >> 1) | (feedback << 14);
    } else timer--;
}
void Apu::Noise::clockLength() {
    if (lengthCounter > 0 && !envelope.loop) lengthCounter--;
}
int Apu::Noise::output() const {
    if (!enabled || lengthCounter == 0) return 0;
    if (lfsr & 1) return 0;
    return envelope.output();
}
nlohmann::json Apu::Noise::getState() const {
    return {{"enabled",enabled},{"lengthCounter",lengthCounter},{"envelope",envelope.getState()},
            {"mode",mode},{"timerPeriod",timerPeriod},{"timer",timer},{"lfsr",lfsr}};
}
void Apu::Noise::setState(const nlohmann::json& s) {
    enabled       = s["enabled"].get<bool>();
    lengthCounter = s["lengthCounter"].get<int>();
    envelope.setState(s["envelope"]);
    mode        = s["mode"].get<bool>();
    timerPeriod = s["timerPeriod"].get<int>();
    timer       = s["timer"].get<int>();
    lfsr        = s["lfsr"].get<int>();
}

// ---------------------------------------------------------------------------
// DMC
// ---------------------------------------------------------------------------
void Apu::Dmc::writeCtrl(uint8_t v) {
    irqEnabled  = (v & 0x80) != 0;
    loop        = (v & 0x40) != 0;
    timerPeriod = DMC_PERIODS[v & 0x0f];
    if (!irqEnabled) irqFlag = false;
}
void Apu::Dmc::writeLoad(uint8_t v)   { outputLevel = v & 0x7f; }
void Apu::Dmc::writeAddr(uint8_t v)   { sampleAddr  = 0xc000 + v * 64; }
void Apu::Dmc::writeLength(uint8_t v) { sampleLength = v * 16 + 1; }
void Apu::Dmc::restart() {
    currentAddr     = sampleAddr;
    bytesRemaining  = sampleLength;
}
void Apu::Dmc::fillBuffer() {
    if (sampleBuffer >= 0 || bytesRemaining == 0) return;
    sampleBuffer = memoryReader((uint16_t)currentAddr);
    stallCycles += 4;
    currentAddr = (currentAddr == 0xffff) ? 0x8000 : currentAddr + 1;
    if (--bytesRemaining == 0) {
        if (loop) restart();
        else if (irqEnabled) irqFlag = true;
    }
}
void Apu::Dmc::clockTimer() {
    fillBuffer();
    if (timer > 0) { timer--; return; }
    timer = timerPeriod - 1;
    if (!silence) {
        if (shiftReg & 1) { if (outputLevel <= 125) outputLevel += 2; }
        else               { if (outputLevel >= 2)  outputLevel -= 2; }
    }
    shiftReg >>= 1;
    if (--bitsRemaining == 0) {
        bitsRemaining = 8;
        if (sampleBuffer < 0) {
            silence = true;
        } else {
            silence = false;
            shiftReg     = sampleBuffer;
            sampleBuffer = -1;
        }
    }
}
nlohmann::json Apu::Dmc::getState() const {
    return {{"irqEnabled",irqEnabled},{"irqFlag",irqFlag},{"loop",loop},
            {"bytesRemaining",bytesRemaining},{"outputLevel",outputLevel},
            {"timerPeriod",timerPeriod},{"timer",timer},
            {"sampleAddr",sampleAddr},{"sampleLength",sampleLength},
            {"currentAddr",currentAddr},{"sampleBuffer",sampleBuffer},
            {"shiftReg",shiftReg},{"bitsRemaining",bitsRemaining},{"silence",silence}};
}
void Apu::Dmc::setState(const nlohmann::json& s) {
    irqEnabled     = s["irqEnabled"].get<bool>();
    irqFlag        = s["irqFlag"].get<bool>();
    loop           = s["loop"].get<bool>();
    bytesRemaining = s["bytesRemaining"].get<int>();
    outputLevel    = s["outputLevel"].get<int>();
    timerPeriod    = s["timerPeriod"].get<int>();
    timer          = s["timer"].get<int>();
    sampleAddr     = s["sampleAddr"].get<int>();
    sampleLength   = s["sampleLength"].get<int>();
    currentAddr    = s["currentAddr"].get<int>();
    sampleBuffer   = s["sampleBuffer"].get<int>();
    shiftReg       = s["shiftReg"].get<int>();
    bitsRemaining  = s["bitsRemaining"].get<int>();
    silence        = s["silence"].get<bool>();
}

// ---------------------------------------------------------------------------
// APU top
// ---------------------------------------------------------------------------
Apu::Apu(int sr) : sampleRate_(sr), cyclesPerSample_(1789773.0 / sr) {}

void Apu::setSampleRate(int sr) {
    sampleRate_      = sr;
    cyclesPerSample_ = 1789773.0 / sr;
}

void Apu::setMemoryReader(std::function<uint8_t(uint16_t)> reader) {
    dmc_.memoryReader = reader;
}

int  Apu::takeStallCycles() { int s = dmc_.stallCycles; dmc_.stallCycles = 0; return s; }
bool Apu::irqAsserted() const { return frameIrqFlag_ || dmc_.irqFlag; }

void Apu::clockQuarter() {
    pulse1_.envelope.clock();
    pulse2_.envelope.clock();
    noise_.envelope.clock();
    triangle_.clockLinear();
}
void Apu::clockHalf() {
    pulse1_.clockLength();  pulse1_.clockSweep();
    pulse2_.clockLength();  pulse2_.clockSweep();
    triangle_.clockLength();
    noise_.clockLength();
}

void Apu::writeRegister(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0x4000: pulse1_.writeCtrl(value);    break;
        case 0x4001: pulse1_.writeSweep(value);   break;
        case 0x4002: pulse1_.writeTimerLo(value); break;
        case 0x4003: pulse1_.writeTimerHi(value); break;
        case 0x4004: pulse2_.writeCtrl(value);    break;
        case 0x4005: pulse2_.writeSweep(value);   break;
        case 0x4006: pulse2_.writeTimerLo(value); break;
        case 0x4007: pulse2_.writeTimerHi(value); break;
        case 0x4008: triangle_.writeCtrl(value);    break;
        case 0x400a: triangle_.writeTimerLo(value); break;
        case 0x400b: triangle_.writeTimerHi(value); break;
        case 0x400c: noise_.writeCtrl(value);   break;
        case 0x400e: noise_.writePeriod(value); break;
        case 0x400f: noise_.writeLength(value); break;
        case 0x4010: dmc_.writeCtrl(value);   break;
        case 0x4011: dmc_.writeLoad(value);   break;
        case 0x4012: dmc_.writeAddr(value);   break;
        case 0x4013: dmc_.writeLength(value); break;
        case 0x4015:
            pulse1_.enabled   = (value & 0x01) != 0;
            pulse2_.enabled   = (value & 0x02) != 0;
            triangle_.enabled = (value & 0x04) != 0;
            noise_.enabled    = (value & 0x08) != 0;
            if (!pulse1_.enabled)   pulse1_.lengthCounter   = 0;
            if (!pulse2_.enabled)   pulse2_.lengthCounter   = 0;
            if (!triangle_.enabled) triangle_.lengthCounter = 0;
            if (!noise_.enabled)    noise_.lengthCounter    = 0;
            if (value & 0x10) { if (dmc_.bytesRemaining == 0) dmc_.restart(); }
            else               { dmc_.bytesRemaining = 0; }
            dmc_.irqFlag = false;
            break;
        case 0x4017:
            frameMode5_      = (value & 0x80) != 0;
            frameIrqInhibit_ = (value & 0x40) != 0;
            if (frameIrqInhibit_) frameIrqFlag_ = false;
            frameCycle_ = 0;
            if (frameMode5_) { clockQuarter(); clockHalf(); }
            break;
    }
}

uint8_t Apu::readStatus() {
    uint8_t v = 0;
    if (pulse1_.lengthCounter   > 0) v |= 0x01;
    if (pulse2_.lengthCounter   > 0) v |= 0x02;
    if (triangle_.lengthCounter > 0) v |= 0x04;
    if (noise_.lengthCounter    > 0) v |= 0x08;
    if (dmc_.bytesRemaining     > 0) v |= 0x10;
    if (frameIrqFlag_) v |= 0x40;
    if (dmc_.irqFlag)  v |= 0x80;
    frameIrqFlag_ = false;
    return v;
}

void Apu::tick() {
    frameCycle_++;
    if (!frameMode5_) {
        switch (frameCycle_) {
            case  7457: clockQuarter(); break;
            case 14913: clockQuarter(); clockHalf(); break;
            case 22371: clockQuarter(); break;
            case 29829: clockQuarter(); clockHalf();
                if (!frameIrqInhibit_) frameIrqFlag_ = true; break;
            case 29830: frameCycle_ = 0; break;
        }
    } else {
        switch (frameCycle_) {
            case  7457: clockQuarter(); break;
            case 14913: clockQuarter(); clockHalf(); break;
            case 22371: clockQuarter(); break;
            case 37281: clockQuarter(); clockHalf(); break;
            case 37282: frameCycle_ = 0; break;
        }
    }

    triangle_.clockTimer();
    dmc_.clockTimer();
    oddCycle_ = !oddCycle_;
    if (oddCycle_) {
        pulse1_.clockTimer();
        pulse2_.clockTimer();
        noise_.clockTimer();
    }

    sampleAcc_++;
    if (sampleAcc_ >= cyclesPerSample_) {
        sampleAcc_ -= cyclesPerSample_;
        emitSample();
    }
}

void Apu::setChannelMute(int ch, bool muted) {
    if (ch >= 0 && ch < 5) mute_[ch] = muted;
}

void Apu::emitSample() {
    float p   = PULSE_MIX[(mute_[0] ? 0 : pulse1_.output()) + (mute_[1] ? 0 : pulse2_.output())];
    float tnd = TND_MIX[3 * (mute_[2] ? 0 : triangle_.output()) +
                         2 * (mute_[3] ? 0 : noise_.output()) +
                             (mute_[4] ? 0 : dmc_.outputLevel)];
    float raw = p + tnd;
    float sample = raw - hpLastIn_ + 0.995f * hpLastOut_;
    hpLastIn_  = raw;
    hpLastOut_ = sample;
    int next = (ringW_ + 1) % RING;
    if (next != ringR_) { ringBuf_[ringW_] = sample; ringW_ = next; }
}

int Apu::available() const {
    return (ringW_ - ringR_ + RING) % RING;
}

int Apu::drainSamples(float* out, int maxSamples) {
    int n = 0;
    while (n < maxSamples && ringR_ != ringW_) {
        out[n++] = ringBuf_[ringR_];
        ringR_   = (ringR_ + 1) % RING;
    }
    return n;
}

nlohmann::json Apu::getState() const {
    return {{"pulse1",pulse1_.getState()},{"pulse2",pulse2_.getState()},
            {"triangle",triangle_.getState()},{"noise",noise_.getState()},
            {"dmc",dmc_.getState()},
            {"frameMode5",frameMode5_},{"frameIrqInhibit",frameIrqInhibit_},
            {"frameIrqFlag",frameIrqFlag_},{"frameCycle",frameCycle_},
            {"oddCycle",oddCycle_}};
}
void Apu::setState(const nlohmann::json& s) {
    pulse1_.setState(s["pulse1"]);
    pulse2_.setState(s["pulse2"]);
    triangle_.setState(s["triangle"]);
    noise_.setState(s["noise"]);
    dmc_.setState(s["dmc"]);
    frameMode5_      = s["frameMode5"].get<bool>();
    frameIrqInhibit_ = s["frameIrqInhibit"].get<bool>();
    frameIrqFlag_    = s["frameIrqFlag"].get<bool>();
    frameCycle_      = s["frameCycle"].get<int>();
    oddCycle_        = s["oddCycle"].get<bool>();
}
