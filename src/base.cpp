#include "base.hpp"

#include <lgpio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

namespace base {
namespace {

// Pi 5 exposes the 40-pin header on /dev/gpiochip4 (not gpiochip0 as on Pi 4).
constexpr int kGpioChip = 4;

// BCM pin assignments for L298N motor driver
constexpr int kLeftForward  = 13;
constexpr int kLeftBackward = 26;
constexpr int kLeftEnable   = 19;
constexpr int kRightForward  = 16;
constexpr int kRightBackward = 20;
constexpr int kRightEnable   = 21;

constexpr double kDefaultWidthMm              = 235.0;
constexpr double kDefaultWheelCircumferenceMm = 220.0;
constexpr double kDefaultMaxSpeedMmS          = 1341.0;
constexpr double kDefaultMaxSpinDegS          = 180.0;

constexpr int kPwmFrequencyHz = 100;

double clamp(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}

double attr_double(const viam::sdk::ProtoStruct& attrs, const std::string& key, double def) {
    auto it = attrs.find(key);
    if (it == attrs.end()) return def;
    if (it->second.is_a<double>()) return it->second.get_unchecked<double>();
    return def;
}

} // namespace

class Motor {
public:
    Motor(int handle, int forward, int backward, int enable)
        : handle_(handle), fwd_(forward), bwd_(backward), en_(enable) {
        auto claim = [&](int pin, const char* name) {
            int rc = lgGpioClaimOutput(handle_, 0, pin, 0);
            if (rc < 0) {
                throw std::runtime_error(
                    std::string("lgGpioClaimOutput ") + name + " pin=" + std::to_string(pin) +
                    " rc=" + std::to_string(rc));
            }
        };
        try {
            claim(fwd_, "fwd");
            claim(bwd_, "bwd");
            claim(en_,  "en");
        } catch (...) {
            lgGpioFree(handle_, fwd_);
            lgGpioFree(handle_, bwd_);
            lgGpioFree(handle_, en_);
            throw;
        }
    }

    ~Motor() {
        lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0, 0, 0);
        lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0, 0, 0);
        lgGpioWrite(handle_, en_, 0);
        lgGpioFree(handle_, fwd_);
        lgGpioFree(handle_, bwd_);
        lgGpioFree(handle_, en_);
    }

    Motor(const Motor&) = delete;
    Motor& operator=(const Motor&) = delete;

    void set_value(double value) {
        value = clamp(value, -1.0, 1.0);
        double duty = std::abs(value) * 100.0;

        if (value == 0.0) {
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0, 0, 0);
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0, 0, 0);
            lgGpioWrite(handle_, en_, 0);
        } else if (value > 0.0) {
            lgGpioWrite(handle_, en_, 1);
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, 0.0,  0, 0);
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, duty, 0, 0);
        } else {
            lgGpioWrite(handle_, en_, 1);
            lgTxPwm(handle_, fwd_, kPwmFrequencyHz, 0.0,  0, 0);
            lgTxPwm(handle_, bwd_, kPwmFrequencyHz, duty, 0, 0);
        }
    }

private:
    int handle_;
    int fwd_, bwd_, en_;
};

Base::Base(const viam::sdk::Dependencies& /*deps*/, const viam::sdk::ResourceConfig& cfg)
    : viam::sdk::Base(cfg.name()) {
    chip_handle_ = lgGpiochipOpen(kGpioChip);
    if (chip_handle_ < 0) {
        throw std::runtime_error(
            "lgGpiochipOpen(" + std::to_string(kGpioChip) + ") rc=" + std::to_string(chip_handle_));
    }

    try {
        motor_left_  = std::make_unique<Motor>(chip_handle_, kLeftForward,  kLeftBackward,  kLeftEnable);
        motor_right_ = std::make_unique<Motor>(chip_handle_, kRightForward, kRightBackward, kRightEnable);
    } catch (...) {
        lgGpiochipClose(chip_handle_);
        chip_handle_ = -1;
        throw;
    }

    reconfigure(cfg);
}

Base::~Base() {
    motor_left_.reset();
    motor_right_.reset();
    if (chip_handle_ >= 0) {
        lgGpiochipClose(chip_handle_);
    }
}

void Base::reconfigure(const viam::sdk::ResourceConfig& cfg) {
    const auto& attrs = cfg.attributes();
    width_mm_               = attr_double(attrs, "width_mm",               kDefaultWidthMm);
    wheel_circumference_mm_ = attr_double(attrs, "wheel_circumference_mm", kDefaultWheelCircumferenceMm);
    max_speed_mm_s_         = attr_double(attrs, "max_speed_mm_s",         kDefaultMaxSpeedMmS);
    max_spin_deg_s_         = attr_double(attrs, "max_spin_deg_s",         kDefaultMaxSpinDegS);
}

std::vector<std::string> Base::validate(const viam::sdk::ResourceConfig& /*cfg*/) {
    return {};
}

void Base::set_motors(double left, double right) {
    std::lock_guard<std::mutex> lock(motor_mutex_);
    motor_left_->set_value(left);
    motor_right_->set_value(right);
    moving_.store(left != 0.0 || right != 0.0);
}

void Base::stop(const viam::sdk::ProtoStruct& /*extra*/) {
    set_motors(0.0, 0.0);
}

void Base::move_straight(int64_t distance_mm, double mm_per_sec, const viam::sdk::ProtoStruct& /*extra*/) {
    if (distance_mm == 0 || mm_per_sec == 0.0) {
        set_motors(0.0, 0.0);
        return;
    }
    double speed = std::abs(mm_per_sec) / max_speed_mm_s_;
    double power = clamp(distance_mm > 0 ? speed : -speed, -1.0, 1.0);
    double duration_s = std::abs(static_cast<double>(distance_mm) / mm_per_sec);

    set_motors(power, power);
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_s));
    set_motors(0.0, 0.0);
}

void Base::spin(double angle_deg, double degs_per_sec, const viam::sdk::ProtoStruct& /*extra*/) {
    if (angle_deg == 0.0 || degs_per_sec == 0.0) {
        set_motors(0.0, 0.0);
        return;
    }
    double power = clamp(std::abs(degs_per_sec) / max_spin_deg_s_, 0.0, 1.0);
    double duration_s = std::abs(angle_deg / degs_per_sec);

    if (angle_deg > 0.0) {
        set_motors(-power, power);
    } else {
        set_motors(power, -power);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_s));
    set_motors(0.0, 0.0);
}

void Base::set_power(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular, const viam::sdk::ProtoStruct& /*extra*/) {
    set_motors(
        clamp(linear.y() - angular.z(), -1.0, 1.0),
        clamp(linear.y() + angular.z(), -1.0, 1.0));
}

void Base::set_velocity(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular, const viam::sdk::ProtoStruct& /*extra*/) {
    double omega = angular.z() * M_PI / 180.0;
    double half_width = width_mm_ / 2.0;
    set_motors(
        clamp((linear.y() - omega * half_width) / max_speed_mm_s_, -1.0, 1.0),
        clamp((linear.y() + omega * half_width) / max_speed_mm_s_, -1.0, 1.0));
}

bool Base::is_moving() {
    return moving_.load();
}

viam::sdk::ProtoStruct Base::get_status() {
    return {};
}

viam::sdk::Base::properties Base::get_properties(const viam::sdk::ProtoStruct& /*extra*/) {
    viam::sdk::Base::properties p{};
    p.width_meters               = width_mm_ / 1000.0;
    p.turning_radius_meters      = 0.0;
    p.wheel_circumference_meters = wheel_circumference_mm_ / 1000.0;
    return p;
}

viam::sdk::ProtoStruct Base::do_command(const viam::sdk::ProtoStruct& /*command*/) {
    throw std::runtime_error("do_command not implemented");
}

std::vector<viam::sdk::GeometryConfig> Base::get_geometries(const viam::sdk::ProtoStruct& /*extra*/) {
    return {};
}

} // namespace base
