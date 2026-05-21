#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/log/logging.hpp>
#include <viam/sdk/registry/registry.hpp>

#include "base_crtp.hpp"

int main(int argc, char** argv) try {
    viam::sdk::Instance inst;

    VIAM_SDK_LOG(info) << "Starting up base_crtp module";

    viam::sdk::Model model("sean", "roomba-pi", "base");

    auto mr = std::make_shared<viam::sdk::ModelRegistration>(
        viam::sdk::API::get<viam::sdk::Base>(),
        model,
        [](viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg) {
            return std::make_unique<base::crtp::Base>(deps, cfg);
        },
        &base::crtp::Base::validate);

    std::vector<std::shared_ptr<viam::sdk::ModelRegistration>> mrs = {mr};
    auto my_mod = std::make_shared<viam::sdk::ModuleService>(argc, argv, mrs);
    my_mod->serve();

    return EXIT_SUCCESS;
} catch (const viam::sdk::Exception& ex) {
    std::cerr << "main_crtp failed with exception: " << ex.what() << "\n";
    return EXIT_FAILURE;
}
