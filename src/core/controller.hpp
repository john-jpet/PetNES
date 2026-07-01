#pragma once
#include <cstdint>
#include "vendor/json.hpp"

enum class Button : int { A=0, B=1, Select=2, Start=3, Up=4, Down=5, Left=6, Right=7 };

class Controller {
public:
    void setButton(Button btn, bool pressed);
    void write(uint8_t value);
    uint8_t read();
    nlohmann::json getState() const;
    void setState(const nlohmann::json& s);
private:
    uint8_t buttons = 0;
    uint8_t shift   = 0;
    bool    strobe  = false;
};
