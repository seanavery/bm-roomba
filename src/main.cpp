#include <cstdint>
#include "base.hpp"
#include "cleaner.hpp"

#include <cstdio>
#include <memory>
#include <print>
#include <vector>

#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/log/logging.hpp>
#include <viam/sdk/registry/registry.hpp>


int main(int argc, char** argv) try {
    viam::sdk::Instance inst;

    VIAM_SDK_LOG(info) << "Starting up base module";

    auto base_mr = std::make_shared<viam::sdk::ModelRegistration>(
        viam::sdk::API::get<viam::sdk::Base>(),
        viam::sdk::Model("sean", "roomba-pi", "base"),
        [](viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg) {
            return std::make_unique<base::Base>(deps, cfg);
        },
        &base::Base::validate);

    auto cleaner_mr = std::make_shared<viam::sdk::ModelRegistration>(
        viam::sdk::API::get<viam::sdk::Motor>(),
        viam::sdk::Model("sean", "roomba-pi", "cleaner"),
        [](viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg) {
            return std::make_unique<cleaner::Cleaner>(deps, cfg);
        },
        &cleaner::Cleaner::validate);

    std::vector<std::shared_ptr<viam::sdk::ModelRegistration>> mrs = {base_mr, cleaner_mr};
    auto my_mod = std::make_shared<viam::sdk::ModuleService>(argc, argv, mrs);
    my_mod->serve();

    return EXIT_SUCCESS;
} catch (const viam::sdk::Exception& ex) {
    std::println(stderr, "main failed with exception: {}", ex.what());
    return EXIT_FAILURE;
}
