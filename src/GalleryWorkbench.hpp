#pragma once
#include "ProductLayout.hpp"
#include <string>
#include <vector>

namespace stoutwall {
// Pure presentation state. Storage, network and export operations remain in
// ProductBackend; this component owns only workbench layout and local filtering.
class GalleryWorkbench final {
public:
    struct Item {
        std::wstring date,title;
        bool favorite=false,current=false;
    };
    int previewSize=2;
    bool favoritesOnly=false;
    std::vector<Item> items;
    std::vector<size_t> visibleItems;
    std::vector<std::wstring> Rows();
    int DisplayIndex(const std::wstring& date) const;
    std::wstring DateAt(int displayIndex) const;
    static ProductControlPlacement Layout(const std::wstring& window,const ControlDef& control);
};
} // namespace stoutwall
