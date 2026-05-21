#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <viam/sdk/common/proto_value.hpp>
#include <viam/sdk/components/motor.hpp>
#include <viam/sdk/config/resource.hpp>

namespace cleaner {

class TwoPinMotor;

// viam::sdk::Motor implementation for an H-bridge driven by two GPIOs
// with no encoder (no position reporting). The enable pin is assumed to
// be jumpered always-on on the L298N driver.
//
// Required config attributes:
//   "forward_pin"  (number) - BCM GPIO for IN1
//   "backward_pin" (number) - BCM GPIO for IN2
class Cleaner : public viam::sdk::Motor {
public:
    Cleaner(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);
    ~Cleaner() override;

    static std::vector<std::string> validate(const viam::sdk::ResourceConfig& cfg);

    void set_power(double power_pct, const viam::sdk::ProtoStruct& extra) override;
    void go_for(double rpm, double revolutions, const viam::sdk::ProtoStruct& extra) override;
    void go_to(double rpm, double position_revolutions, const viam::sdk::ProtoStruct& extra) override;
    void set_rpm(double rpm, const viam::sdk::ProtoStruct& extra) override;
    void reset_zero_position(double offset, const viam::sdk::ProtoStruct& extra) override;
    position get_position(const viam::sdk::ProtoStruct& extra) override;
    properties get_properties(const viam::sdk::ProtoStruct& extra) override;
    power_status get_power_status(const viam::sdk::ProtoStruct& extra) override;
    bool is_moving() override;
    viam::sdk::ProtoStruct get_status() override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra) override;
    void stop(const viam::sdk::ProtoStruct& extra) override;

private:
    int chip_handle_ = -1;
    std::unique_ptr<TwoPinMotor> motor_;
    std::mutex mutex_;
    std::atomic<double> current_power_{0.0};
};

} // namespace cleaner
