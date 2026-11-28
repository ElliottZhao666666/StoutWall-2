#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace stoutwall {

struct ProductOptions {
    bool testMode = false;
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
