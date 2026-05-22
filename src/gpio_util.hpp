#pragma once

#include <lgpio.h>

#include <expected>
#include <string>

namespace gpio_util {

inline std::expected<int, std::string> open_chip(int chip) {
    int h = lgGpiochipOpen(chip);
    if (h < 0) [[unlikely]] {
        return std::unexpected(
            "lgGpiochipOpen(" + std::to_string(chip) + ") rc=" + std::to_string(h));
    }
    return h;
}

inline std::expected<void, std::string> claim_output(int handle, int pin, const char* name, int level = 0) {
    int rc = lgGpioClaimOutput(handle, 0, pin, level);
    if (rc < 0) [[unlikely]] {
        return std::unexpected(
            std::string("lgGpioClaimOutput ") + name + " pin=" + std::to_string(pin) +
            " rc=" + std::to_string(rc));
    }
    return {};
}

} // namespace gpio_util
