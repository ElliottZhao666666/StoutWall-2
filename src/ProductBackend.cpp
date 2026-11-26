#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ProductBackend.hpp"
#include "GalleryImage.hpp"
#include "SequenceRandom.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <list>
#include <random>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace stoutwall {
namespace fs = std::filesystem;
namespace {

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) return "UTF-8 message conversion failed";
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
        return "UTF-8 message conversion failed";
    return result;
}

std::wstring ErrorText(const std::exception& error) {
    const int length = static_cast<int>(strlen(error.what()));
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, error.what(), length, nullptr, 0);
    if (size <= 0) return L"Unknown error";
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, error.what(), length, result.data(), size);
    return result;
}

[[noreturn]] void Fail(const std::wstring& message) {
    throw std::runtime_error(ToUtf8(message));
}

[[noreturn]] void FailCode(const wchar_t* operation, DWORD code) {
    Fail(std::wstring(operation) + L" (Win32 error " + std::to_wstring(code) + L")");
}

void CheckPlatform(DWORD result, const wchar_t* operation) {
    if (result != ERROR_SUCCESS) FailCode(operation, result);
}

void CheckLibrary(int32_t result, const wchar_t* operation) {
    if (result >= 0) return;
    int32_t needed = Library_LastErrorW(nullptr, 0);
    std::wstring detail;
    if (needed > 1 && needed < 65536) {
        std::vector<wchar_t> buffer(static_cast<size_t>(needed));
        Library_LastErrorW(buffer.data(), needed);
        detail.assign(buffer.data());
    }
    Fail(std::wstring(operation) + L" (gallery.dll " + std::to_wstring(result) + L")" +
         (detail.empty() ? L"" : L": " + detail));
}

void CheckNetwork(DWORD result, const wchar_t* operation) {
    if (result != ERROR_SUCCESS) FailCode(operation, result);
}

template<size_t N> void Put(wchar_t (&target)[N], const std::wstring& value, const wchar_t* field) {
    if (value.size() >= N) Fail(std::wstring(field) + L" exceeds the API field capacity");
    std::copy(value.begin(), value.end(), target);
    target[value.size()] = L'\0';
}

std::wstring Get(const wchar_t* value) { return value ? std::wstring(value) : std::wstring(); }
std::wstring DateOf(const SWL_Record& record) { return Get(record.date); }
bool IsWithin(const fs::path& root, const fs::path& candidate) {
    const std::wstring rootText = fs::absolute(root).lexically_normal().wstring();
    const std::wstring candidateText = fs::absolute(candidate).lexically_normal().wstring();
    if (_wcsicmp(rootText.c_str(), candidateText.c_str()) == 0) return true;
    std::wstring prefix = rootText;
    if (!prefix.empty() && prefix.back() != L'\\') prefix.push_back(L'\\');
    return candidateText.size() > prefix.size() &&
           _wcsnicmp(candidateText.c_str(), prefix.c_str(), prefix.size()) == 0;
}
DWORD LibrarySizeIndex(DWORD uiSizeIndex) {
    if (uiSizeIndex > 4) Fail(L"UI image size index must be 0..4");
    return uiSizeIndex + 1; // LibraryApi is 1-based; NetworkApi remains 0-based.
}

std::wstring UniqueToken() {
    return std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(GetTickCount64()) + L"-" +
           std::to_wstring(std::random_device{}());
}

void EnsureDirectory(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error || !fs::is_directory(path)) Fail(L"Unable to create directory: " + path.wstring());
}

std::vector<unsigned char> ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) Fail(L"Unable to read image: " + path.wstring());
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length <= 0 || static_cast<unsigned long long>(length) >
        static_cast<unsigned long long>((std::numeric_limits<int32_t>::max)()))
        Fail(L"Image file has an invalid size: " + path.wstring());
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        Fail(L"Unable to read complete image: " + path.wstring());
    return bytes;
}

void WriteFile(const fs::path& path, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) Fail(L"Refusing to write an empty image");
    EnsureDirectory(path.parent_path());
    fs::path temporary = path;
    temporary += L"." + UniqueToken() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                                      static_cast<std::streamsize>(bytes.size()))) {
            std::error_code ignored; fs::remove(temporary, ignored);
            Fail(L"Unable to write image: " + path.wstring());
        }
        output.flush();
        if (!output) {
            output.close(); std::error_code ignored; fs::remove(temporary, ignored);
            Fail(L"Unable to flush image: " + path.wstring());
        }
    }
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const DWORD error = GetLastError();
        // A reader or file scanner can briefly hold the image during rapid
        // changes. Keep the previous file intact and bound the retry to 375 ms.
        if (attempt < 5 && (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
                            error == ERROR_LOCK_VIOLATION)) {
            Sleep(25 * (attempt + 1));
            continue;
        }
        std::error_code ignored; fs::remove(temporary, ignored);
        FailCode(L"Unable to finalize image file", error);
    }
}

SWL_Record NetworkRecord(const SWN_Metadata& metadata) {
    SWL_Record record{};
    Put(record.date, Get(metadata.enddate), L"enddate");
    Put(record.urlbase, Get(metadata.urlbase), L"urlbase");
    Put(record.title, Get(metadata.title), L"title");
    Put(record.description, Get(metadata.description), L"description");
    Put(record.author, Get(metadata.author), L"author");
    Put(record.link, Get(metadata.copyrightlink), L"copyrightlink");
    return record;
}

class TempDirectory final {
public:
    explicit TempDirectory(const fs::path& parent) : path_(parent / (L"product-" + UniqueToken())) {
        EnsureDirectory(path_);
    }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    const fs::path& path() const noexcept { return path_; }
private:
    fs::path path_;
};

std::wstring TrimIni(std::wstring value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring ReadConfigText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
        const size_t count = (bytes.size() - 2) / sizeof(wchar_t);
        std::wstring result(count, L'\0');
        if (count) memcpy(result.data(), bytes.data() + 2, count * sizeof(wchar_t));
        return result;
    }
    size_t offset = bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf ? 3 : 0;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
        static_cast<int>(bytes.size() - offset), nullptr, 0);
    UINT codePage = CP_UTF8; DWORD flags = MB_ERR_INVALID_CHARS;
    if (!count && bytes.size() > offset) {
        codePage = CP_ACP; flags = 0;
        count = MultiByteToWideChar(codePage, flags, bytes.data() + offset,
            static_cast<int>(bytes.size() - offset), nullptr, 0);
    }
    if (!count) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(codePage, flags, bytes.data() + offset,
        static_cast<int>(bytes.size() - offset), result.data(), count) != count) return {};
    return result;
}

std::vector<std::wstring> ConfigLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t offset = 0;
    while (offset < text.size()) {
        const size_t end = text.find_first_of(L"\r\n", offset);
        lines.push_back(text.substr(offset, end == std::wstring::npos ? text.size() - offset : end - offset));
        if (end == std::wstring::npos) break;
        offset = end + 1;
        if (text[end] == L'\r' && offset < text.size() && text[offset] == L'\n') ++offset;
    }
    return lines;
}

bool ReadAutoRunPreference(const fs::path& path) {
    const auto lines = ConfigLines(ReadConfigText(path));
    bool inGeneral = false;
    for (const auto& raw : lines) {
        const std::wstring line = TrimIni(raw);
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            std::wstring section = TrimIni(line.substr(1, line.size() - 2));
            std::transform(section.begin(), section.end(), section.begin(), towlower);
            inGeneral = section == L"general";
            continue;
        }
        if (!inGeneral) continue;
        const size_t equal = line.find(L'=');
        if (equal == std::wstring::npos) continue;
        std::wstring key = TrimIni(line.substr(0, equal));
        std::transform(key.begin(), key.end(), key.begin(), towlower);
        if (key == L"autorun") return TrimIni(line.substr(equal + 1)) == L"1";
    }
    return false;
}

void WriteAutoRunPreference(const fs::path& path, bool enabled) {
    std::vector<std::wstring> lines = ConfigLines(ReadConfigText(path));
    size_t generalBegin = lines.size(), generalEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring line = TrimIni(lines[i]);
        if (line.size() < 2 || line.front() != L'[' || line.back() != L']') continue;
        std::wstring section = TrimIni(line.substr(1, line.size() - 2));
        std::transform(section.begin(), section.end(), section.begin(), towlower);
        if (section == L"general" && generalBegin == lines.size()) generalBegin = i + 1;
        else if (generalBegin != lines.size() && generalEnd == lines.size()) { generalEnd = i; break; }
    }
    if (generalBegin == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.emplace_back(L"[General]");
        generalBegin = lines.size(); generalEnd = lines.size();
    } else if (generalEnd == lines.size()) generalEnd = lines.size();
    bool replaced = false;
    for (size_t i = generalBegin; i < generalEnd; ++i) {
        const size_t equal = lines[i].find(L'=');
        if (equal == std::wstring::npos) continue;
        std::wstring key = TrimIni(lines[i].substr(0, equal));
        std::transform(key.begin(), key.end(), key.begin(), towlower);
        if (key == L"autorun") { lines[i] = L"AutoRun=" + std::wstring(enabled ? L"1" : L"0"); replaced = true; break; }
    }
    if (!replaced) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(generalEnd),
                                L"AutoRun=" + std::wstring(enabled ? L"1" : L"0"));
    std::wstring text;
    for (const auto& line : lines) text += line + L"\r\n";
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) Fail(L"Unable to encode AutoRun preference");
    std::vector<unsigned char> bytes{0xef, 0xbb, 0xbf};
    const size_t previousSize = bytes.size(); bytes.resize(previousSize + static_cast<size_t>(byteCount));
    if (byteCount && WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        reinterpret_cast<char*>(bytes.data() + previousSize), byteCount, nullptr, nullptr) != byteCount)
        Fail(L"Unable to encode AutoRun preference");
    WriteFile(path, bytes);
}

} // namespace

struct ProductBackend::Impl {
    DWORD ownerThread = 0;
    bool initialized = false;
    bool libraryOpen = false;
    bool testMode = false;
    bool autoRunPreference = false;
    bool dailyFetchFailed = false;
    bool browseFavoritesOnly = false;
    HANDLE cancel = nullptr;
    std::wstring proxy;
    fs::path runtime, dataRoot, configPath, galleryDir, currentImagePath;
    SWP_Config config{};
    std::vector<SWL_Record> allRecords;
    std::vector<SWL_Record> sequence;
    std::vector<unsigned char> currentImage, selectedImage;
    struct CachedImage {
        std::wstring directory, date;
        DWORD size;
        std::vector<unsigned char> bytes;
    };
    mutable std::list<CachedImage> imageCache;
    mutable size_t imageCacheBytes = 0;
    SWL_Record current{}, selected{};
    bool hasCurrent = false, hasSelected = false;
    std::wstring selectedDate;
    std::wstring status;
    size_t sequencePosition = 0;
    std::mt19937 random{std::random_device{}()};

    void CheckThread(bool allowUninitialized = false) const {
        if (!ownerThread || ownerThread != GetCurrentThreadId())
            Fail(L"ProductBackend methods must run on its single worker thread");
        if (!allowUninitialized && !initialized) Fail(L"ProductBackend has not been initialized");
    }

    SWP_Options PlatformOptions() const {
        SWP_Options options{}; options.cbSize = sizeof(options); options.cancelEvent = cancel;
        return options;
    }

    SWN_Options NetworkOptions() const {
        SWN_Options options{}; options.cbSize = sizeof(options); options.cancelEvent = cancel;
        if (proxy.size() >= _countof(options.proxy)) Fail(L"Proxy exceeds the NetworkApi capacity");
        Put(options.proxy, proxy, L"proxy");
        return options;
    }

    void SaveConfig() {
        Put(config.galleryDir, galleryDir.wstring(), L"galleryDir");
        CheckPlatform(SWP_SaveConfig(configPath.c_str(), &config), L"Saving StoutWall.cfg");
    }

    void LoadRecords() {
        const int32_t count = Library_Count(0);
        CheckLibrary(count, L"Counting gallery records");
        allRecords.clear(); allRecords.reserve(static_cast<size_t>(count));
        for (int32_t ordinal = 1; ordinal <= count; ++ordinal) {
            SWL_Record record{};
            CheckLibrary(Library_GetRecord(ordinal, 0, &record), L"Reading gallery record");
            allRecords.push_back(record);
        }
    }

    std::vector<SWL_Record> FilteredRecords() const {
        std::vector<SWL_Record> result;
        result.reserve(allRecords.size());
        for (const auto& record : allRecords)
            if (!browseFavoritesOnly || record.favorite) result.push_back(record);
        return result;
    }

    SWL_Record FindRecord(const std::wstring& date, bool& found) const {
        for (const auto& record : allRecords) {
            if (DateOf(record) == date) { found = true; return record; }
        }
        found = false; return {};
    }

    std::vector<unsigned char> GetImage(const std::wstring& date, DWORD librarySize) const {
        if (librarySize < 1 || librarySize > SWN_SIZE_COUNT) Fail(L"Invalid gallery image size index");
        const auto directory = galleryDir.wstring();
        for (auto it = imageCache.begin(); it != imageCache.end(); ++it) {
            if (it->directory == directory && it->date == date && it->size == librarySize) {
                imageCache.splice(imageCache.begin(), imageCache, it);
                return imageCache.front().bytes;
            }
        }
        std::vector<unsigned char> bytes;
        const int32_t actual = ReadGalleryImage(date.c_str(), static_cast<int32_t>(librarySize), bytes);
        CheckLibrary(actual, L"Reading gallery image");
        if (!actual) Fail(L"Gallery returned an empty image");
        constexpr size_t cacheLimit = 16 * 1024 * 1024;
        if (bytes.size() <= cacheLimit) {
            while (!imageCache.empty() && (imageCache.size() >= 6 || imageCacheBytes + bytes.size() > cacheLimit)) {
                imageCacheBytes -= imageCache.back().bytes.size();
                imageCache.pop_back();
            }
            imageCache.push_front({directory, date, librarySize, bytes});
            imageCacheBytes += bytes.size();
        }
        return bytes;
    }

    void ClearImageCache() { imageCache.clear(); imageCacheBytes = 0; }

    std::vector<unsigned char> PreviewImage(const SWL_Record& record,
                                           const std::vector<unsigned char>& original) const {
        if (config.currentWall.size != SWN_UHD || !libraryOpen) return original;
        bool found = false; FindRecord(DateOf(record), found);
        if (found) {
            try { return GetImage(DateOf(record), LibrarySizeIndex(SWN_1920x1080)); }
            catch (const std::exception&) {
                if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                // A missing preview must not prevent using an intact UHD image.
                // The renderer also bounds decoded previews to 1920 x 1080.
            }
        }
        return original;
    }

    std::vector<SWL_Record> BuildSequence(DWORD mode, const std::wstring& pinnedDate = {}) {
        std::vector<SWL_Record> result;
        if (mode == SWP_DAILY) return result;
        for (const auto& record : allRecords)
            if (mode != SWP_FAVORITES || record.favorite) result.push_back(record);
        if (config.changeRandom && mode == SWP_GALLERY) {
            size_t pinnedPosition = static_cast<size_t>(-1);
            size_t pinnedIndex = static_cast<size_t>(-1);
            if (!pinnedDate.empty()) {
                const auto found = std::find_if(result.begin(), result.end(), [&](const SWL_Record& record) {
                    return DateOf(record) == pinnedDate;
                });
                if (found != result.end()) {
                    pinnedPosition = ChooseRandomSequenceIndex(result.size(), random);
                    pinnedIndex = static_cast<size_t>(found - result.begin());
                }
            }
            const auto indices = BuildRandomSequenceIndices(result.size(), result.size(), random,
                                                            pinnedPosition, pinnedIndex);
            std::vector<SWL_Record> sampled;
            sampled.reserve(indices.size());
            for (const size_t index : indices) sampled.push_back(result[index]);
            result = std::move(sampled);
        }
        return result;
    }

    size_t SequenceStart(const std::vector<SWL_Record>& records, const std::wstring& keep) {
        const auto found = std::find_if(records.begin(), records.end(), [&](const SWL_Record& r) {
            return DateOf(r) == keep;
        });
        if (found != records.end()) return static_cast<size_t>(found - records.begin());
        return config.renderPoint && config.renderPoint <= records.size() ? config.renderPoint - 1 : 0;
    }

    void RebuildSequence() {
        const std::wstring keep = hasCurrent ? DateOf(current) : std::wstring();
        DWORD rebuiltMode = config.mode;
        auto rebuilt = BuildSequence(rebuiltMode, config.changeRandom && rebuiltMode == SWP_GALLERY ? keep : std::wstring());
        if (rebuiltMode == SWP_FAVORITES && rebuilt.empty() && !allRecords.empty()) {
            rebuiltMode = SWP_GALLERY;
            rebuilt = BuildSequence(rebuiltMode, config.changeRandom ? keep : std::wstring());
        }
        if (rebuiltMode == SWP_DAILY || rebuilt.empty()) {
            sequence = std::move(rebuilt);
            sequencePosition = 0;
            if (rebuiltMode != SWP_DAILY) config.renderPoint = 1;
            return;
        }
        const auto found = std::find_if(rebuilt.begin(), rebuilt.end(), [&](const SWL_Record& record) {
            return DateOf(record) == keep;
        });
        if (found != rebuilt.end()) sequencePosition = static_cast<size_t>(found - rebuilt.begin());
        else if (config.changeRandom && rebuiltMode == SWP_GALLERY)
            sequencePosition = ChooseRandomSequenceIndex(rebuilt.size(), random);
        else sequencePosition = SequenceStart(rebuilt, {});
        if (hasCurrent && !keep.empty() && found == rebuilt.end())
            ApplySequenceRecord(rebuilt[sequencePosition], sequencePosition, rebuiltMode);
        else {
            config.mode = rebuiltMode;
            config.renderPoint = static_cast<DWORD>(sequencePosition + 1);
        }
        sequence = std::move(rebuilt);
    }

    const wchar_t* EmptyModeMessage(DWORD mode) const {
        return mode == SWP_FAVORITES ? L"There are no favorite wallpapers to display"
                                     : L"The gallery contains no wallpapers";
    }

    void UpdateInterval() {
        const DWORD count = static_cast<DWORD>(sequence.size());
        DWORD interval = 0;
        CheckPlatform(SWP_GetChangeInterval(&config, count, dailyFetchFailed ? TRUE : FALSE, &interval),
                      L"Calculating wallpaper interval");
        if (config.paused) interval = 0;
        cachedInterval = interval;
    }
    DWORD cachedInterval = 0;

    void SelectBrowse(const std::wstring& date) {
        bool found = false;
        SWL_Record record = FindRecord(date, found);
        if (!found) Fail(L"The requested gallery record does not exist");
        selected = record; selectedDate = date; hasSelected = true;
        RefreshSelectedImage();
    }

    void RefreshSelectedImage() {
        if (!hasSelected) { selectedImage.clear(); return; }
        bool found = false;
        selected = FindRecord(selectedDate, found);
        if (!found) { hasSelected = false; selectedDate.clear(); selectedImage.clear(); return; }
        const DWORD size = config.currentWall.size;
        if (size == SWN_UHD) {
            try {
                selectedImage = GetImage(selectedDate, LibrarySizeIndex(SWN_1920x1080));
                return;
            } catch (const std::exception&) {
                if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
            }
        }
        selectedImage = GetImage(selectedDate, LibrarySizeIndex(size));
    }

    void ApplyCurrent(const SWL_Record& record, std::vector<unsigned char> bytes,
                      SWP_Config nextConfig, bool fromLibrary, bool doApply = true) {
        if (bytes.empty()) Fail(L"No image is available for the current wallpaper");
        Put(nextConfig.currentWall.enddate, Get(record.date), L"current enddate");
        Put(nextConfig.currentWall.title, Get(record.title), L"current title");
        Put(nextConfig.currentWall.description, Get(record.description), L"current description");
        Put(nextConfig.currentWall.author, Get(record.author), L"current author");
        Put(nextConfig.currentWall.copyrightlink, Get(record.link), L"current copyright link");
        nextConfig.currentWall.favorite = record.favorite ? 1 : 0;
        if (fromLibrary) Put(nextConfig.galleryCurrent, Get(record.date), L"gallery current date");
        WriteFile(currentImagePath, bytes);
        if (doApply && !testMode) {
            const DWORD result = SWP_SetWallpaper(currentImagePath.c_str());
            if (result != ERROR_SUCCESS) {
                if (!currentImage.empty()) WriteFile(currentImagePath, currentImage);
                else {
                    std::error_code ignored;
                    fs::remove(currentImagePath, ignored);
                }
                FailCode(L"Applying wallpaper", result);
            }
        }
        config = nextConfig;
        current = record; hasCurrent = true;
        currentImage = std::move(bytes);
    }

    void ApplySequenceRecord(const SWL_Record& record, size_t position, DWORD mode) {
        SWP_Config nextConfig = config;
        nextConfig.mode = mode;
        nextConfig.renderPoint = static_cast<DWORD>(position + 1);
        ApplyCurrent(record, GetImage(DateOf(record), LibrarySizeIndex(config.currentWall.size)), nextConfig, true);
        sequencePosition = position;
    }

    bool SelectGalleryMode(DWORD mode, const std::wstring& date = {}) {
        auto next = !date.empty() && config.mode == mode && !sequence.empty()
            ? sequence : BuildSequence(mode);
        if (next.empty()) return false;
        size_t position = 0;
        if (!date.empty()) {
            const auto found = std::find_if(next.begin(), next.end(), [&](const SWL_Record& record) {
                return DateOf(record) == date;
            });
            position = found == next.end() ? SequenceStart(next, date) : static_cast<size_t>(found - next.begin());
        } else if (config.changeRandom && mode == SWP_GALLERY) {
            position = ChooseRandomSequenceIndex(next.size(), random);
        } else {
            position = SequenceStart(next, hasCurrent && config.mode == mode ? DateOf(current) : std::wstring());
        }
        if (!date.empty()) {
            bool found = false;
            const SWL_Record selectedRecord = FindRecord(date, found);
            if (!found) Fail(L"Selected record is not available in the gallery");
            if (config.mode == mode && !sequence.empty() && sequencePosition < next.size())
                position = sequencePosition;
            next[position] = selectedRecord;
        }
        ApplySequenceRecord(next[position], position, mode);
        sequence = std::move(next);
        return true;
    }

    SWN_Metadata FetchMetadata() {
        SWN_Metadata metadata{}; metadata.cbSize = sizeof(metadata);
        DWORD httpStatus = 0;
        const SWN_Options options = NetworkOptions();
        CheckNetwork(SWN_FetchLatest(config.source, &options, &metadata, &httpStatus),
                     L"Fetching latest wallpaper metadata");
        return metadata;
    }

    fs::path Download(const SWN_Metadata& metadata, DWORD sizeIndex, const fs::path& directory) {
        if (sizeIndex >= SWN_SIZE_COUNT) Fail(L"Invalid network image size index");
        EnsureDirectory(directory);
        fs::path destination = directory / (Get(metadata.enddate) + L"-" +
            std::to_wstring(sizeIndex) + L"-" + UniqueToken() + L".jpg");
        SWN_ImageInfo info{}; info.cbSize = sizeof(info); DWORD httpStatus = 0;
        const SWN_Options options = NetworkOptions();
        CheckNetwork(SWN_DownloadImage(&metadata, sizeIndex, destination.c_str(),
            &options, &info, &httpStatus), L"Downloading wallpaper image");
        return destination;
    }

    std::vector<unsigned char> DownloadBytes(const SWN_Metadata& metadata, DWORD networkSizeIndex) {
        TempDirectory temporary(dataRoot / L"work");
        const fs::path image = Download(metadata, networkSizeIndex, temporary.path());
        return ReadFile(image);
    }

    bool ImportLatest(const SWN_Metadata& metadata) {
        bool exists = false; FindRecord(Get(metadata.enddate), exists);
        if (exists) return false;
        TempDirectory temporary(dataRoot / L"work");
        std::array<fs::path, SWN_SIZE_COUNT> paths;
        std::array<const wchar_t*, SWN_SIZE_COUNT> imagePointers{};
        for (DWORD size = 0; size < SWN_SIZE_COUNT; ++size) {
            paths[size] = Download(metadata, size, temporary.path());
            imagePointers[size] = paths[size].c_str();
        }
        SWL_Record record = NetworkRecord(metadata);
        CheckLibrary(Library_ImportW(&record, imagePointers.data()), L"Importing five gallery sizes");
        ClearImageCache();
        LoadRecords();
        return true;
    }

    ProductSnapshot MakeSnapshot(const std::wstring& message = {}) {
        ProductSnapshot snapshot{};
        snapshot.config = config; snapshot.current = current; snapshot.selected = selected;
        snapshot.records = FilteredRecords(); snapshot.image = currentImage;
        snapshot.selectedPreviewImage = selectedImage; snapshot.status = message.empty() ? status : message;
        if (hasCurrent) snapshot.previewImage = PreviewImage(current, currentImage);
        snapshot.galleryDir = galleryDir.wstring();
        snapshot.total = static_cast<int>((std::min)(allRecords.size(),
            static_cast<size_t>((std::numeric_limits<int>::max)())));
        snapshot.favorites = static_cast<int>(std::count_if(allRecords.begin(), allRecords.end(),
            [](const SWL_Record& r) { return r.favorite != 0; }));
        snapshot.intervalMs = cachedInterval; snapshot.hasCurrent = hasCurrent;
        snapshot.autoRunPreference = autoRunPreference;
        snapshot.hasSelected = hasSelected;
        return snapshot;
    }

    void SetCurrentFromDiskConfig() {
        if (!config.currentWall.enddate[0]) return;
        bool inLibrary = false;
        SWL_Record record = FindRecord(Get(config.currentWall.enddate), inLibrary);
        if (inLibrary) {
            auto bytes = GetImage(DateOf(record), LibrarySizeIndex(config.currentWall.size));
            ApplyCurrent(record, std::move(bytes), config, true, false);
            return;
        }
        std::error_code error;
        if (!fs::is_regular_file(currentImagePath, error) || error) return;
        SWN_ImageInfo imageInfo{}; imageInfo.cbSize = sizeof(imageInfo);
        if (SWN_ValidateImage(currentImagePath.c_str(), config.currentWall.size, &imageInfo) != ERROR_SUCCESS)
            return; // A cached daily image is accepted only at the configured 0..4 network size.
        current = {};
        Put(current.date, Get(config.currentWall.enddate), L"current enddate");
        Put(current.title, Get(config.currentWall.title), L"current title");
        Put(current.description, Get(config.currentWall.description), L"current description");
        Put(current.author, Get(config.currentWall.author), L"current author");
        Put(current.link, Get(config.currentWall.copyrightlink), L"current link");
        current.favorite = static_cast<int32_t>(config.currentWall.favorite);
        currentImage = ReadFile(currentImagePath); hasCurrent = true;
    }

    void ApplyDaily(const SWN_Metadata& metadata, SWP_Config nextConfig) {
        const std::wstring date = Get(metadata.enddate);
        bool inLibrary = false;
        const SWL_Record record = FindRecord(date, inLibrary);
        const DWORD size = nextConfig.currentWall.size;
        if (inLibrary) ApplyCurrent(record, GetImage(date, LibrarySizeIndex(size)), nextConfig, true);
        else ApplyCurrent(NetworkRecord(metadata), DownloadBytes(metadata, size), nextConfig, false);
    }

    void Refresh(std::wstring& message) {
        const SWN_Metadata metadata = FetchMetadata();
        if (config.autoSave) {
            const bool added = ImportLatest(metadata);
            if (added) RebuildSequence();
            message = added ? L"Latest wallpaper added to the gallery" : L"Latest wallpaper is already in the gallery";
        } else message = L"Latest metadata checked; current gallery wallpaper was left unchanged";
        if (config.mode == SWP_DAILY) {
            ApplyDaily(metadata, config);
            dailyFetchFailed = false;
            message += L"; daily wallpaper updated";
        }
        UpdateInterval();
        SaveConfig();
    }

    bool Step(int direction) {
        if (config.mode == SWP_DAILY) Fail(L"Previous/Next are available in gallery and favorites modes");
        if (sequence.size() < 2) return false;
        const bool atCurrent = hasCurrent && DateOf(sequence[sequencePosition]) == DateOf(current);
        if (atCurrent && direction > 0 && sequencePosition + 1 == sequence.size() && config.changeRandom) {
            auto next = BuildSequence(config.mode);
            ApplySequenceRecord(next.front(), 0, config.mode);
            sequence = std::move(next);
        } else {
            size_t next = sequencePosition;
            if (atCurrent && direction > 0) next = (sequencePosition + 1) % sequence.size();
            else if (direction < 0) next = sequencePosition == 0 ? sequence.size() - 1 : sequencePosition - 1;
            ApplySequenceRecord(sequence[next], next, config.mode);
        }
        SaveConfig();
        return true;
    }

    std::wstring Tick() {
        if (config.paused) return L"Automatic changes are paused";
        if (config.mode == SWP_DAILY) {
            if (testMode) return L"Test mode: automatic daily refresh skipped";
            try {
                const SWN_Metadata metadata = FetchMetadata();
                if (config.autoSave) ImportLatest(metadata);
                const bool changed = !hasCurrent || Get(metadata.enddate) != DateOf(current);
                if (changed) {
                    ApplyDaily(metadata, config);
                    SaveConfig();
                }
                dailyFetchFailed = false;
                return changed ? L"New daily wallpaper applied" : L"Daily wallpaper unchanged";
            } catch (const std::exception& error) {
                if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                dailyFetchFailed = true;
                return L"Daily wallpaper update failed; retry scheduled: " + ErrorText(error);
            }
        }

        std::wstring saveStatus;
        if (config.autoSave && !testMode) {
            try {
                if (ImportLatest(FetchMetadata())) {
                    RebuildSequence();
                    SaveConfig();
                    saveStatus = L"; latest wallpaper added to the gallery";
                }
            } catch (const std::exception& error) {
                if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                saveStatus = L"; automatic gallery refresh failed: " + ErrorText(error);
            }
        }
        return (Step(1) ? L"Automatic gallery advance completed" : L"Rotation needs at least two wallpapers") + saveStatus;
    }

    void UpdateBrowseSelection() {
        const auto filtered = FilteredRecords();
        if (filtered.empty()) { hasSelected = false; selectedDate.clear(); selectedImage.clear(); return; }
        bool exists = false; FindRecord(selectedDate, exists);
        if (!exists || (browseFavoritesOnly && !FindRecord(selectedDate, exists).favorite)) {
            SelectBrowse(DateOf(filtered.front()));
        } else RefreshSelectedImage();
    }

    ProductSnapshot Initialize(const std::wstring& runtimeText, const std::wstring& dataRootText,
                              const std::wstring& configText, bool testing, HANDLE cancelEvent,
                              const std::wstring& proxyText) {
        if (ownerThread && ownerThread != GetCurrentThreadId())
            Fail(L"ProductBackend must stay on its original worker thread");
        if (initialized || libraryOpen) Fail(L"ProductBackend is already initialized");
        ownerThread = GetCurrentThreadId(); testMode = testing; cancel = cancelEvent; proxy = proxyText;
        if (runtimeText.empty()) Fail(L"Runtime directory is required");
        if (configText.empty()) Fail(L"A configPath is required; runtime/StoutWall.cfg is never discovered");
        runtime = fs::absolute(fs::path(runtimeText)).lexically_normal();
        if (dataRootText.empty()) Fail(L"A dataRoot is required; runtime directory fallback is disabled");
        dataRoot = fs::absolute(fs::path(dataRootText)).lexically_normal();
        configPath = fs::absolute(fs::path(configText)).lexically_normal();
        if (!IsWithin(dataRoot, configPath))
            Fail(L"configPath must be inside dataRoot");
        currentImagePath = dataRoot / L"Current_Wall.jpg";
        galleryDir = dataRoot / L"Gallery";
        EnsureDirectory(runtime); EnsureDirectory(dataRoot);
        EnsureDirectory(configPath.parent_path());
        EnsureDirectory(dataRoot / L"work");
        config = {}; config.cbSize = sizeof(config);
        CheckPlatform(SWP_LoadConfig(configPath.c_str(), &config), L"Loading StoutWall.cfg");
        autoRunPreference = ReadAutoRunPreference(configPath);
        if (config.galleryDir[0]) {
            const auto configuredGallery = fs::path(config.galleryDir).lexically_normal();
            if (configuredGallery.is_absolute() && (!testMode || IsWithin(dataRoot, configuredGallery)))
                galleryDir = configuredGallery;
        }
        Put(config.galleryDir, galleryDir.wstring(), L"galleryDir");
        if (config.mode < SWP_DAILY || config.mode > SWP_FAVORITES) config.mode = SWP_DAILY;
        if (config.changeFreq > 9) config.changeFreq = 3;
        if (config.source < SWN_GLOBAL || config.source > SWN_CUSTOM) config.source = SWN_CN;
        if (config.currentWall.size > 4) config.currentWall.size = 0;
        Put(config.galleryDir, galleryDir.wstring(), L"galleryDir");
        CheckLibrary(Library_OpenW(runtime.c_str(), galleryDir.c_str(), 1), L"Opening gallery and releasing GalleryBase");
        libraryOpen = true;
        LoadRecords(); browseFavoritesOnly = false;
        SetCurrentFromDiskConfig();
        std::wstring message = L"Gallery and configuration loaded";
        if (config.mode == SWP_DAILY) {
            if (!testMode) {
                try { Refresh(message); }
                catch (const std::exception& e) {
                    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                    dailyFetchFailed = true; UpdateInterval();
                    if (hasCurrent && !currentImage.empty())
                        CheckPlatform(SWP_SetWallpaper(currentImagePath.c_str()),
                                      L"Applying cached wallpaper after refresh failure");
                    message = L"Daily wallpaper update failed; retry scheduled: " + ErrorText(e);
                }
            } else message = L"Test mode: automatic network refresh and wallpaper apply skipped";
        } else {
            const bool loaded = SelectGalleryMode(config.mode);
            if (loaded) {
                message = testMode ? L"Test mode: initial gallery wallpaper loaded; apply skipped"
                                   : L"Gallery wallpaper loaded";
            } else message = EmptyModeMessage(config.mode);
            if (!testMode && config.autoSave) {
                bool added = false;
                try {
                    added = ImportLatest(FetchMetadata());
                } catch (const std::exception& e) {
                    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                    message += L"; automatic gallery refresh failed: " + ErrorText(e);
                }
                if (added) {
                    if (loaded) RebuildSequence();
                    else SelectGalleryMode(config.mode);
                    message += L"; latest wallpaper added to the gallery";
                }
            }
        }
        if (!hasSelected && !allRecords.empty()) {
            const std::wstring initialSelection = hasCurrent ? Get(current.date) : DateOf(allRecords.front());
            bool exists = false; FindRecord(initialSelection, exists);
            SelectBrowse(exists ? initialSelection : DateOf(allRecords.front()));
        }
        RefreshSelectedImage(); UpdateInterval(); SaveConfig();
        initialized = true;
        return MakeSnapshot(message);
    }

    ProductSnapshot Execute(const ProductRequest& request) {
        CheckThread(request.action == ProductAction::Shutdown);
        if (request.action == ProductAction::Shutdown) {
            if (libraryOpen) {
                CheckLibrary(Library_Close(), L"Closing gallery");
                libraryOpen = false;
            }
            initialized = false;
            return MakeSnapshot(L"Gallery closed");
        }
        std::wstring message;
        switch (request.action) {
        case ProductAction::Initialize:
            Fail(L"Initialize is called through ProductBackend::Initialize");
        case ProductAction::Refresh:
            Refresh(message); break;
        case ProductAction::Previous:
            message = Step(-1) ? L"Previous wallpaper selected" : L"Rotation needs at least two wallpapers"; break;
        case ProductAction::Next:
            message = Step(1) ? L"Next wallpaper selected" : L"Rotation needs at least two wallpapers"; break;
        case ProductAction::Favorite: {
            if (!hasCurrent) Fail(L"There is no current wallpaper to favorite");
            bool found = false; SWL_Record record = FindRecord(Get(current.date), found);
            if (!found) {
                const SWN_Metadata latest = FetchMetadata();
                if (Get(latest.enddate) != Get(current.date))
                    Fail(L"Current wallpaper is not in the gallery and is no longer the latest downloadable image");
                ImportLatest(latest);
                record = FindRecord(Get(current.date), found);
                if (!found) Fail(L"Current wallpaper could not be added to the gallery");
            }
            CheckLibrary(Library_SetFavorite(record.date, record.favorite ? 0 : 1), L"Changing current favorite");
            LoadRecords(); record = FindRecord(Get(current.date), found);
            current.favorite = record.favorite; config.currentWall.favorite = static_cast<DWORD>(record.favorite);
            if (hasSelected && selectedDate == Get(record.date)) RefreshSelectedImage();
            RebuildSequence(); UpdateBrowseSelection(); UpdateInterval(); SaveConfig();
            message = record.favorite ? L"Wallpaper added to favorites" : L"Wallpaper removed from favorites";
            break;
        }
        case ProductAction::ChangeMode: {
            if (request.value < SWP_DAILY || request.value > SWP_FAVORITES) Fail(L"Mode must be 1, 2, or 3");
            const DWORD mode = static_cast<DWORD>(request.value);
            if (mode == SWP_DAILY) {
                SWP_Config nextConfig = config;
                nextConfig.mode = mode;
                if (!testMode) ApplyDaily(FetchMetadata(), nextConfig);
                else config = nextConfig;
                RebuildSequence();
                dailyFetchFailed = false;
            } else if (!SelectGalleryMode(mode)) {
                message = EmptyModeMessage(mode);
                break;
            }
            SaveConfig(); message = L"Wallpaper mode changed"; break;
        }
        case ProductAction::ChangeSize: {
            if (request.value < 0 || request.value > 4) Fail(L"Size index must be 0..4");
            if (!hasCurrent) Fail(L"There is no current wallpaper");
            SWP_Config nextConfig = config;
            nextConfig.currentWall.size = static_cast<DWORD>(request.value);
            bool inLibrary = false;
            const auto record = FindRecord(DateOf(current), inLibrary);
            if (inLibrary) {
                ApplyCurrent(record, GetImage(DateOf(record), LibrarySizeIndex(nextConfig.currentWall.size)), nextConfig, true);
            } else {
                // An uncached daily image can only be resized while it is the latest item.
                const auto metadata = FetchMetadata();
                if (Get(metadata.enddate) != DateOf(current)) Fail(L"Current wallpaper is no longer the latest item");
                ApplyDaily(metadata, nextConfig);
            }
            SaveConfig(); message = L"Wallpaper size changed"; break;
        }
        case ProductAction::ChangeFrequency:
            if (request.value < 0 || request.value > 9) Fail(L"Frequency index must be 0..9");
            config.changeFreq = static_cast<DWORD>(request.value); UpdateInterval(); SaveConfig();
            message = L"Change frequency updated"; break;
        case ProductAction::AutoSave: {
            if (request.value != 0 && request.value != 1) Fail(L"AutoSave value must be 0 or 1");
            const DWORD previous = config.autoSave;
            config.autoSave = static_cast<DWORD>(request.value);
            try { SaveConfig(); }
            catch (...) { config.autoSave = previous; throw; }
            message = config.autoSave ? L"Automatic gallery saving enabled" : L"Automatic gallery saving disabled";
            if (testMode || previous || !config.autoSave) break;

            bool added = false;
            try { added = ImportLatest(FetchMetadata()); }
            catch (const std::exception& error) {
                if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) throw;
                message += L"; could not add the latest wallpaper to the gallery: " + ErrorText(error);
                break;
            }
            if (added) {
                RebuildSequence(); UpdateBrowseSelection(); SaveConfig();
                message += L"; latest wallpaper added to the gallery";
            } else message += L"; latest wallpaper is already in the gallery";
            break;
        }
        case ProductAction::AutoRun:
            if (request.value != 0 && request.value != 1) Fail(L"AutoRun preference value must be 0 or 1");
            WriteAutoRunPreference(configPath, request.value != 0);
            autoRunPreference = request.value != 0;
            message = autoRunPreference ? L"AutoRun preference saved; no operating-system startup entry was changed"
                                        : L"AutoRun preference cleared; no operating-system startup entry was changed";
            break;
        case ProductAction::Random:
            if (request.value != 0 && request.value != 1) Fail(L"Random value must be 0 or 1");
            config.changeRandom = static_cast<DWORD>(request.value);
            if (config.mode != SWP_DAILY) SelectGalleryMode(config.mode);
            else RebuildSequence();
            SaveConfig(); message = config.changeRandom ? L"Random sequence enabled" : L"Original gallery order restored"; break;
        case ProductAction::Pause:
            if (request.value != 0 && request.value != 1) Fail(L"Pause value must be 0 or 1");
            config.paused = static_cast<DWORD>(request.value); UpdateInterval(); SaveConfig();
            message = config.paused ? L"Automatic changes paused" : L"Automatic changes resumed"; break;
        case ProductAction::Reshuffle:
            if (config.mode == SWP_DAILY) {
                if (testMode) message = L"Test mode: daily refresh skipped";
                else Refresh(message);
            } else {
                SelectGalleryMode(config.mode);
                UpdateInterval(); SaveConfig();
                message = L"Wallpaper sequence reshuffled";
            }
            break;
        case ProductAction::BrowseSelect: {
            if (!request.text.empty()) SelectBrowse(request.text);
            else {
                const auto filtered = FilteredRecords();
                if (request.value < 1 || static_cast<size_t>(request.value) > filtered.size())
                    Fail(L"Browse ordinal is outside the filtered gallery");
                SelectBrowse(DateOf(filtered[static_cast<size_t>(request.value - 1)]));
            }
            message = L"Gallery preview selected"; break;
        }
        case ProductAction::BrowseFilter:
            if (request.value != 0 && request.value != 1) Fail(L"Browse filter must be 0 (all) or 1 (favorites)");
            browseFavoritesOnly = request.value == 1; UpdateBrowseSelection();
            message = browseFavoritesOnly ? L"Showing favorites" : L"Showing all gallery records"; break;
        case ProductAction::BrowseFavorite: {
            if (!hasSelected) Fail(L"Select a gallery record before changing its favorite status");
            CheckLibrary(Library_SetFavorite(selected.date, selected.favorite ? 0 : 1), L"Changing preview favorite");
            LoadRecords(); bool found = false; selected = FindRecord(selectedDate, found);
            if (!found) Fail(L"Selected gallery record disappeared");
            if (hasCurrent && Get(current.date) == selectedDate) {
                current.favorite = selected.favorite;
                config.currentWall.favorite = static_cast<DWORD>(selected.favorite);
            }
            RefreshSelectedImage(); RebuildSequence(); UpdateInterval(); SaveConfig();
            message = selected.favorite ? L"Preview added to favorites" : L"Preview removed from favorites";
            break;
        }
        case ProductAction::BrowseDelete: {
            if (!hasSelected) Fail(L"Select a gallery record before deleting");
            if (allRecords.size() <= 2) Fail(L"The gallery must retain at least two records");
            CheckLibrary(Library_Delete(selected.date), L"Deleting gallery record");
            ClearImageCache();
            LoadRecords();
            selectedDate.clear(); hasSelected = false; selectedImage.clear();
            RebuildSequence();
            UpdateBrowseSelection(); UpdateInterval(); SaveConfig();
            message = L"Gallery record deleted"; break;
        }
        case ProductAction::BrowseApply:
            if (!hasSelected) Fail(L"Select a gallery record before applying it");
            if (!SelectGalleryMode(SWP_GALLERY, selectedDate)) Fail(EmptyModeMessage(SWP_GALLERY));
            SaveConfig();
            message = testMode ? L"Test mode: preview application simulated"
                               : L"Preview applied as the current wallpaper";
            break;
        case ProductAction::ClearFavorites:
            CheckLibrary(Library_ClearFavorites(), L"Clearing favorites"); LoadRecords();
            if (hasCurrent) { current.favorite = 0; config.currentWall.favorite = 0; }
            if (config.mode == SWP_FAVORITES) config.mode = SWP_GALLERY;
            RebuildSequence(); UpdateBrowseSelection(); UpdateInterval(); SaveConfig();
            message = L"All favorites cleared"; break;
        case ProductAction::Relocate:
            if (request.text.empty()) Fail(L"Relocation destination is required");
            CheckLibrary(Library_RelocateW(request.text.c_str()), L"Relocating gallery");
            ClearImageCache();
            galleryDir = fs::absolute(fs::path(request.text)).lexically_normal();
            Put(config.galleryDir, galleryDir.wstring(), L"galleryDir");
            LoadRecords(); RebuildSequence(); SaveConfig(); RefreshSelectedImage();
            message = L"Gallery relocated"; break;
        case ProductAction::SaveCurrent:
            if (!hasCurrent || currentImage.empty()) Fail(L"There is no current image to save");
            if (request.text.empty()) Fail(L"SaveCurrent requires an output file path");
            EnsureDirectory(fs::path(request.text).parent_path());
            { const SWP_Options options = PlatformOptions();
            CheckPlatform(SWP_CopyFile(currentImagePath.c_str(), request.text.c_str(), TRUE, &options),
                          L"Saving current wallpaper");
            }
            message = L"Current wallpaper saved"; break;
        case ProductAction::StoreLatest:
            if (ImportLatest(FetchMetadata())) {
                RebuildSequence(); UpdateBrowseSelection(); SaveConfig();
                message = L"已将本壁纸保存到壁纸库！";
            } else message = L"壁纸库里已经有这张啦！不用再保存啦！";
            break;
        case ProductAction::ExportBrowse:
            if (!hasSelected) Fail(L"Select a gallery record before exporting");
            if (request.text.empty() || request.value < 1 || request.value > 5)
                Fail(L"ExportBrowse needs an output JPG path and size 1..5");
            CheckLibrary(Library_ExportW(selected.date, request.value, request.text.c_str(), 0),
                         L"Exporting preview image");
            message = L"Preview image exported"; break;
        case ProductAction::ExportAllSizes:
            if (!hasSelected) Fail(L"Select a gallery record before exporting all sizes");
            if (request.text.empty()) Fail(L"ExportAllSizes requires an output directory");
            EnsureDirectory(fs::path(request.text));
            {
                static const wchar_t* names[] = {L"UHD", L"1920x1200", L"1920x1080", L"1024x768", L"1080x1920"};
                for (DWORD size = 1; size <= 5; ++size) {
                    const fs::path output = fs::path(request.text) /
                        (Get(selected.date) + L"_" + names[size - 1] + L".jpg");
                    CheckLibrary(Library_ExportW(selected.date, static_cast<int32_t>(size),
                        output.c_str(), 0), L"Exporting gallery size");
                }
            }
            message = L"All five gallery sizes exported"; break;
        case ProductAction::ExportTheme: {
            SWP_ThemeOptions theme = request.theme; theme.cbSize = sizeof(theme);
            if (theme.selection == SWP_THEME_CUSTOM)
                Fail(L"Custom theme selection is unavailable: ProductRequest has no custom date list");
            if (theme.selection > SWP_THEME_CUSTOM) Fail(L"Invalid theme selection");
            if (theme.templateArchive[0] == L'\0') Put(theme.templateArchive, (runtime / L"ForTheme.EDT").wstring(), L"theme template");
            if (theme.sevenZipExe[0] == L'\0') Put(theme.sevenZipExe, (runtime / L"7za.exe").wstring(), L"7za.exe");
            if (theme.cabarcExe[0] == L'\0') Put(theme.cabarcExe, (runtime / L"cabarc.exe").wstring(), L"cabarc.exe");
            std::vector<SWL_Record> chosen;
            for (const auto& record : allRecords)
                if (theme.selection != SWP_THEME_FAVORITES || record.favorite) chosen.push_back(record);
            if (chosen.empty()) Fail(L"Theme selection contains no gallery records");
            TempDirectory temporary(dataRoot / L"work");
            const fs::path templateSource(theme.templateArchive);
            const fs::path stagedTemplate = temporary.path() / L"ForTheme.EDT";
            std::error_code copyError;
            fs::copy_file(templateSource, stagedTemplate, fs::copy_options::none, copyError);
            if (copyError) Fail(L"Unable to stage ForTheme.EDT under the data directory: " +
                                templateSource.wstring());
            Put(theme.templateArchive, stagedTemplate.wstring(), L"staged theme template");
            std::vector<SWP_Path> paths(chosen.size());
            for (size_t i = 0; i < chosen.size(); ++i) {
                fs::path output = temporary.path() / (Get(chosen[i].date) + L".jpg");
                CheckLibrary(Library_ExportW(chosen[i].date, static_cast<int32_t>(LibrarySizeIndex(theme.sizeIndex)),
                    output.c_str(), 0), L"Preparing theme image");
                Put(paths[i].path, output.wstring(), L"theme image path");
            }
            SWP_Options options = PlatformOptions(); DWORD toolExit = 0; wchar_t output[SWP_PATH_CAP]{};
            const DWORD error=SWP_ExportTheme(&theme, paths.data(), static_cast<DWORD>(paths.size()),
                &options, output, _countof(output), &toolExit);
            if(error!=ERROR_SUCCESS) {
                const wchar_t* reason=error==ERROR_ALREADY_EXISTS?L"同名主题包已存在，请修改主题名称后重试。":
                    error==ERROR_BAD_FORMAT?L"图库图片格式或尺寸不符合导出要求，请更换尺寸后重试。":
                    error==ERROR_PATH_NOT_FOUND||error==ERROR_FILE_NOT_FOUND?L"导出目录或主题打包工具不存在，请检查保存位置和程序文件。":
                    error==ERROR_ACCESS_DENIED?L"无法写入导出目录，请选择可写入的位置后重试。":
                    error==ERROR_CANCELLED?L"主题包导出已取消。":
                    error==ERROR_TIMEOUT?L"主题打包超时，请重试。":
                    error==ERROR_INSTALL_FAILURE?L"主题打包工具执行失败，请检查程序文件后重试。":
                    L"主题包导出失败，请检查名称、图片和保存位置后重试。";
                Fail(std::wstring(reason)+L" (Win32 error "+std::to_wstring(error)+
                    (toolExit?L", tool exit "+std::to_wstring(toolExit):L"")+L")");
            }
            message = L"主题包已导出：" + Get(output); break;
        }
        case ProductAction::Source:
            if (request.value < SWN_GLOBAL || request.value > SWN_CUSTOM) Fail(L"Source must be 1, 2, or 3");
            config.source = static_cast<DWORD>(request.value); SaveConfig(); message = L"Wallpaper source updated"; break;
        case ProductAction::Tick:
            message = Tick(); break;
        case ProductAction::Shutdown:
            Fail(L"Invalid action dispatch");
        }
        status = message;
        RefreshSelectedImage(); UpdateInterval();
        return MakeSnapshot(message);
    }
};

ProductBackend::ProductBackend() : impl_(new Impl()) {}
ProductBackend::~ProductBackend() {
    if (impl_ && impl_->libraryOpen && impl_->ownerThread == GetCurrentThreadId()) {
        Library_Close();
        impl_->libraryOpen = false;
    }
    delete impl_;
}

ProductSnapshot ProductBackend::Initialize(const std::wstring& runtime, const std::wstring& dataRoot,
    const std::wstring& configPath, bool testMode, HANDLE cancel, const std::wstring& proxy) {
    try {
        return impl_->Initialize(runtime, dataRoot, configPath, testMode, cancel, proxy);
    } catch (...) {
        if (impl_->libraryOpen && impl_->ownerThread == GetCurrentThreadId()) {
            Library_Close();
            impl_->libraryOpen = false;
        }
        impl_->initialized = false;
        throw;
    }
}

ProductSnapshot ProductBackend::Execute(const ProductRequest& request) {
    return impl_->Execute(request);
}

} // namespace stoutwall
