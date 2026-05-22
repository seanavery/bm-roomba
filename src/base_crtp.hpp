#pragma once

#include <lgpio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/components/base.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/module/service.hpp>

namespace base::crtp {

template <typename T>
concept MotorLike = requires(T m, double v) {
    { m.set_value(v) } -> std::same_as<void>;
};

template <typename Derived>
class MotorBase {
public:
    void set_value(double v) {
        v = std::clamp(v, -1.0, 1.0);
        static_cast<Derived*>(this)->do_set(v);
    }
};

template <int FwdPin, int BwdPin, int EnPin, int PwmHz = 100>
class L298NMotor : public MotorBase<L298NMotor<FwdPin, BwdPin, EnPin, PwmHz>> {
public:
    explicit L298NMotor(int chip_handle) : handle_(chip_handle) {
        claim(FwdPin, "fwd");
        claim(BwdPin, "bwd");
        claim(EnPin,  "en");
    }

    ~L298NMotor() {
        lgTxPwm(handle_, FwdPin, PwmHz, 0.0, 0, 0);
        lgTxPwm(handle_, BwdPin, PwmHz, 0.0, 0, 0);
        lgGpioWrite(handle_, EnPin, 0);
        lgGpioFree(handle_, FwdPin);
        lgGpioFree(handle_, BwdPin);
        lgGpioFree(handle_, EnPin);
    }

    L298NMotor(const L298NMotor&) = delete;
    L298NMotor& operator=(const L298NMotor&) = delete;

    void do_set(double v) {
        const double duty = std::abs(v) * 100.0;
        if (v == 0.0) {
            lgTxPwm(handle_, FwdPin, PwmHz, 0.0, 0, 0);
            lgTxPwm(handle_, BwdPin, PwmHz, 0.0, 0, 0);
            lgGpioWrite(handle_, EnPin, 0);
        } else if (v > 0.0) {
            lgGpioWrite(handle_, EnPin, 1);
            lgTxPwm(handle_, BwdPin, PwmHz, 0.0,  0, 0);
            lgTxPwm(handle_, FwdPin, PwmHz, duty, 0, 0);
        } else {
            lgGpioWrite(handle_, EnPin, 1);
            lgTxPwm(handle_, FwdPin, PwmHz, 0.0,  0, 0);
            lgTxPwm(handle_, BwdPin, PwmHz, duty, 0, 0);
        }
    }

private:
    int handle_;

    void claim(int pin, const char* name) {
        int rc = lgGpioClaimOutput(handle_, 0, pin, 0);
        if (rc < 0) {
            throw std::runtime_error(
                std::string("lgGpioClaimOutput ") + name +
                " pin=" + std::to_string(pin) +
                " rc=" + std::to_string(rc));
        }
    }
};

struct DiffDriveSpec {
    double width_mm;
    double wheel_circumference_mm;
    double max_speed_mm_s;
    double max_spin_deg_s;
};

template <typename Derived, DiffDriveSpec Spec>
class DifferentialDriveBase {
public:
    static constexpr DiffDriveSpec spec() { return Spec; }

    void set_power(double linear_y, double angular_z) {
        static_cast<Derived*>(this)->set_motors(
            std::clamp(linear_y - angular_z, -1.0, 1.0),
            std::clamp(linear_y + angular_z, -1.0, 1.0));
    }

    void set_velocity(double linear_y_mm_s, double angular_z_deg_s) {
        constexpr double half_width = Spec.width_mm / 2.0;
        const double omega = angular_z_deg_s * 3.14159265358979323846 / 180.0;
        static_cast<Derived*>(this)->set_motors(
            std::clamp((linear_y_mm_s - omega * half_width) / Spec.max_speed_mm_s, -1.0, 1.0),
            std::clamp((linear_y_mm_s + omega * half_width) / Spec.max_speed_mm_s, -1.0, 1.0));
    }
};

template <MotorLike LeftMotor, MotorLike RightMotor, DiffDriveSpec Spec>
class DifferentialDrive
    : public DifferentialDriveBase<
          DifferentialDrive<LeftMotor, RightMotor, Spec>, Spec> {
public:
    explicit DifferentialDrive(int chip_handle)
        : left_(chip_handle), right_(chip_handle) {}

    void set_motors(double left, double right) {
        left_.set_value(left);
        right_.set_value(right);
    }

    void stop() { set_motors(0.0, 0.0); }

private:
    LeftMotor  left_;
    RightMotor right_;
};

// Compile-time chassis configuration.
inline constexpr DiffDriveSpec kRoombaSpec{
    .width_mm               = 235.0,
    .wheel_circumference_mm = 220.0,
    .max_speed_mm_s         = 1341.0,
    .max_spin_deg_s         = 180.0,
};
using LeftMotor  = L298NMotor</*fwd*/13, /*bwd*/26, /*en*/19>;
using RightMotor = L298NMotor</*fwd*/16, /*bwd*/20, /*en*/21>;
using RoombaBase = DifferentialDrive<LeftMotor, RightMotor, kRoombaSpec>;

static_assert(RoombaBase::spec().width_mm > 0.0);
static_assert(RoombaBase::spec().max_speed_mm_s > 0.0);

// SDK adapter: viam::sdk::Base requires virtual dispatch. This class forwards
// the SDK's virtuals into the CRTP/static-dispatched RoombaBase guts.
class Base : public viam::sdk::Base {
public:
    Base(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);
    ~Base() override;

    static std::vector<std::string> validate(const viam::sdk::ResourceConfig& cfg);

    void stop(const viam::sdk::ProtoStruct& extra) override;
    void move_straight(int64_t distance_mm, double mm_per_sec,
                       const viam::sdk::ProtoStruct& extra) override;
    void spin(double angle_deg, double degs_per_sec,
              const viam::sdk::ProtoStruct& extra) override;
    void set_power(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular,
                   const viam::sdk::ProtoStruct& extra) override;
    void set_velocity(const viam::sdk::Vector3& linear, const viam::sdk::Vector3& angular,
                      const viam::sdk::ProtoStruct& extra) override;
    bool is_moving() override;
    viam::sdk::ProtoStruct get_status() override;
    viam::sdk::Base::properties get_properties(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra) override;

private:
    int chip_handle_ = -1;
    std::unique_ptr<RoombaBase> drive_;
    std::mutex drive_mutex_;
    std::atomic<bool> moving_{false};
};

} // namespace base::crtp
