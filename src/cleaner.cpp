#include "cleaner.hpp"

#include <lgpio.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace cleaner {
namespace {

// Pi 5 exposes the 40-pin header on /dev/gpiochip4.
constexpr int kGpioChip = 4;
constexpr int kPwmFrequencyHz = 100;

int attr_int(const viam::sdk::ProtoStruct& attrs, const std::string& key) {
    auto it = attrs.find(key);
    if (it == attrs.end()) [[unlikely]] {
        throw std::runtime_error("missing required attribute: " + key);
    }
    if (it->second.is_a<double>()) {
        return static_cast<int>(it->second.get_unchecked<double>());
    }
    throw std::runtime_error("attribute " + key + " is not a number");
}

} // namespace

class TwoPinMotor {
public:
    TwoPinMotor(int chip_handle, int forward_pin, int backward_pin)
        : handle_(chip_handle), fwd_(forward_pin), bwd_(backward_pin) {
        claim(fwd_, "fwd");
        try {
            claim(bwd_, "bwd");
        } catch (...) {
            lgGpioFree(handle_, fwd_);
            throw;
        }
    }

    ~TwoPinMotor() {
        lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0, 0, 0);
        lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0, 0, 0);
        lgGpioFree(handle_, fwd_);
        lgGpioFree(handle_, bwd_);
    }

    TwoPinMotor(const TwoPinMotor&) = delete;
    TwoPinMotor& operator=(const TwoPinMotor&) = delete;

    void set_value(double v) {
        v = std::clamp(v, -1.0, 1.0);
        const double duty = std::abs(v) * 100.0;
        if (v == 0.0) {
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0, 0, 0);
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0, 0, 0);
        } else if (v > 0.0) {
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0,  0, 0);
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, duty, 0, 0);
        } else {
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0,  0, 0);
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, duty, 0, 0);
        }
    }

private:
    int handle_;
    int fwd_;
    int bwd_;

    void claim(int pin, const char* name) {
        int rc = lgGpioClaimOutput(handle_, 0, pin, 0);
        if (rc < 0) [[unlikely]] {
            throw std::runtime_error(
                std::string("lgGpioClaimOutput ") + name +
                " pin=" + std::to_string(pin) +
                " rc=" + std::to_string(rc));
        }
    }
};

Cleaner::Cleaner([[maybe_unused]] const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg)
    : viam::sdk::Motor(cfg.name()) {
    chip_handle_ = lgGpiochipOpen(kGpioChip);
    if (chip_handle_ < 0) [[unlikely]] {
        throw std::runtime_error(
            "lgGpiochipOpen(" + std::to_string(kGpioChip) +
            ") rc=" + std::to_string(chip_handle_));
    }
    try {
        const auto& attrs = cfg.attributes();
        const int fwd = attr_int(attrs, "forward_pin");
        const int bwd = attr_int(attrs, "backward_pin");
        motor_ = std::make_unique<TwoPinMotor>(chip_handle_, fwd, bwd);

        // Optional enable pin: claim, drive high for the motor's lifetime.
        if (attrs.find("enable_pin") != attrs.end()) {
            const int en = attr_int(attrs, "enable_pin");
            int rc = lgGpioClaimOutput(chip_handle_, 0, en, 1);
            if (rc < 0) [[unlikely]] {
                throw std::runtime_error(
                    "lgGpioClaimOutput en pin=" + std::to_string(en) +
                    " rc=" + std::to_string(rc));
            }
            en_pin_ = en;
        }
    } catch (...) {
        if (en_pin_ >= 0) [[unlikely]] {
            lgGpioFree(chip_handle_, en_pin_);
            en_pin_ = -1;
        }
        motor_.reset();
        lgGpiochipClose(chip_handle_);
        chip_handle_ = -1;
        throw;
    }
}

Cleaner::~Cleaner() {
    motor_.reset();
    if (en_pin_ >= 0) {
        lgGpioWrite(chip_handle_, en_pin_, 0);
        lgGpioFree(chip_handle_, en_pin_);
    }
    if (chip_handle_ >= 0) {
        lgGpiochipClose(chip_handle_);
    }
}

std::vector<std::string> Cleaner::validate(const viam::sdk::ResourceConfig& cfg) {
    std::vector<std::string> errs;
    const auto& attrs = cfg.attributes();
    if (attrs.find("forward_pin")  == attrs.end()) [[unlikely]] errs.push_back("missing forward_pin");
    if (attrs.find("backward_pin") == attrs.end()) [[unlikely]] errs.push_back("missing backward_pin");
    return errs;
}

void Cleaner::set_power(double power_pct, [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> lock(mutex_);
    motor_->set_value(power_pct);
    current_power_.store(std::clamp(power_pct, -1.0, 1.0));
}

void Cleaner::go_for([[maybe_unused]] double rpm, [[maybe_unused]] double revolutions, [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("go_for not supported (no encoder)");
}

void Cleaner::go_to([[maybe_unused]] double rpm, [[maybe_unused]] double position_revolutions, [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("go_to not supported (no encoder)");
}

void Cleaner::set_rpm([[maybe_unused]] double rpm, [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("set_rpm not supported (no encoder)");
}

void Cleaner::reset_zero_position([[maybe_unused]] double offset, [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("reset_zero_position not supported (no encoder)");
}

Cleaner::position Cleaner::get_position([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_position not supported (no encoder)");
}

Cleaner::properties Cleaner::get_properties([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    return { .position_reporting = false };
}

Cleaner::power_status Cleaner::get_power_status([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    const double p = current_power_.load();
    return { .is_on = (p != 0.0), .power_pct = p };
}

bool Cleaner::is_moving() {
    return current_power_.load() != 0.0;
}

viam::sdk::ProtoStruct Cleaner::get_status() {
    return {};
}

viam::sdk::ProtoStruct Cleaner::do_command([[maybe_unused]] const viam::sdk::ProtoStruct& command) {
    throw std::runtime_error("do_command not implemented");
}

std::vector<viam::sdk::GeometryConfig> Cleaner::get_geometries([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    return {};
}

void Cleaner::stop([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> lock(mutex_);
    motor_->set_value(0.0);
    current_power_.store(0.0);
}

} // namespace cleaner
