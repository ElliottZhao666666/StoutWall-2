#pragma once

#include "OriginalLayout.hpp"
#include <string>
#include <vector>

namespace stoutwall {

struct ProductControlPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int page = -1;
    bool visible = true;
    bool customized = false;
    std::wstring parent;
    std::wstring text;
};

struct ProductWindowPlacement {
    int width = 0;
    int height = 0;
    bool customized = false;
};

ProductWindowPlacement ProductWindowSize(const std::wstring& window);
std::vector<std::wstring> ProductTabTitles(const std::wstring& window);
ProductControlPlacement ProductControlLayout(const std::wstring& window,
                                             const ControlDef& control);

} // namespace stoutwall
