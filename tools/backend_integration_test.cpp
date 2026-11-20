#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "ProductBackend.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::wstring Argument(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == name) return argv[i + 1];
    return {};
}

void CopyFixture(const fs::path& fixture, const fs::path& gallery) {
    Require(fs::is_directory(fixture), "fixture directory is missing");
    fs::create_directories(gallery);
    for (const auto& entry : fs::directory_iterator(fixture)) {
        if (!entry.is_regular_file()) continue;
        fs::copy_file(entry.path(), gallery / entry.path().filename(), fs::copy_options::none);
    }
    Require(fs::is_regular_file(gallery / L"Stout_WallGallery_Index.DDT"),
            "fixture index was not copied");
}

std::vector<unsigned char> Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(static_cast<bool>(input), "unable to open " + path.string());
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    Require(length > 0, "empty output: " + path.string());
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    input.seekg(0, std::ios::beg);
    Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()),
                                         static_cast<std::streamsize>(bytes.size()))),
            "unable to read " + path.string());
    return bytes;
}

bool IsJpeg(const fs::path& path) {
    const auto bytes = Read(path);
    return bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8 &&
           bytes[bytes.size() - 2] == 0xff && bytes.back() == 0xd9;
}

std::wstring Date(const SWL_Record& record) {
    return std::wstring(record.date);
}

stoutwall::ProductSnapshot Run(stoutwall::ProductBackend& backend,
                               stoutwall::ProductAction action, int value = 0,
                               std::wstring text = {}) {
    stoutwall::ProductRequest request{};
    request.action = action;
    request.value = value;
    request.text = std::move(text);
    return backend.Execute(request);
}

void CheckFiveSizes(const fs::path& directory, const std::wstring& date) {
    static const wchar_t* names[] = {L"UHD", L"1920x1200", L"1920x1080", L"1024x768", L"1080x1920"};
    for (const auto* name : names) {
        const fs::path file = directory / (date + L"_" + name + L".jpg");
        Require(fs::is_regular_file(file) && IsJpeg(file), "invalid exported size: " + file.string());
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const fs::path runtime = fs::absolute(Argument(argc, argv, L"--runtime"));
        const fs::path fixture = fs::absolute(Argument(argc, argv, L"--fixture"));
        const fs::path root = fs::absolute(Argument(argc, argv, L"--root"));
        Require(!runtime.empty() && !fixture.empty() && !root.empty(),
                "usage: backend-integration-test --runtime DIR --fixture DIR --root DIR");
        Require(!fs::exists(root), "test root already exists: " + root.string());
        fs::create_directories(root);
        CopyFixture(fixture, root / L"Gallery");

        const fs::path config = root / L"StoutWall.cfg";
        const fs::path exportDirectory = root / L"exports";
        const fs::path allSizesDirectory = root / L"all-sizes";
        const fs::path migrated = root / L"MigratedGallery";
        fs::create_directories(exportDirectory);

        stoutwall::ProductSnapshot snapshot{};
        {
            stoutwall::ProductBackend backend;
            snapshot = backend.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
            Require(snapshot.total >= 3, "isolated gallery has too few records");
            Require(!snapshot.autoRunPreference, "test started with AutoRun preference enabled");
            Require(!snapshot.hasCurrent, "daily test mode unexpectedly applied a wallpaper");
            const std::wstring firstDate = Date(snapshot.records.front());

            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY);
            Require(snapshot.config.mode == SWP_GALLERY && snapshot.hasCurrent,
                    "gallery mode did not select a current record");
            const std::wstring initialDate = Date(snapshot.current);
            Require(!snapshot.image.empty(), "gallery mode did not load an image");

            snapshot = Run(backend, stoutwall::ProductAction::ChangeSize, 1);
            Require(snapshot.config.currentWall.size == 1 && !snapshot.image.empty(),
                    "image size change failed");
            snapshot = Run(backend, stoutwall::ProductAction::ChangeFrequency, 0);
            Require(snapshot.config.changeFreq == 0, "frequency change failed");
            snapshot = Run(backend, stoutwall::ProductAction::Pause, 1);
            Require(snapshot.config.paused != 0 && snapshot.intervalMs == 0,
                    "pause did not stop the interval");
            snapshot = Run(backend, stoutwall::ProductAction::Pause, 0);
            Require(snapshot.config.paused == 0 && snapshot.intervalMs > 0,
                    "resume did not restore the interval");

            snapshot = Run(backend, stoutwall::ProductAction::Next);
            Require(!Date(snapshot.current).empty(), "next operation lost the current record");
            snapshot = Run(backend, stoutwall::ProductAction::Previous);
            Require(Date(snapshot.current) == initialDate, "previous did not return to the prior record");

            snapshot = Run(backend, stoutwall::ProductAction::Random, 1);
            Require(snapshot.config.changeRandom != 0, "random mode was not enabled");
            snapshot = Run(backend, stoutwall::ProductAction::Reshuffle);
            Require(snapshot.config.changeRandom != 0, "reshuffle cleared random mode");
            snapshot = Run(backend, stoutwall::ProductAction::Random, 0);
            Require(snapshot.config.changeRandom == 0, "random mode was not disabled");

            const bool currentFavoriteBefore = snapshot.current.favorite != 0;
            snapshot = Run(backend, stoutwall::ProductAction::Favorite);
            Require(snapshot.hasCurrent && (snapshot.current.favorite != 0) != currentFavoriteBefore,
                    "current wallpaper favorite toggle did not change state");
            snapshot = Run(backend, stoutwall::ProductAction::Favorite);
            Require(snapshot.hasCurrent && (snapshot.current.favorite != 0) == currentFavoriteBefore,
                    "current wallpaper favorite toggle did not restore state");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFilter, 1);
            Require(!snapshot.records.empty() &&
                        std::all_of(snapshot.records.begin(), snapshot.records.end(),
                                    [](const SWL_Record& record) { return record.favorite != 0; }),
                    "favorites filter returned a non-favorite record");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFilter, 0);
            Require(snapshot.records.size() == static_cast<size_t>(snapshot.total),
                    "all-record filter did not restore the full gallery");

            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, firstDate);
            Require(snapshot.hasSelected && Date(snapshot.selected) == firstDate,
                    "browse selection did not select the requested record");
            const bool selectedFavoriteBefore = snapshot.selected.favorite != 0;
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
            Require(snapshot.hasSelected && (snapshot.selected.favorite != 0) != selectedFavoriteBefore,
                    "browse favorite toggle did not change state");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
            Require(snapshot.hasSelected && (snapshot.selected.favorite != 0) == selectedFavoriteBefore,
                    "browse favorite toggle did not restore state");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseApply);
            Require(snapshot.hasCurrent && Date(snapshot.current) == firstDate,
                    "browse apply did not make the selected record current");

            const fs::path currentCopy = exportDirectory / L"current.jpg";
            stoutwall::ProductRequest save{};
            save.action = stoutwall::ProductAction::SaveCurrent;
            save.text = currentCopy.wstring();
            snapshot = backend.Execute(save);
            Require(fs::is_regular_file(currentCopy) && IsJpeg(currentCopy),
                    "current wallpaper export is invalid");

            const fs::path single = exportDirectory / L"single.jpg";
            stoutwall::ProductRequest exportOne{};
            exportOne.action = stoutwall::ProductAction::ExportBrowse;
            exportOne.value = 2;
            exportOne.text = single.wstring();
            snapshot = backend.Execute(exportOne);
            Require(fs::is_regular_file(single) && IsJpeg(single), "single-size export is invalid");

            fs::create_directories(allSizesDirectory);
            stoutwall::ProductRequest exportAll{};
            exportAll.action = stoutwall::ProductAction::ExportAllSizes;
            exportAll.text = allSizesDirectory.wstring();
            snapshot = backend.Execute(exportAll);
            CheckFiveSizes(allSizesDirectory, firstDate);

            fs::create_directories(root / L"theme-win7");
            stoutwall::ProductRequest theme{};
            theme.action = stoutwall::ProductAction::ExportTheme;
            theme.theme.cbSize = sizeof(theme.theme);
            theme.theme.kind = SWP_THEME_WIN7;
            theme.theme.selection = SWP_THEME_ALL;
            theme.theme.sizeIndex = 2;
            theme.theme.style = SWP_THEME_FIT;
            theme.theme.colorIndex = 0;
            theme.theme.intervalIndex = 2;
            theme.theme.shuffle = 1;
            theme.theme.transparency = 1;
            wcscpy_s(theme.theme.name, L"backend-test-win7");
            wcscpy_s(theme.theme.outputDir, (root / L"theme-win7").c_str());
            snapshot = backend.Execute(theme);
            const fs::path win7Theme = root / L"theme-win7" / L"backend-test-win7.themepack";
            Require(fs::is_regular_file(win7Theme) && fs::file_size(win7Theme) > 0,
                    "Win7 theme package was not created");

            fs::create_directories(root / L"theme-modern");
            theme.theme.kind = SWP_THEME_MODERN;
            wcscpy_s(theme.theme.name, L"backend-test-modern");
            wcscpy_s(theme.theme.outputDir, (root / L"theme-modern").c_str());
            snapshot = backend.Execute(theme);
            const fs::path modernTheme = root / L"theme-modern" / L"backend-test-modern.deskthemepack";
            Require(fs::is_regular_file(modernTheme) && fs::file_size(modernTheme) > 0,
                    "modern theme package was not created");

            snapshot = Run(backend, stoutwall::ProductAction::Relocate, 0, migrated.wstring());
            Require(snapshot.galleryDir == fs::absolute(migrated).lexically_normal().wstring() &&
                        fs::is_regular_file(migrated / L"Stout_WallGallery_Index.DDT"),
                    "empty-directory gallery migration failed");
            snapshot = Run(backend, stoutwall::ProductAction::Relocate, 0,
                           (root / L"Gallery").wstring());
            Require(snapshot.galleryDir == fs::absolute(root / L"Gallery").lexically_normal().wstring(),
                    "switch back to a complete gallery failed");

            const std::wstring deleteDate = Date(snapshot.records.back());
            const int beforeDelete = snapshot.total;
            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, deleteDate);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseDelete);
            Require(snapshot.total == beforeDelete - 1 &&
                        std::none_of(snapshot.records.begin(), snapshot.records.end(),
                                     [&](const SWL_Record& record) { return Date(record) == deleteDate; }),
                    "gallery deletion did not remove exactly one record");

            snapshot = Run(backend, stoutwall::ProductAction::ClearFavorites);
            Require(snapshot.favorites == 0, "clear favorites left a favorite record");
            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_FAVORITES);
            Require(snapshot.config.mode == SWP_FAVORITES && snapshot.intervalMs == 0,
                    "empty favorites mode did not stop rotation");
            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY);
            Require(snapshot.config.mode == SWP_GALLERY, "could not return to gallery mode");

            std::wcout << L"PASS: backend core actions, exports, migration, deletion, favorites, and themes.\n";
        }

        stoutwall::ProductBackend reopened;
        snapshot = reopened.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
        Require(snapshot.total >= 2 && snapshot.favorites == 0 && snapshot.config.mode == SWP_GALLERY,
                "reopened state does not match the completed isolated round");
        std::wcout << L"PASS: isolated configuration and gallery reopen after backend shutdown.\n";
        std::wcout << L"Retained test root: " << root.wstring() << L"\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "FAIL: unknown exception\n";
        return 1;
    }
}
