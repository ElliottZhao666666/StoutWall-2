#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "ProductUi.hpp"
#include "Assets.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <objbase.h>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace stoutwall {
namespace fs = std::filesystem;
namespace {

constexpr int kInitializeOperation = 1;
constexpr int kActionOperation = 2;
constexpr int kThemeOperation = 9;
constexpr int kStoreOperation = 10;
constexpr int kTickOperation = 11;
constexpr int kRestartClockOperation = 12;
constexpr int kAutoSaveOperation = 13;
constexpr int kClearFavoritesOperation = 14;
constexpr int kShutdownOperation = 3;
constexpr int kSourceOperation = 4;
constexpr int kRelocateOperation = 5;
constexpr int kGalleryOperation = 6;
constexpr int kPreviewOperation = 7;
constexpr int kExportOperation = 8;
constexpr UINT_PTR kClockTimer = 0x5321;
constexpr UINT kTimerIntervalMs = 1000;
constexpr UINT kTrayMessage = WM_APP + 31;
constexpr wchar_t kMainWindow[] = L"_启动窗口";
constexpr wchar_t kViewerWindow[] = L"大图窗口";
constexpr wchar_t kThemeWindow[] = L"主题包窗口";
constexpr wchar_t kThemeSelectionWindow[] = L"壁纸导出主题自选窗口";
const std::vector<std::wstring> kSizes {L"(4K16:9)3840x2160", L"(16:10)1920x1200", L"(16:9)1920x1080", L"(4:3)1024x768", L"(竖屏9:16)1080x1920"};
constexpr wchar_t kSourceWindow[] = L"壁纸来源窗口";
constexpr wchar_t kRelocateWindow[] = L"库迁移窗口";
constexpr wchar_t kGalleryWindow[] = L"库管理窗口";
constexpr wchar_t kRelocateButton[] = L"确定修改/迁移";
constexpr wchar_t kIndexFile[] = L"Stout_WallGallery_Index.DDT";
constexpr const wchar_t* kSourceRadios[] = {L"单选框EX1", L"单选框EX2", L"单选框EX3"};
constexpr const wchar_t* kSizeNames[] = {L"UHD", L"1920x1200", L"1920x1080", L"1024x768", L"1080x1920"};
static_assert(_countof(kSizeNames) == SWN_SIZE_COUNT);

std::wstring ModuleDirectory(HINSTANCE instance) {
    wchar_t buffer[32768]{};
    const DWORD size = GetModuleFileNameW(instance, buffer, static_cast<DWORD>(_countof(buffer)));
    if (!size || size >= _countof(buffer)) return fs::current_path().wstring();
    return fs::path(buffer, buffer + size).parent_path().wstring();
}

std::wstring DefaultDataRoot() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(_countof(buffer)));
    const fs::path base = size && size < _countof(buffer)
        ? fs::path(buffer, buffer + size) : fs::temp_directory_path();
    return (base / L"StoutWallReBuild2").wstring();
}

fs::path NewTestDataRoot() {
    GUID id{};
    wchar_t token[39]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, token, _countof(token)))
        throw std::runtime_error("Unable to allocate an isolated test data path");
    return fs::temp_directory_path() / (L"StoutWallReBuild2-Test-" + std::wstring(token));
}

bool IsWithin(const fs::path& rootPath, const fs::path& childPath) {
    const std::wstring root = fs::absolute(rootPath).lexically_normal().wstring();
    const std::wstring child = fs::absolute(childPath).lexically_normal().wstring();
    if (_wcsicmp(root.c_str(), child.c_str()) == 0) return true;
    if (child.size() <= root.size() || _wcsnicmp(root.c_str(), child.c_str(), root.size()) != 0) return false;
    return root.back() == L'\\' || root.back() == L'/' || child[root.size()] == L'\\' || child[root.size()] == L'/';
}

std::wstring BoundedText(const wchar_t* value, size_t capacity) {
    size_t length = 0;
    while (length < capacity && value[length]) ++length;
    return std::wstring(value, value + length);
}

void OpenPage(HWND owner,const std::wstring& address) {
    if (address.rfind(L"https://",0)==0 || address.rfind(L"http://",0)==0)
        ShellExecuteW(owner,L"open",address.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}

bool CopyText(HWND owner,const wchar_t* text) {
    const size_t bytes=(wcslen(text)+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if (!memory) return false;
    void* data=GlobalLock(memory);
    if (!data) {GlobalFree(memory);return false;}
    memcpy(data,text,bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) {GlobalFree(memory);return false;}
    const bool copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);
    CloseClipboard();
    if (!copied)GlobalFree(memory);
    return copied;
}

std::wstring RecordDate(const SWL_Record& record) { return BoundedText(record.date, _countof(record.date)); }
std::wstring RecordTitle(const SWL_Record& record) { return BoundedText(record.title, _countof(record.title)); }

std::wstring GalleryDetails(const SWL_Record& record, int index, size_t count) {
    const std::wstring date = RecordDate(record);
    const std::wstring displayDate = date.size() == 8 ? date.substr(0, 4) + L"." + date.substr(4, 2) + L"." + date.substr(6) : date;
    std::wstring id = BoundedText(record.urlbase, _countof(record.urlbase));
    if (id.compare(0, 7, L"/th?id=") == 0) id.erase(0, 7);
    return L"[ " + std::to_wstring(index + 1) + L" / " + std::to_wstring(count) + L" ]" +
        (record.favorite ? L" [已收藏]" : L"") + L"\r\n" + RecordTitle(record) + L"\r\n" +
        BoundedText(record.description, _countof(record.description)) + L"\r\n(C) " +
        BoundedText(record.author, _countof(record.author)) + L"\r\n\r\n日期：" + displayDate + L"\r\nthID：" + id +
        L"\r\n详情链接：" + BoundedText(record.link, _countof(record.link)) + L"\r\n";
}

std::wstring SuggestedJpeg(const SWL_Record& record, int size) {
    std::wstring name = RecordTitle(record);
    for (auto& ch : name)
        if (ch < 32 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos) ch = L'_';
    if (name.size() > 100) {
        name.resize(100);
        if (name.back() >= 0xd800 && name.back() <= 0xdbff) name.pop_back();
    }
    return RecordDate(record) + (name.empty() ? L"" : L"_" + name) + L"_" + kSizeNames[size] + L".jpg";
}

// Keep test outputs on ordinary paths under the isolated data root, including
// when the destination does not exist yet. Recheck before writes on the worker.
fs::path IsolatedDestination(const std::wstring& root, const std::wstring& input) {
    const auto first = input.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = input.find_last_not_of(L" \t\r\n");
    fs::path path(input.substr(first, last - first + 1));
    path.make_preferred();
    if (!path.is_absolute() || path.native().compare(0, 4, L"\\\\?\\") == 0 ||
        path.native().compare(0, 4, L"\\\\.\\") == 0) return {};
    for (const auto& part : path.relative_path()) {
        const auto& name = part.native();
        if (name.empty() || name == L"." || name == L"..") continue;
        if (name.back() == L'.' || name.back() == L' ' || name.find_first_of(L"<>:\"|?*\t\r\n") != std::wstring::npos)
            return {};
        const auto device = name.substr(0, name.find(L'.'));
        if (_wcsicmp(device.c_str(), L"CON") == 0 || _wcsicmp(device.c_str(), L"PRN") == 0 ||
            _wcsicmp(device.c_str(), L"AUX") == 0 || _wcsicmp(device.c_str(), L"NUL") == 0 ||
            (device.size() == 4 && (_wcsnicmp(device.c_str(), L"COM", 3) == 0 || _wcsnicmp(device.c_str(), L"LPT", 3) == 0) &&
                std::wstring_view(L"123456789\u00b9\u00b2\u00b3").find(device[3]) != std::wstring_view::npos)) return {};
    }
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
    if (!IsWithin(root, path)) return {};
    for (auto current = path; current != current.root_path(); current = current.parent_path()) {
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                (current != path && !(attributes & FILE_ATTRIBUTE_DIRECTORY))) return {};
        } else {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return {};
        }
    }
    return path;
}

std::wstring PickPath(HWND owner, const wchar_t* title, const std::wstring& initialFolder,
                      const std::wstring& filename = {}) {
    using Microsoft::WRL::ComPtr;
    const bool folder = filename.empty();
    ComPtr<IFileDialog> dialog;
    const CLSID& cls = folder ? __uuidof(FileOpenDialog) : __uuidof(FileSaveDialog);
    if (FAILED(CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    FILEOPENDIALOGOPTIONS flags{};
    if (FAILED(dialog->GetOptions(&flags))) return {};
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT | FOS_NODEREFERENCELINKS;
    if (folder) flags |= FOS_PICKFOLDERS;
    else flags = (flags & ~FOS_OVERWRITEPROMPT) | FOS_STRICTFILETYPES | FOS_NOTESTFILECREATE;
    if (FAILED(dialog->SetOptions(flags))) return {};
    dialog->SetTitle(title);
    ComPtr<IShellItem> initial;
    if (SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), nullptr, IID_PPV_ARGS(initial.GetAddressOf()))))
        dialog->SetFolder(initial.Get());
    if (!folder) {
        const COMDLG_FILTERSPEC filter{L"JPG 文件（*.jpg）", L"*.jpg"};
        dialog->SetFileTypes(1, &filter);
        dialog->SetDefaultExtension(L"jpg");
        dialog->SetFileName(filename.c_str());
    }
    ComPtr<IShellItem> selected;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(selected.GetAddressOf()))) return {};
    PWSTR rawPath = nullptr;
    if (FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) return {};
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(rawPath, CoTaskMemFree);
    return path ? std::wstring(path.get()) : std::wstring();
}

std::vector<unsigned char> ReadGalleryPreview(const std::wstring& date, int size) {
    const int32_t length = Library_GetImage(date.c_str(), size + 1, nullptr, 0);
    if (length <= 0) throw std::runtime_error(u8"无法读取所选尺寸的图库预览。");
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    if (Library_GetImage(date.c_str(), size + 1, bytes.data(), length) != length)
        throw std::runtime_error(u8"无法读取完整的图库预览。");
    return bytes;
}

void WriteGalleryDetails(const fs::path& path, const std::wstring& details) {
    const int length = static_cast<int>(details.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, details.data(), length,
                                         nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error(u8"无法编码壁纸信息文本。");
    std::string output(static_cast<size_t>(bytes) + 3, '\0');
    output.replace(0, 3, "\xef\xbb\xbf");
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, details.data(), length,
                           output.data() + 3, bytes, nullptr, nullptr) != bytes)
        throw std::runtime_error(u8"无法编码壁纸信息文本。");
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error(u8"图片已导出，但无法新建壁纸信息文本。");
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(output.size());
    const bool complete = WriteFile(file, output.data(), size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!complete) throw std::runtime_error(u8"图片已导出，但未能完整写入壁纸信息文本。");
}

std::wstring WorkerError(const std::exception& error) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, error.what(), -1, nullptr, 0);
    if (size <= 1) return L"后台操作失败。";
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, error.what(), -1, result.data(), size))
        return L"后台操作失败。";
    result.pop_back();
    return result;
}

HWND DialogOwner(HWND mainWindow) {
    const HWND active = GetActiveWindow();
    return active ? active : mainWindow;
}

void SetRelocationProgress(int position, bool busy = false) {
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, L"StoutWallOriginalWindow", L"请指定新的壁纸库位置:")) != nullptr) {
        if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) continue;
        const HWND bar = FindWindowExW(window, nullptr, PROGRESS_CLASSW, nullptr);
        if (bar) {
            const LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE);
            if (!busy) SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
            SetWindowLongPtrW(bar, GWL_STYLE, busy ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
            if (busy) SendMessageW(bar, PBM_SETMARQUEE, TRUE, 50);
            else SendMessageW(bar, PBM_SETPOS, position, 0);
        }
        break;
    }
}

struct PreviewKey {
    std::wstring directory, date;
    int size = -1;

    bool operator==(const PreviewKey& other) const {
        return directory == other.directory && date == other.date && size == other.size;
    }
};

} // namespace

ProductOptions ParseProductOptions(HINSTANCE instance) {
    ProductOptions options;
    options.runtime = ModuleDirectory(instance);

    int argc = 0;
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argc), &LocalFree);
    if (!arguments) throw std::runtime_error("Unable to parse command line");
    const auto argv = arguments.get();
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument(argv[i]);
        if (argument == L"--test-mode") {
            options.testMode = true;
            continue;
        }
        if (argument == L"--ui-test") {
            options.testMode = true;
            options.uiTest = true;
            continue;
        }
        // Older launchers may still pass this flag; explicit test flags take precedence.
        if (argument == L"--allow-wallpaper") continue;
        if (argument == L"-q") {
            options.quiet = true;
            continue;
        }
        const wchar_t* keys[] = {L"--data-root", L"--config", L"--proxy"};
        std::wstring* targets[] = {&options.dataRoot, &options.configPath, &options.proxy};
        bool consumed = false;
        for (size_t k = 0; k < _countof(keys); ++k) {
            const std::wstring prefix = std::wstring(keys[k]) + L"=";
            if (argument.compare(0, prefix.size(), prefix) == 0) *targets[k] = argument.substr(prefix.size());
            else if (argument == keys[k]) {
                if (i + 1 >= argc || argv[i + 1][0] == L'-')
                    throw std::runtime_error("Missing command-line option value");
                *targets[k] = argv[++i];
            } else {
                continue;
            }
            if (k < 2 && targets[k]->empty()) throw std::runtime_error("Data and config paths must not be empty");
            consumed = true;
            break;
        }
        if (!consumed) throw std::runtime_error("Unknown command-line option");
    }

    const auto productRoot = fs::absolute(DefaultDataRoot()).lexically_normal();
    if (!options.testMode && (!options.dataRoot.empty() || !options.configPath.empty()))
        throw std::runtime_error("--data-root and --config require --test-mode or --ui-test");
    if (options.dataRoot.empty()) options.dataRoot = (options.testMode ? NewTestDataRoot() : productRoot).wstring();
    options.dataRoot = fs::absolute(options.dataRoot).lexically_normal().make_preferred().wstring();
    if (options.testMode && (IsWithin(productRoot, options.dataRoot) || IsWithin(options.dataRoot, productRoot)))
        throw std::runtime_error("The test data root must be separate from the product data root");

    if (options.configPath.empty()) options.configPath = (fs::path(options.dataRoot) / L"StoutWall.cfg").wstring();
    options.configPath = fs::absolute(options.configPath).lexically_normal().make_preferred().wstring();
    if (!IsWithin(options.dataRoot, options.configPath) || IsWithin(options.configPath, options.dataRoot))
        throw std::runtime_error("--config must be located inside --data-root");
    if (options.testMode && IsolatedDestination(options.dataRoot, options.configPath).empty())
        throw std::runtime_error("The isolated config path must not use a redirected directory");
    return options;
}

struct ProductUi::State {
    HINSTANCE instance;
    ProductOptions options;
    std::unique_ptr<ReplicaUi> ui;
    TaskRunner runner;
    std::unique_ptr<ProductBackend> backend;
    ProductSnapshot snapshot;
    bool closing = false;
    bool shutdownQueued = false;
    bool initialized = false;
    bool sourceBusy = false;
    bool relocateBusy = false;
    bool updatingRelocationEdit = false;
    int galleryOperation = 0;
    int gallerySize = 0;
    std::wstring pendingGalleryDate, pendingRelocationPath;
    std::vector<std::wstring> galleryRows;
    std::vector<unsigned char> noPicture, drawnPreview;
    PreviewKey requestedPreview, cachedPreview;
    uint64_t previewTask = 0;
    std::vector<unsigned char> previewBytes;
    std::wstring previewError;
    bool trayInstalled = false;
    HICON trayIcon = nullptr;
    UINT taskbarCreated = 0;
    ReplicaUi::ImageMode viewerMode = ReplicaUi::ImageMode::Fit;
    v6::AssetModule assets;

    State(HINSTANCE h, ProductOptions o) : instance(h), options(std::move(o)) {}

    ProductRequest Request(ProductAction action, int value = 0) const {
        ProductRequest request;
        request.action = action;
        request.value = value;
        return request;
    }

    TaskResult Result(int operation, ProductSnapshot value) {
        TaskResult result;
        result.operation = operation;
        result.payload = std::make_shared<ProductSnapshot>(std::move(value));
        return result;
    }

    bool SubmitAction(ProductRequest request) {
        if (closing || !initialized) return false;
        const int operation=request.action==ProductAction::ExportTheme?kThemeOperation:
            request.action==ProductAction::StoreLatest?kStoreOperation:
            request.action==ProductAction::Tick?kTickOperation:
            request.action==ProductAction::AutoSave?kAutoSaveOperation:
            request.action==ProductAction::ClearFavorites?kClearFavoritesOperation:
            (request.action==ProductAction::ChangeFrequency||request.action==ProductAction::Pause||
             request.action==ProductAction::ChangeMode)?kRestartClockOperation:kActionOperation;
        return runner.Submit(operation, [this, request, operation]() {
            return Result(operation, backend->Execute(request));
        }) != 0;
    }

    void UpdateSourceUi(bool selectSavedSource) {
        if (selectSavedSource) {
            for (size_t i = 0; i < _countof(kSourceRadios); ++i)
                ui->SetChecked(kSourceWindow, kSourceRadios[i], snapshot.config.source == SWN_GLOBAL + i);
        }
        for (const auto* radio : kSourceRadios) ui->SetEnabled(kSourceWindow, radio, !sourceBusy);
        ui->SetEnabled(kSourceWindow, L"按钮EX1", false); // The original has no availability-test handler.
        ui->SetEnabled(kSourceWindow, L"按钮EX13", !sourceBusy);
        ui->SetText(kSourceWindow, L"按钮EX13", sourceBusy ? L"刷新中…" : L"确定");
    }

    void HandleSourceEvent(const UiEvent& event) {
        if (sourceBusy || event.notification != BN_CLICKED) return;
        for (size_t i = 0; i < _countof(kSourceRadios); ++i) {
            if (event.control != kSourceRadios[i]) continue;
            for (const auto* radio : kSourceRadios) ui->SetChecked(kSourceWindow, radio, event.control == radio);
            return;
        }
        if (event.control != L"按钮EX13") return;
        int source = 0;
        for (size_t i = 0; i < _countof(kSourceRadios); ++i)
            if (ui->Selection(kSourceWindow, kSourceRadios[i]) == BST_CHECKED) source = SWN_GLOBAL + static_cast<int>(i);
        if (!source) return;

        sourceBusy = runner.Submit(kSourceOperation, [this, source, directory = snapshot.galleryDir]() {
            if (options.testMode && (IsolatedDestination(options.dataRoot, directory).empty() ||
                                     IsolatedDestination(options.dataRoot, options.configPath).empty()))
                throw std::runtime_error(u8"当前图库或配置文件已不在隔离测试目录内。");
            auto saved = backend->Execute(Request(ProductAction::Source, source));
            try {
                return Result(kSourceOperation, backend->Execute(Request(ProductAction::Refresh)));
            } catch (const std::exception& error) {
                // The source was saved even when the network refresh failed.
                saved.status = L"来源已保存，但刷新今日壁纸失败。";
                auto result = Result(kSourceOperation, std::move(saved));
                result.code = -1;
                result.message = L"来源已保存，但刷新今日壁纸失败。\n\n" + WorkerError(error);
                return result;
            }
        }) != 0;
        UpdateSourceUi(false);
    }

    fs::path RelocationDestination(const std::wstring& input) const {
        const auto start = input.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) return {};
        const fs::path entered = input.substr(start, input.find_last_not_of(L" \t\r\n") - start + 1);
        auto path = options.testMode ? IsolatedDestination(options.dataRoot, entered)
                                     : entered.is_absolute() ? entered.lexically_normal() : fs::path{};
        if (path.empty() || path.native().size() >= SWP_PATH_CAP) return {};
        return path;
    }

    void UpdateRelocationUi(const wchar_t* overrideStatus = nullptr) {
        if (ui->GetText(kRelocateWindow, L"按钮EX1").empty()) return;
        const auto input = ui->GetText(kRelocateWindow, L"编辑框EX2");
        const auto path = RelocationDestination(input);
        const fs::path current = snapshot.galleryDir;
        std::wstring status;
        bool valid = false;
        if (relocateBusy) status = L"正在检查目标壁纸库并切换或迁移……";
        else if (input.find_first_not_of(L" \t\r\n") == std::wstring::npos) status = L"请指定一个壁纸库目录！";
        else if (path.empty()) status = options.testMode ? L"请选择隔离测试目录内的有效路径。" : L"请选择有效的壁纸库路径。";
        else if (IsWithin(current, path) && IsWithin(path, current)) status = L"已选的是当前的壁纸库目录！";
        else if (IsWithin(current, path) || IsWithin(path, current)) status = L"请选择与当前壁纸库互不包含的目录。";
        else {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                status = L"目标是文件，请指定一个壁纸库目录。";
            else {
                valid = true;
                const DWORD index = GetFileAttributesW((path / kIndexFile).c_str());
                status = index == INVALID_FILE_ATTRIBUTES
                    ? L"未找到图库索引，确定后将尝试迁移当前壁纸库。"
                    : L"找到图库索引，确定后将检查完整性并尝试切换。";
            }
        }
        ui->SetText(kRelocateWindow, L"标签EX6", overrideStatus ? overrideStatus : status.c_str());
        ui->SetText(kRelocateWindow, L"按钮EX1", relocateBusy ? L"操作中…" : kRelocateButton);
        ui->SetEnabled(kRelocateWindow, L"编辑框EX2", !relocateBusy);
        ui->SetEnabled(kRelocateWindow, L"按钮EX2", !relocateBusy);
        ui->SetEnabled(kRelocateWindow, L"按钮EX1", valid && !relocateBusy && !galleryOperation);
    }

    void SetRelocationEdit(const std::wstring& value) {
        updatingRelocationEdit = true;
        ui->SetText(kRelocateWindow, L"编辑框EX2", value);
        updatingRelocationEdit = false;
    }

    void HandleRelocationEvent(const UiEvent& event) {
        if (relocateBusy || updatingRelocationEdit) return;
        if (event.control == L"编辑框EX2" && event.notification == EN_CHANGE) {
            SetRelocationProgress(0);
            UpdateRelocationUi();
        } else if (event.notification == BN_CLICKED && event.control == L"按钮EX2") {
            const auto path = PickPath(DialogOwner(ui->MainWindow()), L"选择新的壁纸库位置", options.dataRoot);
            if (!path.empty()) {
                SetRelocationEdit(path);
                SetRelocationProgress(0);
                UpdateRelocationUi();
            }
        } else if (event.notification == BN_CLICKED && event.control == L"按钮EX1" && !galleryOperation) {
            const auto path = RelocationDestination(ui->GetText(kRelocateWindow, L"编辑框EX2"));
            if (path.empty() || IsWithin(snapshot.galleryDir, path) || IsWithin(path, snapshot.galleryDir)) return;
            relocateBusy = runner.Submit(kRelocateOperation, [this, path, original = snapshot.galleryDir]() {
                const auto checked = RelocationDestination(path.wstring());
                if (checked.empty() || (options.testMode &&
                    (IsolatedDestination(options.dataRoot, original).empty() ||
                     IsolatedDestination(options.dataRoot, options.configPath).empty())))
                    throw std::runtime_error(u8"图库路径无效或已改变。");
                auto request = Request(ProductAction::Relocate);
                request.text = checked.wstring();
                return Result(kRelocateOperation, backend->Execute(request));
            }) != 0;
            if (relocateBusy) pendingRelocationPath = path.wstring();
            SetRelocationProgress(0, relocateBusy);
            UpdateRelocationUi();
            UpdateGalleryUi();
        }
    }

    int GalleryIndex(const std::wstring& date) const {
        const auto& records = snapshot.records;
        const auto found = std::find_if(records.begin(), records.end(), [&](const SWL_Record& record) {
            return RecordDate(record) == date;
        });
        return found == records.end() ? -1 : static_cast<int>(found - records.begin());
    }

    PreviewKey CurrentPreviewKey() const {
        return {snapshot.galleryDir, snapshot.hasSelected ? RecordDate(snapshot.selected) : L"", gallerySize};
    }

    void RequestPreview(const PreviewKey& key) {
        if ((previewTask && requestedPreview == key) || cachedPreview == key) return;
        requestedPreview = key;
        previewTask = runner.Submit(kPreviewOperation, [this, key]() {
            if (options.testMode && IsolatedDestination(options.dataRoot, key.directory).empty())
                throw std::runtime_error(u8"当前图库已不在隔离测试目录内。");
            TaskResult result;
            result.bytes = ReadGalleryPreview(key.date, key.size);
            return result;
        });
    }

    void UpdateGalleryUi(bool opened = false) {
        if (ui->GetText(kGalleryWindow, L"按钮EX6").empty()) return;
        const auto key = CurrentPreviewKey();
        const int index = GalleryIndex(key.date);
        std::vector<std::wstring> rows;
        rows.reserve(snapshot.records.size());
        for (const auto& record : snapshot.records)
            rows.push_back(RecordDate(record) + (record.favorite ? L"[收藏]" : L""));
        if (opened) {
            ui->SetItems(kGalleryWindow, L"组合框EX2", kSizes);
            ui->SetSelection(kGalleryWindow, L"组合框EX2", gallerySize);
        }
        if (opened || rows != galleryRows) {
            ui->SetItems(kGalleryWindow, L"列表框EX1", rows);
            galleryRows = std::move(rows);
        }
        const int wantedIndex = galleryOperation && !pendingGalleryDate.empty() ? GalleryIndex(pendingGalleryDate) : index;
        if (ui->Selection(kGalleryWindow, L"列表框EX1") != wantedIndex)
            ui->SetSelection(kGalleryWindow, L"列表框EX1", wantedIndex);

        const bool selected = index >= 0;
        const bool idle = !closing && !galleryOperation && !relocateBusy;
        ui->SetEnabled(kGalleryWindow, L"列表框EX1", idle);
        ui->SetEnabled(kGalleryWindow, L"组合框EX2", idle);
        ui->SetEnabled(kGalleryWindow, L"按钮EX6", idle);
        for (const auto* control : {L"按钮EX1", L"按钮EX2", L"按钮EX3", L"多功能按钮EX1"})
            ui->SetEnabled(kGalleryWindow, control, idle && selected);
        ui->SetText(kGalleryWindow, L"按钮EX1", selected && snapshot.selected.favorite ? L"取消收藏" : L"收藏");

        std::wstring details = selected ? GalleryDetails(snapshot.selected, index, snapshot.records.size())
                                         : L"请从左侧列表内选择一个壁纸以查看和进行操作";
        const std::vector<unsigned char>* image = &noPicture;
        if (selected && gallerySize == static_cast<int>(snapshot.config.currentWall.size)) {
            if (!snapshot.selectedImage.empty()) image = &snapshot.selectedImage;
        } else if (selected) {
            if (cachedPreview == key) {
                if (!previewBytes.empty()) image = &previewBytes;
                if (!previewError.empty()) details += L"\r\n" + previewError;
            } else if (idle) {
                RequestPreview(key);
                details += L"\r\n正在加载预览……";
            }
        }
        if (ui->GetText(kGalleryWindow, L"编辑框EX1") != details)
            ui->SetText(kGalleryWindow, L"编辑框EX1", details);
        if (opened || drawnPreview != *image) {
            if (ui->SetImage(kGalleryWindow, L"图片框EX1", *image)) drawnPreview = *image;
            else {
                ui->SetImage(kGalleryWindow, L"图片框EX1", noPicture);
                drawnPreview = noPicture;
            }
        }
    }

    void SelectGalleryRecord(const std::wstring& date) {
        auto request = Request(ProductAction::BrowseSelect);
        request.text = date;
        SubmitGalleryOperation(std::move(request));
    }

    void SubmitGalleryOperation(ProductRequest request, std::wstring date = {}, int operation = kGalleryOperation,
                                std::wstring exportDetails = {}) {
        if (closing || galleryOperation || relocateBusy) return;
        const auto nextDate = request.action == ProductAction::BrowseSelect ? request.text : date;
        if (!runner.Submit(operation, [this, request = std::move(request), date = std::move(date), operation,
                                       directory = snapshot.galleryDir, details = std::move(exportDetails)]() {
            if (options.testMode && (IsolatedDestination(options.dataRoot, directory).empty() ||
                IsolatedDestination(options.dataRoot, options.configPath).empty()))
                throw std::runtime_error(u8"当前壁纸库或配置不在隔离测试目录内，或路径已改变。");
            fs::path detailsPath;
            if (operation == kExportOperation) {
                const auto path = options.testMode ? IsolatedDestination(options.dataRoot, request.text)
                                                   : fs::path(request.text).lexically_normal();
                if (path.empty() || IsWithin(directory, path))
                    throw std::runtime_error(u8"导出路径无效或与当前壁纸库冲突。");
                if (request.action == ProductAction::ExportAllSizes) {
                    const auto checkFile = [&](const fs::path& file) {
                        if ((options.testMode && IsolatedDestination(options.dataRoot, file.wstring()).empty()) || IsWithin(directory, file))
                            throw std::runtime_error(u8"导出目标中包含无效的文件路径。");
                        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES)
                            throw std::runtime_error(u8"导出目录已有同名文件，请另选目录。");
                    };
                    for (const auto* size : kSizeNames) checkFile(path / (date + L"_" + size + L".jpg"));
                    detailsPath = path / (date + L".txt");
                    checkFile(detailsPath);
                }
            }
            if (!date.empty()) {
                auto select = Request(ProductAction::BrowseSelect);
                select.text = date;
                backend->Execute(select);
            }
            auto value = backend->Execute(request);
            if (!detailsPath.empty()) WriteGalleryDetails(detailsPath, details);
            return Result(operation, std::move(value));
        })) return;
        pendingGalleryDate = nextDate;
        galleryOperation = operation;
        UpdateGalleryUi();
        UpdateRelocationUi();
    }

    void ExportGalleryRecord() {
        const HWND owner = DialogOwner(ui->MainWindow());
        const auto record = snapshot.selected;
        const auto date = RecordDate(record);
        const auto directory = snapshot.galleryDir;
        const auto details = GalleryDetails(record, GalleryIndex(date), snapshot.records.size());
        const int currentSize = gallerySize;
        const HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, 1, L"导出当前预览尺寸");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < kSizes.size(); ++i) AppendMenuW(menu, MF_STRING, 2 + i, kSizes[i].c_str());
        AppendMenuW(menu, MF_STRING, 7, L"导出全部尺寸");
        const int choice = static_cast<int>(ChooseMenu(menu, owner));
        if (choice < 1 || choice > 7 || closing) return;
        const bool allSizes = choice == 7;
        const int size = choice == 1 ? currentSize : choice - 2;
        const auto path = PickPath(owner, L"请选择壁纸导出到的位置", options.dataRoot,
            allSizes ? std::wstring() : SuggestedJpeg(record, size));
        if (path.empty() || closing || relocateBusy || snapshot.galleryDir != directory) return;
        const auto checked = options.testMode ? IsolatedDestination(options.dataRoot, path) : fs::path(path).lexically_normal();
        if (checked.empty() || IsWithin(snapshot.galleryDir, checked)) {
            const auto* message = options.testMode ? L"请选择隔离测试目录内、当前壁纸库之外的导出位置。"
                                                   : L"请选择当前壁纸库之外的导出位置。";
            MessageBoxW(owner, message, L"导出壁纸", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!allSizes && GetFileAttributesW(checked.c_str()) != INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(owner, L"目标文件已存在，请另选文件名。", L"导出壁纸", MB_OK | MB_ICONWARNING);
            return;
        }
        auto request = Request(allSizes ? ProductAction::ExportAllSizes : ProductAction::ExportBrowse,
                               allSizes ? 0 : size + 1);
        request.text = checked.wstring();
        SubmitGalleryOperation(std::move(request), date, kExportOperation, allSizes ? details : L"");
    }

    void HandleGalleryEvent(const UiEvent& event) {
        if (galleryOperation || relocateBusy) return;
        const auto& control = event.control;
        if (control == L"列表框EX1" && event.notification == LBN_SELCHANGE && event.selection >= 0 &&
            static_cast<size_t>(event.selection) < snapshot.records.size()) {
            SelectGalleryRecord(RecordDate(snapshot.records[static_cast<size_t>(event.selection)]));
            return;
        }
        if (control == L"组合框EX2" && event.notification == CBN_SELCHANGE) {
            if (event.selection >= 0 && event.selection < SWN_SIZE_COUNT) {
                gallerySize = event.selection;
                UpdateGalleryUi();
            }
            return;
        }
        if (event.notification != BN_CLICKED) return;
        if (control == L"按钮EX6") {
            cachedPreview = {};
            previewTask = 0;
            if (snapshot.hasSelected) SelectGalleryRecord(RecordDate(snapshot.selected));
            else SubmitGalleryOperation(Request(ProductAction::BrowseFilter));
            return;
        }
        const auto date = RecordDate(snapshot.selected);
        const int index = GalleryIndex(date);
        if (!snapshot.hasSelected || index < 0 || ui->Selection(kGalleryWindow, L"列表框EX1") != index) return;
        const auto directory = snapshot.galleryDir;
        const auto sameGallery = [&]() { return !relocateBusy && snapshot.galleryDir == directory; };
        const HWND owner = DialogOwner(ui->MainWindow());
        if (control == L"按钮EX1") {
            SubmitGalleryOperation(Request(ProductAction::BrowseFavorite), date);
        } else if (control == L"按钮EX2") {
            if ((snapshot.config.mode == SWP_GALLERY || MessageBoxW(owner,
                L"您需要切换到【壁纸库模式】才能应用特定壁纸。是否切换并应用本张壁纸？",
                L"请注意", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) && sameGallery())
                SubmitGalleryOperation(Request(ProductAction::BrowseApply), date);
        } else if (control == L"按钮EX3") {
            if (snapshot.total <= 2) {
                MessageBoxW(owner, L"壁纸库内至少要有两张壁纸，不能再删除啦！", L"删除失败", MB_OK | MB_ICONERROR);
                return;
            }
            const auto name = L"壁纸 " + date + L"（" + RecordTitle(snapshot.selected) + L"）";
            const auto first = L"确定要删除" + name + L"吗？删除后将无法恢复。";
            if (MessageBoxW(owner, first.c_str(), L"删除本图", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES || !sameGallery()) return;
            const auto second = L"再次确认删除" + name + L"。非今日的壁纸删除后可能无法重新获取。确定删除请点击【否】。";
            if (MessageBoxW(owner, second.c_str(), L"删除本图 [二次确认]", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1) == IDNO && sameGallery())
                SubmitGalleryOperation(Request(ProductAction::BrowseDelete), date);
        } else if (control == L"多功能按钮EX1") {
            ExportGalleryRecord();
        } else if (control == L"图片框EX1" && event.notification == STN_CLICKED) {
            if (gallerySize == static_cast<int>(snapshot.config.currentWall.size)) OpenViewer(snapshot.selectedImage);
            else if (cachedPreview == CurrentPreviewKey()) OpenViewer(previewBytes);
        }
    }

    void UpdateUi(const ProductSnapshot& value) {
        if(snapshot.intervalMs!=value.intervalMs) ScheduleNextTick(value.intervalMs);
        snapshot = value;
        const auto& config = snapshot.config;
        ui->SetText(L"_启动窗口", L"标签EX1", snapshot.hasCurrent ? BoundedText(snapshot.current.title, _countof(snapshot.current.title)) : L"");
        ui->SetText(L"_启动窗口", L"标签EX2", snapshot.hasCurrent ? BoundedText(snapshot.current.description, _countof(snapshot.current.description)) : L"");
        ui->SetText(kMainWindow,L"标签EX3",snapshot.hasCurrent?L"(C) "+BoundedText(snapshot.current.author,_countof(snapshot.current.author)):L"");
        const wchar_t* sizes[]={L"3840x2160",L"1920x1200",L"1920x1080",L"1024x768",L"1080x1920"};
        ui->SetText(kMainWindow,L"标签EX4",snapshot.hasCurrent&&config.currentWall.size<5?sizes[config.currentWall.size]:L"");
        auto date=BoundedText(snapshot.current.date,_countof(snapshot.current.date));
        if(date.size()==8){date.insert(6,L".");date.insert(4,L".");}
        ui->SetText(kMainWindow,L"标签EX5",date);
        ui->SetText(kMainWindow,L"编辑框EX2",L"【壁纸库信息】\r\n\r\n壁纸库位置："+snapshot.galleryDir+
            L"\r\n壁纸数量："+std::to_wstring(snapshot.total)+L"\r\n收藏数量："+std::to_wstring(snapshot.favorites)+
            L"\r\n自动保存："+(config.autoSave?L"已打开":L"已关闭"));
        if (!snapshot.image.empty()) ui->SetImage(kMainWindow,L"图片框EX1",snapshot.image);
        ui->SetChecked(L"_启动窗口", L"选择框EX1", config.autoSave != 0);
        ui->SetChecked(L"_启动窗口", L"选择框EX2", config.changeRandom != 0);
        ui->SetChecked(L"_启动窗口", L"选择框EX3", snapshot.autoRunPreference);
        ui->SetChecked(L"_启动窗口", L"单选框EX1", config.mode == SWP_DAILY);
        ui->SetChecked(L"_启动窗口", L"单选框EX2", config.mode == SWP_GALLERY);
        ui->SetChecked(L"_启动窗口", L"单选框EX3", config.mode == SWP_FAVORITES);
        ui->SetSelection(L"_启动窗口", L"组合框EX1", static_cast<int>(config.changeFreq));
        ui->SetSelection(L"_启动窗口", L"组合框EX2", static_cast<int>(config.currentWall.size));
        const bool canStep=!closing&&(config.mode==SWP_GALLERY?snapshot.total>1:config.mode==SWP_FAVORITES&&snapshot.favorites>1);
        ui->SetEnabled(kMainWindow,L"图片按钮EX1",canStep);
        ui->SetEnabled(kMainWindow,L"图片按钮EX2",canStep);
        ui->SetEnabled(kMainWindow,L"图片按钮EX4",!closing&&snapshot.hasCurrent);
        ui->SetEnabled(L"_启动窗口", L"图片按钮EX5", !closing);
        ui->SetEnabled(kMainWindow,L"按钮EX9",config.changeRandom&&config.mode!=SWP_DAILY);
        UpdateGalleryUi();
    }

    void AddTray() {
        NOTIFYICONDATAW data{}; data.cbSize=sizeof(data);data.hWnd=ui->MainWindow();data.uID=1;
        data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;data.hIcon=trayIcon;data.uCallbackMessage=kTrayMessage;
        wcscpy_s(data.szTip,L"胖哥每日壁纸");
        trayInstalled=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
    }

    void RemoveTray() {
        if (!trayInstalled)return;
        NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=ui->MainWindow();data.uID=1;
        Shell_NotifyIconW(NIM_DELETE,&data);trayInstalled=false;
    }

    UINT ChooseMenu(HMENU menu, HWND owner) {
        if (!menu)return 0;
        POINT cursor{};GetCursorPos(&cursor);
        SetForegroundWindow(owner);
        const UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,
            cursor.x,cursor.y,0,owner,nullptr);
        DestroyMenu(menu);PostMessageW(owner,WM_NULL,0,0);
        return command;
    }

    void AskExit() {
        if(MessageBoxW(ui->MainWindow(),L"退出本软件后，您的桌面壁纸将不会更新。",L"要退出本软件吗？",
            MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)BeginClose();
    }

    void ShowTrayMenu() {
        if (!IsWindowEnabled(ui->MainWindow())) { ui->Show(kMainWindow); return; }
        HMENU menu=CreatePopupMenu();if(!menu)return;
        auto add=[&](UINT id,const wchar_t* title,UINT flags=0){AppendMenuW(menu,MF_STRING|flags,id,title);};
        auto separator=[&](){AppendMenuW(menu,MF_SEPARATOR,0,nullptr);};
        if(snapshot.hasCurrent) {
            add(1,snapshot.current.title);add(0,snapshot.current.description,MF_GRAYED);
            add(0,snapshot.current.author,MF_GRAYED);separator();
        }
        const bool step=snapshot.config.mode==SWP_GALLERY?snapshot.total>1:
            snapshot.config.mode==SWP_FAVORITES&&snapshot.favorites>1;
        add(2,L"上一张",step?0:MF_GRAYED);add(3,L"下一张",step?0:MF_GRAYED);
        add(4,L"暂停自动更换",snapshot.config.paused?MF_CHECKED:0);separator();
        add(5,L"收藏这张壁纸",snapshot.current.favorite?MF_CHECKED:0);
        add(6,L"保存这张壁纸");add(7,L"重新加载今日壁纸");add(8,L"查看七日壁纸",MF_GRAYED);separator();
        add(9,L"每日一图模式",snapshot.config.mode==SWP_DAILY?MF_CHECKED:0);
        add(10,L"壁纸库模式",snapshot.config.mode==SWP_GALLERY?MF_CHECKED:0);
        add(11,L"收藏模式",snapshot.config.mode==SWP_FAVORITES?MF_CHECKED:0);
        add(12,L"七日壁纸模式",MF_GRAYED);separator();
        add(13,L"打开主窗口");add(0,L"关闭本菜单");add(14,L"退出");
        const UINT command=ChooseMenu(menu,ui->MainWindow());
        switch(command) {
        case 1:OpenPage(ui->MainWindow(),snapshot.current.link);break;
        case 2:SubmitAction(Request(ProductAction::Previous));break;
        case 3:SubmitAction(Request(ProductAction::Next));break;
        case 4:SubmitAction(Request(ProductAction::Pause,snapshot.config.paused?0:1));break;
        case 5:SubmitAction(Request(ProductAction::Favorite));break;
        case 6:SubmitAction(Request(ProductAction::StoreLatest));break;
        case 7:SubmitAction(Request(ProductAction::Refresh));break;
        case 9:case 10:case 11:SubmitAction(Request(ProductAction::ChangeMode,static_cast<int>(command)-8));break;
        case 13:ui->Show(kMainWindow);break;
        case 14:AskExit();break;
        }
    }

    void OpenViewer(const std::vector<unsigned char>& bytes) {
        if(bytes.empty())return;
        if (!ui->WindowHandle(kViewerWindow)) viewerMode = ReplicaUi::ImageMode::Fit;
        ui->Show(kViewerWindow,ui->WindowHandle(kGalleryWindow));
        ui->SetImage(kViewerWindow,L"图片框EX1",bytes);
    }

    void SetViewerMode(ReplicaUi::ImageMode mode) {
        viewerMode=mode;
        ui->SetImageMode(kViewerWindow,L"图片框EX1",mode);
    }

    void HandleViewerEvent(const UiEvent& event) {
        if(event.control==L"超级按钮EX1"&&event.notification==BN_CLICKED) {ui->Close(kViewerWindow);return;}
        if(event.control!=L"图片框EX1"||event.notification!=WM_CONTEXTMENU)return;
        HMENU menu=CreatePopupMenu();if(!menu)return;
        const wchar_t* names[]={L"适应屏幕",L"居中",L"平铺",L"拉伸"};
        for(UINT i=0;i<_countof(names);++i) AppendMenuW(menu,MF_STRING,i+1,names[i]);
        CheckMenuRadioItem(menu,1,4,1+static_cast<UINT>(viewerMode),MF_BYCOMMAND);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,5,L"点击屏幕中间上方的X关闭大图");
        const UINT command=ChooseMenu(menu,ui->WindowHandle(kViewerWindow));
        if(command==5)ui->Close(kViewerWindow);
        else if(command>0&&command<5) SetViewerMode(static_cast<ReplicaUi::ImageMode>(command-1));
    }

    void HandleIosEvent(const UiEvent& event) {
        if(event.notification!=BN_CLICKED&&event.notification!=STN_CLICKED)return;
        const wchar_t* address=nullptr;
        if(event.control==L"图片框EX1")address=L"https://www.icloud.com/shortcuts/34f07ae9921b4195b855fa4b4e35e814";
        if(event.control==L"图片框EX2")address=L"https://www.icloud.com/shortcuts/fd04e26f4cf246e6868e34cf24d96f2c";
        HWND owner=ui->WindowHandle(event.window);
        if(address&&CopyText(owner,address))MessageBoxW(owner,L"快捷指令链接已经复制到剪贴板，快去分享给苹果设备吧！",
            L"快捷指令链接已复制",MB_OK|MB_ICONINFORMATION);
        if(event.control==L"按钮EX4")OpenPage(owner,L"https://mp.weixin.qq.com/s?__biz=Mzg4NDYyOTA2NA==&mid=2247486223&idx=1&sn=1c0267ff6f47274d8e67272cd2cba775&chksm=cfb40028f8c3893eb8461cee7fa72242b12813f4d84b365c1bb1c82489e4e226b1f1a6b828e7#rd");
    }

    bool themeBusy = false;

    fs::path ThemeDirectory() {
        const auto value=ui->GetText(kThemeWindow,L"编辑框EX2");
        if(value.empty())return {};
        return options.testMode?IsolatedDestination(options.dataRoot,value):fs::path(value);
    }

    void UpdateThemeForm() {
        if(!ui->WindowHandle(kThemeWindow))return;
        for(const auto* group:{L"分组框EX1",L"分组框EX2",L"分组框EX3",L"分组框EX4"})
            ui->SetEnabled(kThemeWindow,group,!themeBusy);
        const bool modern=ui->Selection(kThemeWindow,L"单选框EX4")==BST_CHECKED;
        const bool custom=ui->Selection(kThemeWindow,L"单选框EX5")==BST_CHECKED;
        ui->SetEnabled(kThemeWindow,L"单选框EX1",!themeBusy&&snapshot.favorites>0);
        ui->SetEnabled(kThemeWindow,L"选择框EX3",!themeBusy&&modern);
        ui->SetVisible(kThemeWindow,L"按钮EX3",custom);
        ui->SetEnabled(kThemeWindow,L"按钮EX3",!themeBusy&&custom);
        std::error_code ec;
        const auto directory=ThemeDirectory();
        ui->SetEnabled(kThemeWindow,L"按钮EX1",!themeBusy&&!directory.empty()&&fs::is_directory(directory,ec));
    }

    void SetThemeKind(bool modern) {
        ui->SetChecked(kThemeWindow,L"单选框EX4",modern);
        ui->SetChecked(kThemeWindow,L"单选框EX3",!modern);
        ui->SetChecked(kThemeWindow,L"选择框EX1",true);
        ui->SetVisible(kThemeWindow,L"选择框EX3",modern);
        ui->SetText(kThemeWindow,L"选择框EX3",L"深色模式 (Win11/10)");
        ui->SetText(kThemeWindow,L"标签EX4",modern?L"默认主题颜色：":L"指定主题颜色：");
        ui->SetText(kThemeWindow,L"选择框EX1",modern?L"自动主题色":L"透明效果");
    }

    void OpenTheme() {
        const bool fresh=!ui->WindowHandle(kThemeWindow);
        ui->Show(kThemeWindow,ui->MainWindow());
        if(!fresh||!ui->WindowHandle(kThemeWindow))return;
        ui->SetItems(kThemeWindow,L"组合框EX1",{L"1分钟",L"10分钟",L"30分钟",L"1小时",L"6小时",L"1天"});
        ui->SetItems(kThemeWindow,L"组合框EX2",{L"居中",L"拉伸",L"适应",L"填充",L"平铺"});
        ui->SetItems(kThemeWindow,L"组合框EX3",{L"Win11默认蓝",L"Win10默认蓝",L"Win7默认蓝(天空)",L"深蓝色(黄昏)",L"青绿色(大海)",L"绿色(叶)",L"浅绿色",L"黄色(太阳)",L"橙色(南瓜色)",L"深红色",L"紫红色",L"淡红色",L"紫罗兰色",L"淡紫色",L"灰褐色",L"巧克力色",L"暗灰色",L"白色(白霜)"});
        ui->SetItems(kThemeWindow,L"组合框EX4",kSizes);
        ui->SetSelection(kThemeWindow,L"组合框EX1",2);
        ui->SetSelection(kThemeWindow,L"组合框EX2",3);
        ui->SetSelection(kThemeWindow,L"组合框EX3",0);
        ui->SetSelection(kThemeWindow,L"组合框EX4",static_cast<int>(snapshot.config.currentWall.size));
        ui->SetChecked(kThemeWindow,L"单选框EX2",true);
        ui->SetText(kThemeWindow,L"按钮EX3",L"…");
        if(options.testMode)ui->SetText(kThemeWindow,L"编辑框EX2",options.dataRoot);
        SetThemeKind(true);
        UpdateThemeForm();
    }

    void HandleThemeEvent(const UiEvent& event) {
        if(themeBusy)return;
        const auto& control=event.control;
        if(control==L"编辑框EX2"&&event.notification==EN_CHANGE){UpdateThemeForm();return;}
        if(event.notification!=BN_CLICKED)return;
        if(control==L"单选框EX3"||control==L"单选框EX4")SetThemeKind(control==L"单选框EX4");
        if(control==L"单选框EX1"||control==L"单选框EX2"||control==L"单选框EX5") {
            for(const auto* radio:{L"单选框EX1",L"单选框EX2",L"单选框EX5"})
                ui->SetChecked(kThemeWindow,radio,control==radio);
        }
        UpdateThemeForm();
        HWND owner=ui->WindowHandle(kThemeWindow);
        if(control==L"按钮EX2") {
            const auto selected=PickPath(owner,L"请选择主题包的导出位置",ui->GetText(kThemeWindow,L"编辑框EX2"));
            if(!selected.empty())ui->SetText(kThemeWindow,L"编辑框EX2",selected);
        } else if(control==L"按钮EX3") {
            const bool fresh=!ui->WindowHandle(kThemeSelectionWindow);
            ui->Show(kThemeSelectionWindow,owner);
            if(!fresh)return;
            std::vector<std::wstring> dates;
            for(const auto& record:snapshot.records)
                dates.push_back(RecordDate(record)+(record.favorite?L"[收藏]":L""));
            ui->SetItems(kThemeSelectionWindow,L"列表框Ex",dates);
            if(!dates.empty())ui->SetSelection(kThemeSelectionWindow,L"列表框Ex",0);
        } else if(control==L"按钮EX1"&&initialized&&!closing) {
            auto checked=[&](const wchar_t* name){return ui->Selection(kThemeWindow,name)==BST_CHECKED;};
            if(checked(L"单选框EX5")) {
                MessageBoxW(owner,L"原版手动选择窗口未完成，不能用它导出主题包。",L"导出主题包",MB_OK|MB_ICONINFORMATION);return;
            }
            auto request=Request(ProductAction::ExportTheme);
            auto& theme=request.theme;
            theme.cbSize=sizeof(theme);
            theme.kind=checked(L"单选框EX4")?SWP_THEME_MODERN:SWP_THEME_WIN7;
            theme.selection=checked(L"单选框EX1")?SWP_THEME_FAVORITES:SWP_THEME_ALL;
            theme.intervalIndex=static_cast<DWORD>(ui->Selection(kThemeWindow,L"组合框EX1"));
            theme.style=static_cast<DWORD>(ui->Selection(kThemeWindow,L"组合框EX2"));
            theme.colorIndex=static_cast<DWORD>(ui->Selection(kThemeWindow,L"组合框EX3"));
            theme.sizeIndex=static_cast<DWORD>(ui->Selection(kThemeWindow,L"组合框EX4"));
            theme.shuffle=checked(L"选择框EX2");theme.dark=theme.kind==SWP_THEME_MODERN&&checked(L"选择框EX3");
            theme.transparency=theme.kind==SWP_THEME_WIN7&&checked(L"选择框EX1");
            theme.autoColor=theme.kind==SWP_THEME_MODERN&&checked(L"选择框EX1");
            const auto name=ui->GetText(kThemeWindow,L"编辑框EX1");
            const auto directory=ThemeDirectory().wstring();
            if(name.empty()||directory.empty()||name.size()>=_countof(theme.name)||directory.size()>=_countof(theme.outputDir)||
                (options.testMode&&!IsWithin(options.dataRoot,directory))) {
                MessageBoxW(owner,L"请检查主题名称和保存位置。",L"导出主题包",MB_OK|MB_ICONWARNING);return;
            }
            wcscpy_s(theme.name,name.c_str());wcscpy_s(theme.outputDir,directory.c_str());
            if(!SubmitAction(std::move(request)))return;
            themeBusy=true;
            ui->SetText(kThemeWindow,L"按钮EX1",L"正在导出…");
            ui->SetProgress(kThemeWindow,L"进度条EX1",0,true);
            UpdateThemeForm();
        }
    }

    void ShowAuxiliary(const std::wstring& name) {
        if (name == kSourceWindow || name == kRelocateWindow || name == kGalleryWindow) {
            if (closing || !initialized) return;
            const auto* control = name == kSourceWindow ? L"按钮EX13" : (name == kRelocateWindow ? L"按钮EX1" : L"按钮EX6");
            const bool opened = ui->GetText(name, control).empty();
            ui->Show(name,ui->MainWindow());
            if (name == kSourceWindow) {
                UpdateSourceUi(opened);
            } else if (name == kRelocateWindow) {
                if (opened) {
                    SetRelocationEdit(relocateBusy ? pendingRelocationPath : snapshot.galleryDir);
                    SetRelocationProgress(0, relocateBusy);
                    UpdateRelocationUi(relocateBusy ? nullptr : L"若新位置为空，将迁移现有壁纸库；若已有完整图库，将换用该图库。");
                }
            } else if (opened) {
                gallerySize = snapshot.config.currentWall.size < SWN_SIZE_COUNT
                    ? static_cast<int>(snapshot.config.currentWall.size) : 0;
                UpdateGalleryUi(true);
                if (!snapshot.records.empty() && !galleryOperation && !relocateBusy &&
                    (!snapshot.hasSelected || RecordDate(snapshot.selected) != RecordDate(snapshot.records.front())))
                    SelectGalleryRecord(RecordDate(snapshot.records.front()));
            }
            return;
        }
        ui->Show(name,ui->MainWindow());
    }

    void HandleEvent(const UiEvent& event) {
        if (event.window == kSourceWindow || event.window == kRelocateWindow || event.window == kGalleryWindow) {
            if (closing || !initialized) return;
            if (event.window == kSourceWindow) HandleSourceEvent(event);
            else if (event.window == kRelocateWindow) HandleRelocationEvent(event);
            else HandleGalleryEvent(event);
            return;
        }
        if(event.window==kViewerWindow){HandleViewerEvent(event);return;}
        if(event.window==kThemeWindow){HandleThemeEvent(event);return;}
        if(event.window==L"iOS窗口"){HandleIosEvent(event);return;}
        if (event.window == kMainWindow && event.control == L"__close__") {
            if(trayInstalled)ShowWindow(ui->MainWindow(),SW_HIDE);else BeginClose();return;
        }
        if (event.window == L"_启动窗口") {
            if (event.control == L"组合框EX1" && event.notification==CBN_SELCHANGE&&event.selection >= 0) SubmitAction(Request(ProductAction::ChangeFrequency, event.selection));
            else if (event.control == L"组合框EX2" && event.notification==CBN_SELCHANGE&&event.selection >= 0) SubmitAction(Request(ProductAction::ChangeSize, event.selection));
            else if(event.notification==WM_CONTEXTMENU&&event.control==L"图片框EX1")ShowTrayMenu();
            else if(event.notification!=BN_CLICKED)return;
            else if (event.control == L"选择框EX1") SubmitAction(Request(ProductAction::AutoSave, event.checked ? 1 : 0));
            else if (event.control == L"选择框EX2") SubmitAction(Request(ProductAction::Random, event.checked ? 1 : 0));
            else if (event.control == L"选择框EX3") SubmitAction(Request(ProductAction::AutoRun, event.checked ? 1 : 0));
            else if (event.control == L"单选框EX1" && event.checked) SubmitAction(Request(ProductAction::ChangeMode, SWP_DAILY));
            else if (event.control == L"单选框EX2" && event.checked) SubmitAction(Request(ProductAction::ChangeMode, SWP_GALLERY));
            else if (event.control == L"单选框EX3" && event.checked) SubmitAction(Request(ProductAction::ChangeMode, SWP_FAVORITES));
            else if (event.control == L"按钮EX7") SubmitAction(Request(ProductAction::Refresh));
            else if (event.control == L"按钮EX9") SubmitAction(Request(ProductAction::Reshuffle));
            else if (event.control == L"超级按钮EX1") ShowAuxiliary(L"库管理窗口");
            else if (event.control == L"按钮EX1") ShowAuxiliary(L"库迁移窗口");
            else if (event.control == L"按钮EX8") OpenTheme();
            else if (event.control == L"按钮EX12") ShowAuxiliary(L"壁纸来源窗口");
            else if (event.control == L"按钮EX4") ShowAuxiliary(L"iOS窗口");
            else if (event.control == L"按钮EX11") MessageBoxW(ui->MainWindow(),
                L"联系我(作者)的三种方式：\r\n1.扫描左下方二维码，在B站私信发送问题或建议。\r\n2.扫描旁边的二维码或微信搜一搜‘月半菌’。\r\n3.给我发邮件：crh380bl@outlook.com。",L"软件遇到问题或想提点建议？",MB_OK|MB_ICONINFORMATION);
            else if (event.control == L"按钮EX10"&&MessageBoxW(ui->MainWindow(),
                L"请到微信公众号【月半菌】内点击【胖哥出品】→【每日壁纸】。点击【确定】可直接打开。",
                L"帮助和介绍内容",MB_OKCANCEL|MB_ICONINFORMATION)==IDOK)
                OpenPage(ui->MainWindow(),L"https://mp.weixin.qq.com/mp/homepage?__biz=Mzg4NDYyOTA2NA==&hid=2&sn=70edb0c14643345cd643ecc566234bec");
            else if (event.control == L"按钮EX6") {
                if(MessageBoxW(ui->MainWindow(),L"是否要先看【壁纸库合集下载和使用教程】？",L"壁纸库合集下载",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
                    OpenPage(ui->MainWindow(),L"https://mp.weixin.qq.com/s/aqwuGB5tuU7YgSqkJ-29Vg");
                OpenPage(ui->MainWindow(),L"http://update.ybjun.com:1170");
            }
            else if (event.control == L"按钮EX3") AskExit();
            else if (event.control == L"按钮EX14") OpenPage(ui->MainWindow(),L"https://dailywall.ybjun.com");
            else if (event.control == L"超级按钮EX2") OpenPage(ui->MainWindow(),L"https://www.bing.com/");
            else if (event.control == L"图片框EX3") OpenPage(ui->MainWindow(),L"https://joy.link/yuebanjun");
            else if (event.control == L"图片框EX4") OpenPage(ui->MainWindow(),L"https://space.bilibili.com/306479391");
            else if (event.control == L"图片框EX5") OpenPage(ui->MainWindow(),L"https://www.ybjun.com");
            else if (event.control == L"按钮EX5") {
                const HWND owner=ui->MainWindow();
                if(!snapshot.favorites) {
                    MessageBoxW(owner,L"壁纸库内没有已收藏的壁纸，无需清空啦！",L"清空收藏",MB_OK|MB_ICONERROR);
                    return;
                }
                if(MessageBoxW(owner,L"确定要清空所有收藏的壁纸吗？该操作会将所有已收藏壁纸的收藏状态清空，但不会从壁纸库删除这些壁纸。本操作无法恢复！请谨慎操作！",
                    L"清空收藏",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES&&
                    MessageBoxW(owner,L"再次确认！您将要执行清空所有收藏壁纸的操作。再次提醒，请您谨慎操作！如果确定要清空，请点击下面的【否】按钮。",
                        L"清空收藏[二次确认]",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDNO)
                    SubmitAction(Request(ProductAction::ClearFavorites));
            }
            else if (event.control == L"图片按钮EX3") OpenPage(ui->MainWindow(),snapshot.current.link);
            else if (event.control == L"图片按钮EX1") SubmitAction(Request(ProductAction::Previous));
            else if (event.control == L"图片按钮EX2") SubmitAction(Request(ProductAction::Next));
            else if (event.control == L"图片按钮EX4") SubmitAction(Request(ProductAction::Favorite));
            else if (event.control == L"图片按钮EX5") SubmitAction(Request(ProductAction::Pause, snapshot.config.paused ? 0 : 1));
        }
    }

    void BeginClose() {
        if (shutdownQueued) return;
        closing = true;
        RemoveTray();
        KillTimer(ui->MainWindow(), kClockTimer);
        shutdownQueued = true;
        const auto request = Request(ProductAction::Shutdown);
        if (!runner.Submit(kShutdownOperation, [this, request]() {
                ProductSnapshot snapshot{};
                try {
                    if (backend) snapshot = backend->Execute(request);
                } catch (...) {
                    backend.reset();
                    throw;
                }
                backend.reset();
                return Result(kShutdownOperation, std::move(snapshot));
            })) {
            runner.Stop();
            ui->Quit();
        }
    }

    void FinishClose() {
        runner.Stop();
        ui->Quit();
    }

    bool HandleAuxiliaryResult(TaskResult& result) {
        const int operation = result.operation;
        if (operation == kPreviewOperation) {
            if (result.id != previewTask) return true;
            previewTask = 0;
            if (!closing && requestedPreview == CurrentPreviewKey()) {
                cachedPreview = requestedPreview;
                previewBytes = std::move(result.bytes);
                previewError = result.code ? result.message : L"";
                UpdateGalleryUi();
            }
            return true;
        }
        if (operation != kSourceOperation && operation != kRelocateOperation &&
            operation != kGalleryOperation && operation != kExportOperation) return false;

        if (operation == kSourceOperation) sourceBusy = false;
        else if (operation == kRelocateOperation) {
            relocateBusy = false;
            pendingRelocationPath.clear();
            cachedPreview = {};
            previewTask = 0;
        } else {
            galleryOperation = 0;
            pendingGalleryDate.clear();
        }
        if (closing) return true;

        if (result.payload) UpdateUi(*std::static_pointer_cast<ProductSnapshot>(result.payload));
        else UpdateGalleryUi();
        if (operation == kSourceOperation) UpdateSourceUi(true);
        if (operation == kRelocateOperation) {
            if (result.code == 0) SetRelocationEdit(snapshot.galleryDir);
            SetRelocationProgress(result.code == 0 ? 100 : 0);
            UpdateRelocationUi(result.code == 0 ? L"已切换到新壁纸库。" : L"壁纸库切换或迁移失败，请检查目标目录后重试。");
            if (result.code == 0 && !snapshot.hasSelected && !snapshot.records.empty() &&
                !ui->GetText(kGalleryWindow, L"按钮EX6").empty())
                SelectGalleryRecord(RecordDate(snapshot.records.front()));
        } else if (operation == kGalleryOperation || operation == kExportOperation) {
            UpdateRelocationUi();
        }

        const auto* title = operation == kSourceOperation ? L"壁纸来源" : operation == kRelocateOperation ? L"库迁移" :
                            operation == kExportOperation ? L"导出壁纸" : L"壁纸库管理";
        const HWND owner = DialogOwner(ui->MainWindow());
        if (result.code != 0)
            MessageBoxW(owner, result.message.c_str(), title, MB_OK | MB_ICONERROR);
        else if (operation == kExportOperation)
            MessageBoxW(owner, L"导出完成！", title, MB_OK | MB_ICONINFORMATION);
        return true;
    }

    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        constexpr UINT kUiTestSelectTab = WM_APP + 0x51;
        constexpr UINT kUiTestViewerMode = WM_APP + 0x52;
        if (options.uiTest && message == kUiTestSelectTab) {
            const int page = static_cast<int>(wParam);
            if (page >= 0 && page < 5) ui->SetSelection(kMainWindow, L"选择夹EX1", page);
            return true;
        }
        if(options.uiTest&&message==kUiTestViewerMode) {
            if(wParam<4&&ui->WindowHandle(kViewerWindow))SetViewerMode(static_cast<ReplicaUi::ImageMode>(wParam));
            return true;
        }
        if(taskbarCreated&&message==taskbarCreated){AddTray();return true;}
        if(message==kTrayMessage){
            if(lParam==WM_RBUTTONUP)ShowTrayMenu();
            else if(lParam==WM_LBUTTONUP||lParam==WM_LBUTTONDBLCLK)ui->Show(kMainWindow);
            return true;
        }
        if (message == WM_TIMER && wParam == kClockTimer) {
            if (initialized && !closing && !tickPending && nextTick && GetTickCount64() >= nextTick)
                tickPending=SubmitAction(Request(ProductAction::Tick));
            return true;
        }
        if (message != WM_STOUTWALL_TASK_DONE) return false;
        std::unique_ptr<TaskResult> result(reinterpret_cast<TaskResult*>(lParam));
        if (!result) return true;
        if(result->operation==kTickOperation) {
            tickPending=false;
            ScheduleNextTick(snapshot.intervalMs);
        }
        if (HandleAuxiliaryResult(*result)) return true;
        if (result->operation == kThemeOperation) {
            themeBusy=false;
            ui->SetText(kThemeWindow,L"按钮EX1",L"开始导出");
            ui->SetProgress(kThemeWindow,L"进度条EX1",result->code==0?100:0,false);
            UpdateThemeForm();
            const HWND owner=ui->WindowHandle(kThemeWindow);
            if(!closing&&result->payload) {
                const auto value=std::static_pointer_cast<ProductSnapshot>(result->payload);
                MessageBoxW(owner,value->status.c_str(),L"导出主题包",MB_OK|MB_ICONINFORMATION);
            } else if(!closing&&result->code!=0) {
                MessageBoxW(owner,result->message.c_str(),L"导出主题包",MB_OK|MB_ICONERROR);
                return true;
            }
        }
        if(result->operation==kStoreOperation&&!closing&&result->payload) {
            const auto value=std::static_pointer_cast<ProductSnapshot>(result->payload);
            MessageBoxW(DialogOwner(ui->MainWindow()),value->status.c_str(),L"胖哥每日壁纸",MB_OK|MB_ICONINFORMATION);
        }
        if (result->code != 0) {
            if(!closing)MessageBoxW(DialogOwner(ui->MainWindow()),result->message.c_str(),L"胖哥每日壁纸",MB_OK|MB_ICONERROR);
            if (result->operation == kInitializeOperation) BeginClose();
            else if (result->operation == kShutdownOperation) FinishClose();
            return true;
        }
        if (result->payload) {
            auto value = std::static_pointer_cast<ProductSnapshot>(result->payload);
            UpdateUi(*value);
            if(result->operation==kRestartClockOperation) ScheduleNextTick(snapshot.intervalMs);
            if(!closing&&result->operation==kClearFavoritesOperation)
                MessageBoxW(DialogOwner(ui->MainWindow()),L"已将所有原来收藏的壁纸取消收藏！",L"清空收藏",MB_OK|MB_ICONINFORMATION);
            if(!closing&&result->operation==kAutoSaveOperation&&snapshot.config.autoSave&&!options.testMode)
                MessageBoxW(DialogOwner(ui->MainWindow()),snapshot.status.c_str(),L"自动保存壁纸",MB_OK|MB_ICONINFORMATION);
            if (result->operation == kInitializeOperation) {
                initialized = true;
                ScheduleNextTick(snapshot.intervalMs);
                if (closing) BeginClose();
            } else if (result->operation == kShutdownOperation) {
                FinishClose();
            }
        }
        return true;
    }

    void ScheduleNextTick(DWORD interval) { nextTick=interval?GetTickCount64()+interval:0; }
    ULONGLONG nextTick=0;
    bool tickPending=false;

    int Run(int showCommand) {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(comResult)) throw std::runtime_error("UI COM initialization failed");
        const fs::path assetsPath = fs::path(options.runtime) / L"assets.dll";
        if (!assets.Open(assetsPath.wstring())) throw std::runtime_error("assets.dll could not be opened");
        const BYTE* placeholder = nullptr;
        DWORD placeholderSize = 0;
        if (assets.Bytes(L"IMG/nopic", &placeholder, &placeholderSize) && placeholderSize)
            noPicture.assign(placeholder, placeholder + placeholderSize);
        HICON originalIcon = assets.LoadIcon(L"IMG/icon", 32, 32);
        if (!originalIcon) throw std::runtime_error("IMG/icon could not be loaded from assets.dll");
        ui = std::make_unique<ReplicaUi>(instance, [this](const UiEvent& event) { HandleEvent(event); }, originalIcon, assets);
        if (!ui->CreateMain()) { CoUninitialize(); throw std::runtime_error("ReplicaUi::CreateMain failed"); }
        ui->SetMessageHandler([this](UINT m, WPARAM w, LPARAM l) { return HandleMessage(m, w, l); });
        ui->SetCloseGuard([this](const std::wstring& name) {
            if(name!=kThemeWindow)return true;
            if(themeBusy)return false;
            ui->Close(kThemeSelectionWindow);
            return true;
        });
        trayIcon=originalIcon;taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");AddTray();
        ui->SetItems(kMainWindow,L"组合框EX1",{L"5分钟",L"10分钟",L"20分钟",L"30分钟",L"45分钟",L"1小时",L"2小时",L"3小时",L"5小时",L"8小时"});
        ui->SetItems(kMainWindow,L"组合框EX2",kSizes);
        ui->SetEnabled(kMainWindow,L"单选框EX4",false);
        ui->SetEnabled(kMainWindow,L"按钮EX13",false);
        if (!runner.Start(ui->MainWindow())) { CoUninitialize(); throw std::runtime_error("TaskRunner::Start failed"); }
        ShowWindow(ui->MainWindow(),options.quiet&&trayInstalled?SW_HIDE:showCommand);
        UpdateWindow(ui->MainWindow());
        if (!runner.Submit(kInitializeOperation, [this]() {
            backend = std::make_unique<ProductBackend>();
            return Result(kInitializeOperation, backend->Initialize(options.runtime, options.dataRoot,
                options.configPath, options.testMode, runner.CancellationEvent(), options.proxy));
        })) {
            runner.Stop();
            ui->Quit();
        }
        SetTimer(ui->MainWindow(), kClockTimer, kTimerIntervalMs, nullptr);
        MSG message{};
        int result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){
                const HWND root=GetAncestor(message.hwnd,GA_ROOT);
                SendMessageW(root,WM_CLOSE,0,0);continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        KillTimer(ui->MainWindow(), kClockTimer);
        if (runner.CancellationEvent()) runner.Stop();
        RemoveTray();
        ui.reset();
        if (SUCCEEDED(comResult)) CoUninitialize();
        return result < 0 ? 1 : static_cast<int>(message.wParam);
    }
};

ProductUi::ProductUi(HINSTANCE instance, ProductOptions options)
    : state_(std::make_unique<State>(instance, std::move(options))) {}
ProductUi::~ProductUi() = default;
int ProductUi::Run(int showCommand) { return state_->Run(showCommand); }

} // namespace stoutwall

#if defined(STOUTWALL_PRODUCT_UI_ENTRY)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    try {
        stoutwall::ProductUi application(instance, stoutwall::ParseProductOptions(instance));
        return application.Run(showCommand);
    } catch (const std::exception& error) {
        const char* message = error.what();
        const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
        std::wstring detail;
        if (length > 0) {
            detail.resize(static_cast<size_t>(length));
            if (MultiByteToWideChar(CP_UTF8, 0, message, -1, detail.data(), length) == length)
                detail.pop_back();
            else
                detail.clear();
        }
        const std::wstring text = L"StoutWall 主程序启动失败。" +
            (detail.empty() ? std::wstring() : L"\n\n" + detail);
        MessageBoxW(nullptr, text.c_str(), L"StoutWall", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, L"StoutWall 主程序启动失败：未知异常。", L"StoutWall", MB_OK | MB_ICONERROR);
        return 2;
    }
    return 2;
}
#endif
