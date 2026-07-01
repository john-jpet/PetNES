#include <emscripten.h>
#include "core/nes.hpp"
#include "core/ppu.hpp"
#include <cstring>
#include <cstdlib>

extern "C" {

EMSCRIPTEN_KEEPALIVE
void* nes_create(const uint8_t* rom_data, int rom_len) {
    try { return new Nes(rom_data, (size_t)rom_len); }
    catch (...) { return nullptr; }
}

EMSCRIPTEN_KEEPALIVE
void nes_destroy(void* handle) { delete static_cast<Nes*>(handle); }

EMSCRIPTEN_KEEPALIVE
void nes_step_frame(void* handle) { static_cast<Nes*>(handle)->stepFrame(); }

EMSCRIPTEN_KEEPALIVE
uint32_t* nes_get_framebuffer(void* handle) { return static_cast<Nes*>(handle)->framebuffer(); }

EMSCRIPTEN_KEEPALIVE
int nes_get_framebuffer_size() { return 256 * 240 * 4; }

EMSCRIPTEN_KEEPALIVE
void nes_set_sample_rate(void* handle, int sample_rate) {
    static_cast<Nes*>(handle)->apu.setSampleRate(sample_rate);
}

EMSCRIPTEN_KEEPALIVE
void nes_reset(void* handle) { static_cast<Nes*>(handle)->reset(); }

EMSCRIPTEN_KEEPALIVE
void nes_controller_set(void* handle, int controller, uint8_t button_mask) {
    Nes* nes = static_cast<Nes*>(handle);
    Controller& ctrl = (controller == 0) ? nes->controller1 : nes->controller2;
    for (int i = 0; i < 8; i++)
        ctrl.setButton(static_cast<Button>(i), (button_mask >> i) & 1);
}

EMSCRIPTEN_KEEPALIVE
int nes_apu_drain(void* handle, float* out_ptr, int max_samples) {
    return static_cast<Nes*>(handle)->apu.drainSamples(out_ptr, max_samples);
}

EMSCRIPTEN_KEEPALIVE
char* nes_serialize(void* handle) {
    std::string json = static_cast<Nes*>(handle)->serialize();
    char* buf = static_cast<char*>(malloc(json.size() + 1));
    memcpy(buf, json.c_str(), json.size() + 1);
    return buf;
}

EMSCRIPTEN_KEEPALIVE
void nes_deserialize(void* handle, const char* json_str) {
    static_cast<Nes*>(handle)->deserialize(std::string(json_str));
}

EMSCRIPTEN_KEEPALIVE
void nes_free_string(char* ptr) { free(ptr); }

EMSCRIPTEN_KEEPALIVE
void nes_set_channel_mute(void* handle, int channel, int muted) {
    static_cast<Nes*>(handle)->apu.setChannelMute(channel, muted != 0);
}

EMSCRIPTEN_KEEPALIVE
void nes_set_palette_entry(int idx, int r, int g, int b) {
    setPaletteEntry(idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

} // extern "C"
