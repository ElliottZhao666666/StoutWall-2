#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ProductBackend.hpp"
#include "ReplicaUi.hpp"
#include "Assets.hpp"
#ifndef STOUTWALL_PREVIEW_BASELINE
#include "GalleryImage.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> double Measure(F&& f) {
    const auto start = Clock::now(); f();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
template<class F> void Samples(const char* name, int count, F&& f) {
    std::vector<double> values;
    for (int i = 0; i < count; ++i) values.push_back(Measure(f));
    std::sort(values.begin(), values.end());
    std::cout << name << ",n=" << count << ",p50_ms=" << values[count / 2]
              << ",p95_ms=" << values[(count - 1) * 95 / 100] << '\n';
}
std::vector<unsigned char> Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
stoutwall::ProductSnapshot Run(stoutwall::ProductBackend& backend, stoutwall::ProductAction action,
                              int value = 0, const std::wstring& text = {}) {
    stoutwall::ProductRequest r{}; r.action = action; r.value = value; r.text = text;
    return backend.Execute(r);
}

#ifndef STOUTWALL_PREVIEW_BASELINE
void CheckLargeGalleryRead(const fs::path& runtime, const fs::path& root, const std::wstring& sourceDate) {
    Need(Library_OpenW(runtime.c_str(), (root / L"Gallery").c_str(), 0) == 0, "cannot reopen large-image fixture");
    struct Close { ~Close() { Library_Close(); } } close;
    std::vector<unsigned char> large;
    Need(stoutwall::ReadGalleryImage(sourceDate.c_str(), 1, large) > 0, "cannot read large-image source");
    Need(large.size() > 2 && large[large.size()-2] == 0xff && large.back() == 0xd9, "source JPEG lacks EOI");
    large.resize(large.size()-2);
    // Valid JPEG COM segments exercise the capacity retry without a new image dependency.
    while (large.size() <= 4 * 1024 * 1024) {
        large.insert(large.end(), {0xff, 0xfe, 0xff, 0xff});
        large.insert(large.end(), 65533, 0x41);
    }
    large.insert(large.end(), {0xff, 0xd9});
    std::wstring paths[5]; const wchar_t* inputs[5]{};
    for (int i=0; i<5; ++i) {
        std::vector<unsigned char> bytes = large;
        if (i) Need(stoutwall::ReadGalleryImage(sourceDate.c_str(), i+1, bytes)>0, "cannot read fixture size");
        paths[i]=(root/(L"import-size-"+std::to_wstring(i)+L".jpg")).wstring();
        std::ofstream file(paths[i], std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Need(static_cast<bool>(file), "cannot write import fixture");
        inputs[i]=paths[i].c_str();
    }
    SWL_Record record{};
    Need(Library_Find(sourceDate.c_str(), &record)>=0, "cannot find fixture metadata");
    wcscpy_s(record.date,L"20990101");
    const auto imported = Library_ImportW(&record,inputs);
    if (imported<0) {
        wchar_t reason[2048]{}; Library_LastErrorW(reason,2048);
        std::wcerr << L"Large-image import error: " << imported << L" " << reason << L'\n';
    }
    Need(imported>=0, "cannot import large-image fixture");
    std::vector<unsigned char> actual;
    Need(stoutwall::ReadGalleryImage(record.date,1,actual)==static_cast<int32_t>(large.size()) && actual==large,
         "large-image capacity retry changed bytes");
    Need(Library_Delete(record.date)>=0, "cannot remove large-image fixture record");
    std::cout << "PASS: gallery read above 4 MiB preserves all " << large.size() << " bytes\n";
}
#endif

namespace stoutwall {
// Use the real renderer with a private memory DC and hidden windows. No input
// injection, desktop wallpaper changes or external user-data paths.
struct PreviewProbe {
    static void Check(ReplicaUi& ui, const std::vector<unsigned char>& original,
                      const std::vector<unsigned char>& preview) {
        for (const auto* window : {L"_启动窗口", L"库管理窗口"}) {
            Need(ui.CreateProductWindow(window) != nullptr, "cannot create hidden preview window");
            Need(ui.SetImage(window, L"图片框EX1", preview), "window preview decode failed");
#ifndef STOUTWALL_PREVIEW_BASELINE
            const auto* picture = ui.Find(nullptr, window, L"图片框EX1");
            Need(picture && picture->imageWidth == 1920 && picture->imageHeight == 1080,
                 "main/gallery UHD preview is not 1080p");
#endif
        }
        Need(ui.CreateProductWindow(L"大图窗口") != nullptr, "cannot create hidden viewer");
        auto* child = ui.Find(nullptr, L"大图窗口", L"图片框EX1");
        Need(child != nullptr, "viewer picture missing");
        Samples("viewer_decode_original_UHD", 15, [&] {
            Need(ui.SetImage(L"大图窗口", L"图片框EX1", original), "UHD decode failed");
        });
#ifndef STOUTWALL_PREVIEW_BASELINE
        Need(child->imageWidth == 1920 && child->imageHeight == 1080, "UHD fallback not bounded to 1080p");
#endif
        Samples("viewer_decode_display_source", 15, [&] {
            Need(ui.SetImage(L"大图窗口", L"图片框EX1", preview), "preview decode failed");
        });
        std::cout << "decoded_width=" << child->imageWidth << ",decoded_height=" << child->imageHeight
                  << ",pixel_bytes=" << child->image.size() << '\n';
#ifndef STOUTWALL_PREVIEW_BASELINE
        Need(child->imageWidth == 1920 && child->imageHeight == 1080, "UHD display is not 1080p");
        const auto good = child->image;
        Need(!ui.SetImage(L"大图窗口", L"图片框EX1", {1,2,3}), "corrupt image accepted");
        Need(good == child->image, "failed decode destroyed previous valid preview");
#endif
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 1920; info.bmiHeader.biHeight = -1080;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Need(dc && bitmap, "cannot create private render surface");
        const auto previous = SelectObject(dc, bitmap);
        DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_STATIC; draw.hwndItem = child->hwnd;
        draw.hDC = dc; draw.rcItem = {0,0,1920,1080};
        auto paint = [&] { ui.OnDrawItem(ui.WindowHandle(L"大图窗口"), &draw); };
        std::cout << "viewer_first_paint_ms=" << Measure(paint) << '\n';
#ifndef STOUTWALL_PREVIEW_BASELINE
        const auto* cached = child->imageBitmap.Get();
        Need(cached != nullptr, "D2D bitmap not created");
#endif
        Samples("viewer_mode_repaint", 40, [&] {
            static int mode = 0;
            ui.SetImageMode(L"大图窗口", L"图片框EX1", static_cast<ReplicaUi::ImageMode>(mode++ % 4));
            paint();
        });
#ifndef STOUTWALL_PREVIEW_BASELINE
        Need(child->imageBitmap.Get() == cached, "mode changes recreated the D2D bitmap");
        Need(ui.SetImage(L"大图窗口", L"图片框EX1", {}), "cannot clear preview");
        Need(child->image.empty() && !child->imageBitmap, "cleared preview retained image resources");
#endif
        SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    }
};
}

int wmain(int argc, wchar_t** argv) {
    try {
        Need(argc == 4, "usage: preview-benchmark runtime fixture new-root");
        std::cout << std::fixed << std::setprecision(3) << std::unitbuf;
        const fs::path runtime = fs::absolute(argv[1]), fixture = fs::absolute(argv[2]), root = fs::absolute(argv[3]);
        Need(!fs::exists(root), "benchmark root must be new");
        fs::create_directories(root / L"Gallery");
        for (const auto& file : fs::directory_iterator(fixture))
            if (file.is_regular_file()) fs::copy_file(file.path(), root / L"Gallery" / file.path().filename());
        Need(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization failed");
        {
            stoutwall::ProductBackend backend;
            stoutwall::ProductSnapshot snapshot;
            std::cout << "initialize_ms=" << Measure([&] {
                snapshot = backend.Initialize(runtime.wstring(), root.wstring(), (root / L"test.cfg").wstring(), true, nullptr, L"");
            }) << '\n';
            std::cout << "first_gallery_mode_ms=" << Measure([&] { snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY); }) << '\n';
            Need(snapshot.config.currentWall.size == SWN_UHD, "benchmark not using UHD");
            const auto records = snapshot.records;
            size_t nextRecord = 1;
            Samples("uncached_record_select_UHD", 8, [&] {
                const std::wstring nextDate = records[nextRecord++ % records.size()].date;
                snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, nextDate);
            });
            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY);
            const std::wstring date = snapshot.current.date;
            Run(backend, stoutwall::ProductAction::BrowseSelect, 0, date);
            Samples("warm_browse_select_UHD", 25, [&] { snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, date); });
            Samples("warm_gallery_mode_UHD", 15, [&] { snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY); });
            const auto original = snapshot.image;
            std::vector<unsigned char> preview = original;
#ifndef STOUTWALL_PREVIEW_BASELINE
            preview = snapshot.previewImage;
            Need(!preview.empty() && preview != original, "UHD snapshot did not separate preview");
            const auto selected = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, date);
            Need(selected.selectedPreviewImage == preview, "selected and current preview differ");
#endif
            Run(backend, stoutwall::ProductAction::SaveCurrent, 0, (root / L"saved-UHD.jpg").wstring());
            Run(backend, stoutwall::ProductAction::ExportBrowse, 1, (root / L"exported-UHD.jpg").wstring());
            Need(Read(root / L"saved-UHD.jpg") == original && Read(root / L"exported-UHD.jpg") == original,
                 "UHD save or export changed original bytes");
            std::ofstream previewFile(root / L"preview.jpg", std::ios::binary);
            previewFile.write(reinterpret_cast<const char*>(preview.data()), static_cast<std::streamsize>(preview.size())); previewFile.close();
            v6::AssetModule assets; Need(assets.Open((runtime / L"assets.dll").wstring()), "cannot load assets");
            stoutwall::ReplicaUi ui(GetModuleHandleW(nullptr), {}, nullptr, assets);
            stoutwall::PreviewProbe::Check(ui, original, preview);
            Run(backend, stoutwall::ProductAction::Shutdown);
#ifndef STOUTWALL_PREVIEW_BASELINE
            CheckLargeGalleryRead(runtime, root, date);
#endif
            std::cout << "PASS: isolated preview benchmark and original-size save/export\n";
        }
        CoUninitialize(); return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
