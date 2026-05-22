#include "base_crtp.hpp"
#include "gpio_util.hpp"

#include <chrono>
#include <expected>
#include <stdexcept>
#include <thread>

namespace base::crtp {
namespace {

// Pi 5 exposes the 40-pin header on /dev/gpiochip4.
constexpr int kGpioChip = 4;

} // namespace

Base::Base([[maybe_unused]] const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg)
    : viam::sdk::Base(cfg.name()) {
    auto h = gpio_util::open_chip(kGpioChip);
    if (!h) throw std::runtime_error(std::move(h).error());
    chip_handle_ = *h;
    try {
        drive_ = std::make_unique<RoombaBase>(chip_handle_);
    } catch (...) {
        lgGpiochipClose(chip_handle_);
        chip_handle_ = -1;
        throw;
    }
}

Base::~Base() {
    drive_.reset();
    if (chip_handle_ >= 0) {
        lgGpiochipClose(chip_handle_);
    }
}

std::vector<std::string> Base::validate([[maybe_unused]] const viam::sdk::ResourceConfig& cfg) {
    return {};
}

void Base::stop([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> lock(drive_mutex_);
    drive_->stop();
    moving_.store(false);
}

void Base::move_straight(int64_t distance_mm, double mm_per_sec,
                         [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    if (distance_mm == 0 || mm_per_sec == 0.0) {
        stop({});
        return;
    }
    constexpr double max_speed = RoombaBase::spec().max_speed_mm_s;
    const double power = std::clamp(
        (distance_mm > 0 ? std::abs(mm_per_sec) : -std::abs(mm_per_sec)) / max_speed,
        -1.0, 1.0);
    const double duration_s = std::abs(static_cast<double>(distance_mm) / mm_per_sec);

    {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        drive_->set_motors(power, power);
        moving_.store(true);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_s));
    stop({});
}

void Base::spin(double angle_deg, double degs_per_sec,
                [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    if (angle_deg == 0.0 || degs_per_sec == 0.0) {
        stop({});
        return;
    }
    constexpr double max_spin = RoombaBase::spec().max_spin_deg_s;
    const double power = std::clamp(std::abs(degs_per_sec) / max_spin, 0.0, 1.0);
    const double duration_s = std::abs(angle_deg / degs_per_sec);

    {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        if (angle_deg > 0.0) {
            drive_->set_motors(-power, power);
        } else {
            drive_->set_motors(power, -power);
        }
        moving_.store(true);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(duration_s));
    stop({});
}

void Base::set_power(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular,
                     [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> lock(drive_mutex_);
    drive_->set_power(linear.y(), angular.z());
    moving_.store(linear.y() != 0.0 || angular.z() != 0.0);
}

void Base::set_velocity(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular,
                        [[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> lock(drive_mutex_);
    drive_->set_velocity(linear.y(), angular.z());
    moving_.store(linear.y() != 0.0 || angular.z() != 0.0);
}

bool Base::is_moving() { return moving_.load(); }

viam::sdk::ProtoStruct Base::get_status() { return {}; }

viam::sdk::Base::properties Base::get_properties([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    constexpr DiffDriveSpec spec = RoombaBase::spec();
    viam::sdk::Base::properties p{};
    p.width_meters               = spec.width_mm / 1000.0;
    p.turning_radius_meters      = 0.0;
    p.wheel_circumference_meters = spec.wheel_circumference_mm / 1000.0;
    return p;
}

viam::sdk::ProtoStruct Base::do_command([[maybe_unused]] const viam::sdk::ProtoStruct& command) {
    throw std::runtime_error("do_command not implemented");
}

std::vector<viam::sdk::GeometryConfig> Base::get_geometries([[maybe_unused]] const viam::sdk::ProtoStruct& extra) {
    return {};
}

} // namespace base::crtp
