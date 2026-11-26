#pragma once

#include "LibraryApi.h"
#include "NetworkApi.h"
#include "PlatformApi.h"
#include <string>
#include <vector>

namespace stoutwall {

enum class ProductAction {
    Initialize, Refresh, Previous, Next, Favorite, ChangeMode, ChangeSize,
    ChangeFrequency, AutoSave, AutoRun, Random, Pause, Reshuffle, BrowseSelect,
    BrowseFilter, BrowseFavorite, BrowseDelete, BrowseApply, ClearFavorites,
    Relocate, SaveCurrent, StoreLatest, ExportBrowse, ExportAllSizes, ExportTheme, Source,
    Tick, Shutdown
};

// Integer conventions: mode=1 daily/2 gallery/3 favorites; size=0..4 in the
// original UI order; frequency=0..9; source=1 global/2 China/3 custom. Boolean
// toggles including AutoRun use value 0 or 1. BrowseFilter uses 0 for all records and 1 for
// favorites. BrowseSelect uses a 1-based ordinal in the active filter, or text
// as an exact YYYYMMDD date. ExportBrowse uses size 1..5 (LibraryApi order).
// SaveCurrent.text is a file path; ExportAllSizes.text is a directory;
// Relocate.text is the new gallery directory. ExportTheme uses theme.
struct ProductRequest {
    ProductAction action = ProductAction::Initialize;
    int value = 0;
    std::wstring text;
    SWP_ThemeOptions theme{};
};

struct ProductSnapshot {
    SWP_Config config{};
    SWL_Record current{}, selected{};
    std::vector<SWL_Record> records;
    // Current desktop image remains full resolution. Browsing only reads a
    // display source; applying and exporting load the requested original size.
    std::vector<unsigned char> image;
    std::vector<unsigned char> previewImage, selectedPreviewImage;
    std::wstring status, galleryDir;
    int total = 0, favorites = 0;
    DWORD intervalMs = 0;
    bool hasCurrent = false, hasSelected = false;
    bool autoRunPreference = false;
};

// Initialize requires an explicit isolated configPath; it never discovers a
// config from runtime. AutoRun only reads/writes the preference in that INI,
// never checks or changes an operating-system startup entry. Construct, call
// methods, and explicitly execute Shutdown on one persistent worker thread.
class ProductBackend final {
public:
    ProductBackend();
    ~ProductBackend();
    ProductBackend(const ProductBackend&) = delete;
    ProductBackend& operator=(const ProductBackend&) = delete;

    ProductSnapshot Initialize(const std::wstring& runtime,
                               const std::wstring& dataRoot,
                               const std::wstring& configPath,
                               bool testMode, HANDLE cancel,
                               const std::wstring& proxy);
    ProductSnapshot Execute(const ProductRequest& request);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace stoutwall
