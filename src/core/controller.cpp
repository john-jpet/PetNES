#include "controller.hpp"

void Controller::setButton(Button btn, bool pressed) {
    if (pressed) buttons |= 1 << (int)btn;
    else         buttons &= ~(1 << (int)btn);
}

void Controller::write(uint8_t value) {
    strobe = (value & 1) != 0;
    if (strobe) shift = buttons;
}

uint8_t Controller::read() {
    if (strobe) return buttons & 1;
    uint8_t bit = shift & 1;
    shift = (shift >> 1) | 0x80; // reads past 8 return 1
    return bit;
}

nlohmann::json Controller::getState() const {
    return { {"buttons", buttons}, {"shift", shift}, {"strobe", strobe} };
}
void Controller::setState(const nlohmann::json& s) {
    buttons = s["buttons"].get<uint8_t>();
    shift   = s["shift"].get<uint8_t>();
    strobe  = s["strobe"].get<bool>();
}
