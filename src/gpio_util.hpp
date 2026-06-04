#pragma once

#include <lgpio.h>

#include <expected>
#include <format>
#include <string>

namespace gpio_util {

inline std::expected<int, std::string> open_chip(int chip) {
    int h = lgGpiochipOpen(chip);
    if (h < 0) [[unlikely]] {
        return std::unexpected(std::format("lgGpiochipOpen({}) rc={}", chip, h));
    }
    return h;
}

inline std::expected<void, std::string> claim_output(int handle, int pin, const char* name, int level = 0) {
    int rc = lgGpioClaimOutput(handle, 0, pin, level);
    if (rc < 0) [[unlikely]] {
        return std::unexpected(std::format("lgGpioClaimOutput {} pin={} rc={}", name, pin, rc));
    }
    return {};
}

} // namespace gpio_util
