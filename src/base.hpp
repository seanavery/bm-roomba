#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/components/base.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/module/service.hpp>

namespace base {

class Motor;

class Base : public viam::sdk::Base {
public:
    Base(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);
    ~Base() override;

    static std::vector<std::string> validate(const viam::sdk::ResourceConfig& cfg);

    void stop(const viam::sdk::ProtoStruct& extra) override;

    void move_straight(
        int64_t distance_mm,
        double mm_per_sec,
        const viam::sdk::ProtoStruct& extra) override;

    void spin(
        double angle_deg,
        double degs_per_sec,
        const viam::sdk::ProtoStruct& extra) override;

    void set_power(
        const viam::sdk::Vector3& linear,
        const viam::sdk::Vector3& angular,
        const viam::sdk::ProtoStruct& extra) override;

    void set_velocity(
        const viam::sdk::Vector3& linear,
        const viam::sdk::Vector3& angular,
        const viam::sdk::ProtoStruct& extra) override;

    bool is_moving() override;

    viam::sdk::ProtoStruct get_status() override;

    viam::sdk::Base::properties get_properties(const viam::sdk::ProtoStruct& extra) override;

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;

    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra) override;

private:
    void set_motors(double left, double right);
    void reconfigure(const viam::sdk::ResourceConfig& cfg);

    int chip_handle_ = -1;
    std::unique_ptr<Motor> motor_left_;
    std::unique_ptr<Motor> motor_right_;

    double width_mm_ = 0.0;
    double wheel_circumference_mm_ = 0.0;
    double max_speed_mm_s_ = 0.0;
    double max_spin_deg_s_ = 0.0;

    std::mutex motor_mutex_;
    std::atomic<bool> moving_{false};
};

} // namespace base
