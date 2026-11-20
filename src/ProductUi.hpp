#pragma once

#include "ProductBackend.hpp"
#include "ReplicaUi.hpp"
#include "TaskRunner.hpp"
#include <memory>
#include <string>

namespace stoutwall {

struct ProductOptions {
    // Explicitly opt into desktop changes with --allow-wallpaper.
    bool testMode = true;
    bool uiTest = false;
    bool quiet = false;
    std::wstring dataRoot;
    std::wstring configPath;
    std::wstring proxy;
    std::wstring runtime;
};

ProductOptions ParseProductOptions(HINSTANCE instance);

class ProductUi final {
public:
    ProductUi(HINSTANCE instance, ProductOptions options);
    ~ProductUi();
    int Run(int showCommand);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace stoutwall
