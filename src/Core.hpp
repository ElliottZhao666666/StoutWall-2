#pragma once
#include "Version.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace v6
{
    namespace fs = std::filesystem;
    using Microsoft::WRL::ComPtr;
    inline constexpr char Build[] = STOUTWALL_VERSION_STRING;
    inline constexpr wchar_t DllHash[] = L"AF9AAF5A5B2770632EAA7BAA8D65CB02C773EDB057BB694CF6E7960370F6BEF4";
    inline constexpr int ClientWidth = 1120, ClientHeight = 700, NavWidth = 210, PreviewWidth = 640,
                         PreviewHeight = 360;
    inline constexpr size_t MetadataLimit = 1048576, ImageLimit = 20 * 1024 * 1024;
    inline const std::vector<std::wstring> Pages = {L"每日壁纸", L"图库管理",     L"图库迁移",
                                                    L"主题导出", L"大图查看",     L"壁纸来源",
                                                    L"七日壁纸", L"自选主题壁纸", L"iOS 快捷指令"};
    using Bytes = std::vector<unsigned char>;
    void Need(bool ok, const char *message);
    void Win(bool ok, const char *stage);
    std::string Utf8(const std::wstring &text);
    std::wstring Wide(const std::string &text);
    std::string Ansi(const std::wstring &text);
    std::wstring Id();
    std::wstring Hash(const Bytes &bytes);
    Bytes Read(const fs::path &p, size_t max = 256 * 1024 * 1024);
    std::wstring FileHash(const fs::path &p);
    std::string GalleryOpenDiagnostic(const fs::path &root);
    fs::path Full(const std::wstring &p);
    bool Inside(const fs::path &root, const fs::path &p);
    bool Overlap(const fs::path &a, const fs::path &b);
    void NoLinks(const fs::path &p);
    void NewDir(const fs::path &p);
    void WriteNew(const fs::path &p, const Bytes &data);
    void TextNew(const fs::path &p, const std::string &text);
    void ReplaceOwn(const fs::path &p, const std::string &text);
    extern int AtomicRetries;
    void CopyTree(const fs::path &from, const fs::path &to);
    std::string Snapshot(const fs::path &root);
    struct Json
    {
        enum class Type
        {
            Null,
            Object,
            Array,
            String,
            Number,
            Boolean
        };
        Type type = Type::Null;
        std::wstring text;
        std::map<std::wstring, Json> object;
        std::vector<Json> array;
        const Json &At(const std::wstring &key) const;
        std::wstring Str() const;
        int Int() const;
        bool Bool() const;
    };
    Json ParseJson(const Bytes &bytes);
    std::string Quote(const std::wstring &value);
    struct Options
    {
        fs::path readonlyDir, write, out, response, dll, resumeFrom;
        std::wstring proxy, source = L"zh-CN";
        int timeout = 15000;
        bool allowWallpaper = false;
        static Options Parse(int argc, wchar_t **argv);
    };
    std::wstring Endpoint(const std::wstring &source);
    std::wstring Proxy(const std::wstring &value);
    void Https(const std::wstring &url);
    void RequestUrl(const std::wstring &url, const std::wstring &source, bool metadata);
    struct Decoded
    {
        ComPtr<IWICBitmapSource> source;
        UINT width = 0, height = 0;
    };
    Decoded Decode(const Bytes &bytes);
    class Api
    {
        HMODULE module = nullptr;

      public:
        std::mutex lock;
        int openRetries = 0;
        int openSuccesses = 0, closeCalls = 0;
        int(__stdcall *open)(const char *, int) = nullptr;
        int(__stdcall *close)() = nullptr;
        int(__stdcall *count)(int) = nullptr;
        int(__stdcall *date)(int, int, char *, int) = nullptr;
        int(__stdcall *field)(const char *, int, char *, int) = nullptr;
        int(__stdcall *favorite)(const char *) = nullptr;
        int(__stdcall *setFavorite)(const char *, int) = nullptr;
        int(__stdcall *image)(const char *, int, void *, int) = nullptr;
        int(__stdcall *exportImage)(const char *, int, const char *) = nullptr;
        int(__stdcall *deleteImage)(const char *) = nullptr;
        int(__stdcall *check)(const char *, int) = nullptr;
        int(__stdcall *importRecord)(const char *, const char *) = nullptr;
        explicit Api(const fs::path &p);
        ~Api();
        Api(const Api &) = delete;
        Api &operator=(const Api &) = delete;
    };
    struct Record
    {
        std::wstring date, title, description, author, link;
        bool favorite = false;
        Bytes image;
        Decoded bitmap;
    };
    class Gallery
    {
        Api &api;
        fs::path path;
        std::wstring Field(const std::string &date, int n);

      public:
        Gallery(Api &a, const fs::path &p) : api(a), path(p)
        {
            Ansi(p.wstring());
        }
        template <class F> auto Use(bool write, F operation) -> decltype(operation())
        {
            std::lock_guard<std::mutex> guard(api.lock);
            NoLinks(path);
            for (auto &item : fs::directory_iterator(path))
                NoLinks(item.path());
            int status;
            for (int attempt = 0;; attempt++)
            {
                status = api.open(Ansi(path.wstring()).c_str(), write ? 0 : 1);
                if (status != -3 || attempt >= 5)
                    break;
                api.openRetries++;
                Sleep(25 * (attempt + 1));
            }
            if (status != 0)
                throw std::runtime_error("Gallery_Open=" + std::to_string(status) + "; attempts<=6; " +
                                         GalleryOpenDiagnostic(path));
            api.openSuccesses++;
            struct Close
            {
                Api &a;
                ~Close()
                {
                    a.close();
                    a.closeCalls++;
                }
            } close{api};
            return operation();
        }
        void SwapPath(fs::path &next) noexcept
        {
            path.swap(next);
        }
        std::vector<std::wstring> Dates(bool favorites = false);
        Record Get(const std::wstring &date, int size = 3);
        std::wstring UrlBase(const std::wstring &date)
        {
            return Use(false, [&] { return Field(Ansi(date), 2); });
        }
        bool Toggle(const std::wstring &date);
        void Export(const std::wstring &date, int size, const fs::path &to);
        void Delete(const std::wstring &date);
        void Check(const std::wstring &date);
    };
    struct Settings
    {
        std::wstring date, source = L"zh-CN";
        bool paused = true, random = false;
        int seconds = 60, size = 3, viewMode = 0;
        std::wstring galleryRelative = L"gallery-write", themeDates, themeColor = L"C40078D4";
        bool favoritesOnly = false, themeShuffle = false, themeTransparent = true;
        int themeScope = 0, themeSize = 3, themeStyle = 3, themeSeconds = 60;
    };
    Settings ReadSettings(const fs::path &path);
    void ValidateGallery(Gallery &gallery);
    fs::path ImportReceiptPath(const fs::path &root, const std::wstring &date);
    void ArchiveImportReceipt(const fs::path &path, const fs::path &out);
    std::string VerifyImportReceipt(Gallery &gallery, const fs::path &root, const std::wstring &date);
    struct Online
    {
        std::wstring date, title, copyright, url, description, author, link, urlbase, wp, source, metadataHash;
        fs::path cache, importedGallery;
        bool isReplay = false;
        Bytes bytes;
        Decoded bitmap;
    };
    Online ParseBing(const Bytes &json, const std::wstring &source = L"zh-CN");
    using Getter = std::function<Bytes(const std::wstring &, size_t)>;
    Online Pipeline(const Options &args, const std::wstring &market, const fs::path &cache,
                    const std::function<void(const std::string &)> &log, const Getter &get);
    Online Fetch(const Options &args, const std::wstring &market, const fs::path &cache,
                 const std::function<void(const std::string &)> &log);
    Online Replay(const Options &args, const std::wstring &market, const fs::path &cache,
                  const std::function<void(const std::string &)> &log);
    class Engine
    {
      public:
        Options args;
        Api api;
        Gallery gallery;
        Settings state;
        Record current;
        bool hasCurrent = false;
        explicit Engine(const Options &a);
        void Initialize();
        fs::path ActiveWrite() const;
        std::vector<std::wstring> VisibleDates();
        void RefreshSelection();
        void Filter(bool favorites);
        void Adopt(const fs::path &path, const std::wstring &date);
        Online StageImport(Online online, const fs::path &source);
        void AdoptImport(const Online &online);
        void ThemeSelectCurrent();
        fs::path PathIn(const std::wstring &relative) const;
        void Log(const std::string &message) const;
        void Save();
        void Reload();
        void Select(const std::wstring &d);
        void Move(int delta, bool timer = false);
        bool rotationStarted = false; // Session-only; never loaded from settings.
        void PauseRotation();
        void StartRotation();
        std::wstring RotationTick();
        void Toggle();
        void Delete();
        fs::path Export();
        fs::path Migrate(const std::wstring &name = L"migrated-gallery");
        fs::path Theme(const std::wstring &name);
        fs::path IosGuide();
        fs::path PrepareWallpaper();
        std::wstring Wallpaper(bool restore);
    };
    class ComApartment
    {
        HRESULT hr;

      public:
        explicit ComApartment(DWORD type = COINIT_APARTMENTTHREADED) : hr(CoInitializeEx(nullptr, type))
        {
            Need(SUCCEEDED(hr), "CoInitializeEx");
        }
        ~ComApartment()
        {
            if (SUCCEEDED(hr))
                CoUninitialize();
        }
    };
} // namespace v6
