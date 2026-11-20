#include "Core.hpp"
#include <winhttp.h>
#include <wincrypt.h>
#include <sstream>
#include <iomanip>
#include <cwctype>
#include <limits>
namespace v6
{
    void Need(bool ok, const char *m)
    {
        if (!ok)
            throw std::runtime_error(m);
    }
    void Win(bool ok, const char *m)
    {
        if (!ok)
            throw std::runtime_error(std::string(m) + " win32=" + std::to_string(GetLastError()));
    }
    std::string Utf8(const std::wstring &s)
    {
        if (s.empty())
            return {};
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0,
                                    nullptr, nullptr);
        Need(n > 0, "UTF8 encode");
        std::string r(n, 0);
        Need(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n, nullptr,
                                 nullptr) == n,
             "UTF8 encode");
        return r;
    }
    std::wstring Wide(const std::string &s)
    {
        if (s.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
        Need(n > 0, "UTF8 decode");
        std::wstring r(n, 0);
        Need(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n) == n,
             "UTF8 decode");
        return r;
    }
    std::string Ansi(const std::wstring &s)
    {
        BOOL used = FALSE;
        UINT cp = GetACP();
        int n =
            WideCharToMultiByte(cp, 0, s.c_str(), -1, nullptr, 0, nullptr, cp == CP_UTF8 ? nullptr : &used);
        Need(n > 0 && !used, "ANSI path encode");
        std::vector<char> b(n);
        Need(WideCharToMultiByte(cp, 0, s.c_str(), -1, b.data(), n, nullptr,
                                 cp == CP_UTF8 ? nullptr : &used) > 0 &&
                 !used,
             "ANSI path encode");
        int m = MultiByteToWideChar(cp, 0, b.data(), -1, nullptr, 0);
        Need(m > 0, "ANSI path decode");
        std::vector<wchar_t> w(m);
        Need(MultiByteToWideChar(cp, 0, b.data(), -1, w.data(), m) > 0 && s == w.data(),
             "ANSI path round trip");
        return b.data();
    }
    std::wstring Id()
    {
        GUID g{};
        Need(SUCCEEDED(CoCreateGuid(&g)), "CoCreateGuid");
        wchar_t b[40];
        Need(StringFromGUID2(g, b, 40) > 0, "GUID string");
        std::wstring s;
        for (wchar_t c : std::wstring(b))
            if (iswxdigit(c))
                s += c;
        return s;
    }
    struct Handle
    {
        HANDLE h;
        explicit Handle(HANDLE v) : h(v)
        {
        }
        ~Handle()
        {
            if (h != INVALID_HANDLE_VALUE && h)
                CloseHandle(h);
        }
        Handle(const Handle &) = delete;
    };
    std::wstring Hash(const Bytes &b)
    {
        HCRYPTPROV p = 0;
        HCRYPTHASH h = 0;
        Win(CryptAcquireContextW(&p, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE,
            "CryptAcquireContext");
        if (!CryptCreateHash(p, CALG_SHA_256, 0, 0, &h))
        {
            CryptReleaseContext(p, 0);
            Win(false, "CryptCreateHash");
        }
        bool ok = CryptHashData(h, b.data(), (DWORD)b.size(), 0) != FALSE;
        BYTE v[32];
        DWORD n = 32;
        ok = ok && CryptGetHashParam(h, HP_HASHVAL, v, &n, 0);
        CryptDestroyHash(h);
        CryptReleaseContext(p, 0);
        Win(ok, "SHA256");
        const wchar_t *digits = L"0123456789ABCDEF";
        std::wstring r;
        for (BYTE c : v)
        {
            r += digits[c >> 4];
            r += digits[c & 15];
        }
        return r;
    }
    Bytes Read(const fs::path &p, size_t max)
    {
        NoLinks(p);
        Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
        Win(h.h != INVALID_HANDLE_VALUE, "Read open");
        LARGE_INTEGER n;
        Win(GetFileSizeEx(h.h, &n) != FALSE, "Read size");
        Need(n.QuadPart >= 0 && (unsigned long long)n.QuadPart <= max, "File size cap");
        Bytes b((size_t)n.QuadPart);
        DWORD done = 0;
        if (!b.empty())
            Win(ReadFile(h.h, b.data(), (DWORD)b.size(), &done, nullptr) != FALSE && done == b.size(),
                "Read bytes");
        return b;
    }
    std::wstring FileHash(const fs::path &p)
    {
        return Hash(Read(p));
    }
    std::string GalleryOpenDiagnostic(const fs::path &root)
    {
        auto file = root / L"Stout_WallGallery_Index.DDT";
        DWORD attributes = GetFileAttributesW(file.c_str());
        HANDLE h =
            CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        return "index=" + Utf8(file.wstring()) + "; index_read_win32=" + std::to_string(error) +
               "; attributes=" + std::to_string(attributes);
    }
    fs::path Full(const std::wstring &s)
    {
        Need(s.size() >= 3 && iswalpha(s[0]) && s[1] == L':' && (s[2] == L'\\' || s[2] == L'/'),
             "Local absolute drive path required");
        Need(s.find(L':', 2) == s.npos && s.find(L"..") == s.npos && s.find(L'\0') == s.npos, "Unsafe path");
        std::vector<wchar_t> b(32768);
        DWORD n = GetFullPathNameW(s.c_str(), (DWORD)b.size(), b.data(), nullptr);
        Need(n > 0 && n < b.size(), "Full path");
        std::wstring r(b.data(), n);
        while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/'))
            r.pop_back();
        Need(r.size() > 3 && GetDriveTypeW((r.substr(0, 3)).c_str()) == DRIVE_FIXED,
             "A non-root path on a local fixed drive is required");
        return fs::path(r);
    }
    static std::wstring Lower(std::wstring s)
    {
        for (auto &c : s)
            c = (wchar_t)towlower(c);
        return s;
    }
    bool Inside(const fs::path &root, const fs::path &p)
    {
        std::wstring a = Lower(root.wstring() + L"\\"), b = Lower(p.wstring());
        return b.size() > a.size() && b.compare(0, a.size(), a) == 0;
    }
    bool Overlap(const fs::path &a, const fs::path &b)
    {
        return Lower(a.wstring()) == Lower(b.wstring()) || Inside(a, b) || Inside(b, a);
    }
    void NoLinks(const fs::path &p)
    {
        for (auto q = p; !q.empty();)
        {
            DWORD a = GetFileAttributesW(q.c_str());
            if (a != INVALID_FILE_ATTRIBUTES)
                Need((a & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "Reparse point forbidden");
            else
            {
                DWORD e = GetLastError();
                Need(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND, "Cannot inspect path");
            }
            auto next = q.parent_path();
            if (next == q)
                break;
            q = next;
        }
    }
    void NewDir(const fs::path &p)
    {
        NoLinks(p);
        Need(fs::is_directory(p.parent_path()), "Parent must exist");
        Win(CreateDirectoryW(p.c_str(), nullptr) != FALSE, "Refuse reused/uncreatable directory");
    }
    void WriteNew(const fs::path &p, const Bytes &b)
    {
        NoLinks(p);
        Handle h(CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
        Win(h.h != INVALID_HANDLE_VALUE, "Refuse reused/unwritable file");
        DWORD done = 0;
        Win(WriteFile(h.h, b.data(), (DWORD)b.size(), &done, nullptr) != FALSE && done == b.size(),
            "Write bytes");
        Win(FlushFileBuffers(h.h) != FALSE, "Flush bytes");
    }
    void TextNew(const fs::path &p, const std::string &s)
    {
        WriteNew(p, Bytes(s.begin(), s.end()));
    }
    int AtomicRetries = 0;
    void ReplaceOwn(const fs::path &p, const std::string &s)
    {
        NoLinks(p);
        fs::path temp = p.wstring() + L"." + Id() + L".tmp";
        TextNew(temp, s);
        for (int attempt = 0;; attempt++)
        {
            if (MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                break;
            DWORD error = GetLastError();
            if (attempt >= 5 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
                                 error != ERROR_LOCK_VIOLATION))
            {
                SetLastError(error);
                Win(false, "Atomic settings write");
            }
            AtomicRetries++;
            Sleep(25 * (attempt + 1));
        }
    }
    static std::vector<fs::path> Children(const fs::path &p)
    {
        std::vector<fs::path> r;
        for (const auto &i : fs::directory_iterator(p))
        {
            NoLinks(i.path());
            r.push_back(i.path());
        }
        std::sort(r.begin(), r.end());
        return r;
    }
    void CopyTree(const fs::path &from, const fs::path &to)
    {
        NoLinks(from);
        NewDir(to);
        for (const auto &p : Children(from))
        {
            if (fs::is_directory(p))
                CopyTree(p, to / p.filename());
            else
            {
                Win(CopyFileW(p.c_str(), (to / p.filename()).c_str(), TRUE) != FALSE,
                    "Copy file conflict/failure");
            }
        }
    }
    static void Collect(const fs::path &root, const fs::path &dir,
                        std::vector<std::pair<std::string, fs::path>> &files)
    {
        for (const auto &p : Children(dir))
        {
            if (fs::is_directory(p))
                Collect(root, p, files);
            else
                files.emplace_back(Utf8(p.lexically_relative(root).generic_wstring()), p);
        }
    }
    std::string Snapshot(const fs::path &root)
    {
        NoLinks(root);
        std::vector<std::pair<std::string, fs::path>> files;
        Collect(root, root, files);
        std::sort(files.begin(), files.end());
        std::string r;
        for (const auto &p : files)
            r += p.first + "|" + std::to_string(fs::file_size(p.second)) + "|" + Utf8(FileHash(p.second)) +
                 "\n";
        return r;
    }
    const Json &Json::At(const std::wstring &k) const
    {
        Need(type == Type::Object && object.count(k), "JSON missing field");
        return object.at(k);
    }
    std::wstring Json::Str() const
    {
        Need(type == Type::String, "JSON string type");
        return text;
    }
    int Json::Int() const
    {
        Need(type == Type::Number, "JSON integer type");
        size_t n = 0;
        int v = std::stoi(text, &n);
        Need(n == text.size(), "JSON integer");
        return v;
    }
    bool Json::Bool() const
    {
        Need(type == Type::Boolean, "JSON bool type");
        return text == L"true";
    }
    class JsonParser
    {
        std::wstring text;
        size_t at = 0, nodes = 0;
        void Space()
        {
            while (at < text.size() &&
                   (text[at] == L' ' || text[at] == L'\r' || text[at] == L'\n' || text[at] == L'\t'))
                ++at;
        }
        wchar_t Take()
        {
            Need(at < text.size(), "Truncated JSON");
            return text[at++];
        }
        wchar_t Hex()
        {
            unsigned n = 0;
            for (int i = 0; i < 4; i++)
            {
                wchar_t c = Take();
                Need(iswxdigit(c) != 0 && c < 128, "JSON hex");
                n = n * 16 + (c >= L'0' && c <= L'9' ? c - L'0' : (wchar_t)towlower(c) - L'a' + 10);
            }
            return (wchar_t)n;
        }
        std::wstring String()
        {
            Need(Take() == L'"', "JSON quote");
            std::wstring r;
            for (;;)
            {
                wchar_t c = Take();
                if (c == L'"')
                    break;
                Need(c >= 32, "JSON control");
                if (c == L'\\')
                {
                    c = Take();
                    switch (c)
                    {
                    case L'"':
                    case L'\\':
                    case L'/':
                        break;
                    case L'b':
                        c = L'\b';
                        break;
                    case L'f':
                        c = L'\f';
                        break;
                    case L'n':
                        c = L'\n';
                        break;
                    case L'r':
                        c = L'\r';
                        break;
                    case L't':
                        c = L'\t';
                        break;
                    case L'u':
                    {
                        c = Hex();
                        if (c >= 0xD800 && c <= 0xDBFF)
                        {
                            r += c;
                            Need(Take() == L'\\' && Take() == L'u', "JSON surrogate");
                            c = Hex();
                            Need(c >= 0xDC00 && c <= 0xDFFF, "JSON surrogate");
                        }
                        else
                            Need(c < 0xDC00 || c > 0xDFFF, "JSON low surrogate");
                        break;
                    }
                    default:
                        Need(false, "JSON escape");
                    }
                }
                r += c;
            }
            return r;
        }
        Json Value(int depth)
        {
            Need(depth <= 32 && ++nodes <= 100000, "JSON complexity");
            Space();
            Need(at < text.size(), "JSON missing value");
            Json v;
            wchar_t c = text[at];
            if (c == L'"')
            {
                v.type = Json::Type::String;
                v.text = String();
            }
            else if (c == L'{' || c == L'[')
            {
                bool obj = c == L'{';
                ++at;
                v.type = obj ? Json::Type::Object : Json::Type::Array;
                Space();
                if (at < text.size() && text[at] == (obj ? L'}' : L']'))
                {
                    ++at;
                    return v;
                }
                for (;;)
                {
                    if (obj)
                    {
                        Space();
                        std::wstring key = String();
                        Space();
                        Need(Take() == L':', "JSON colon");
                        Need(v.object.emplace(key, Value(depth + 1)).second, "Duplicate JSON key");
                    }
                    else
                        v.array.push_back(Value(depth + 1));
                    Space();
                    wchar_t end = Take();
                    if (end == (obj ? L'}' : L']'))
                        break;
                    Need(end == L',', "JSON comma");
                }
            }
            else if (c == L't' || c == L'f' || c == L'n')
            {
                const wchar_t *literal = c == L't' ? L"true" : c == L'f' ? L"false" : L"null";
                std::wstring s = literal;
                Need(text.compare(at, s.size(), s) == 0, "JSON literal");
                at += s.size();
                v.type = c == L'n' ? Json::Type::Null : Json::Type::Boolean;
                v.text = s;
            }
            else
            {
                size_t start = at;
                if (text[at] == L'-')
                    ++at;
                Need(at < text.size(), "JSON number");
                if (text[at] == L'0')
                    ++at;
                else
                {
                    Need(text[at] >= L'1' && text[at] <= L'9', "JSON digit");
                    while (at < text.size() && text[at] >= L'0' && text[at] <= L'9')
                        ++at;
                }
                if (at < text.size() && text[at] == L'.')
                {
                    ++at;
                    size_t first = at;
                    while (at < text.size() && text[at] >= L'0' && text[at] <= L'9')
                        ++at;
                    Need(at > first, "JSON fraction");
                }
                if (at < text.size() && (text[at] == L'e' || text[at] == L'E'))
                {
                    ++at;
                    if (at < text.size() && (text[at] == L'+' || text[at] == L'-'))
                        ++at;
                    size_t first = at;
                    while (at < text.size() && text[at] >= L'0' && text[at] <= L'9')
                        ++at;
                    Need(at > first, "JSON exponent");
                }
                v.type = Json::Type::Number;
                v.text = text.substr(start, at - start);
            }
            return v;
        }

      public:
        explicit JsonParser(const Bytes &b) : text(Wide(std::string(b.begin(), b.end())))
        {
        }
        Json Parse()
        {
            Json v = Value(0);
            Space();
            Need(at == text.size(), "JSON trailing data");
            return v;
        }
    };
    Json ParseJson(const Bytes &bytes)
    {
        Need(bytes.size() <= MetadataLimit, "JSON size cap");
        return JsonParser(bytes).Parse();
    }
    std::string Quote(const std::wstring &s)
    {
        std::wstring r = L"\"";
        for (wchar_t c : s)
        {
            if (c == L'"' || c == L'\\')
            {
                r += L'\\';
                r += c;
            }
            else if (c < 32)
            {
                wchar_t b[8];
                swprintf_s(b, L"\\u%04x", (unsigned)c);
                r += b;
            }
            else
                r += c;
        }
        return Utf8(r + L'"');
    }
    struct Url
    {
        URL_COMPONENTS c{};
        std::wstring value;
        explicit Url(const std::wstring &s) : value(s)
        {
            c.dwStructSize = sizeof(c);
            c.dwSchemeLength = c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength =
                c.dwUserNameLength = c.dwPasswordLength = (DWORD)-1;
            Win(WinHttpCrackUrl(value.c_str(), (DWORD)value.size(), 0, &c) != FALSE, "URL parse");
        }
        std::wstring Host() const
        {
            return std::wstring(c.lpszHostName, c.dwHostNameLength);
        }
        std::wstring Resource() const
        {
            std::wstring r(c.lpszUrlPath, c.dwUrlPathLength);
            if (r.empty())
                r = L"/";
            r.append(c.lpszExtraInfo, c.dwExtraInfoLength);
            return r;
        }
    };
    std::wstring Endpoint(const std::wstring &market)
    {
        if (market == L"custom")
            return L"https://api.bingpics.ybjun.com/api/latest";
        Need(market == L"zh-CN" || market == L"en-US", "Source must be zh-CN, en-US or custom");
        return L"https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=" + market;
    }
    void Https(const std::wstring &value)
    {
        Url u(value);
        std::wstring h = Lower(u.Host());
        Need(u.c.nScheme == INTERNET_SCHEME_HTTPS && u.c.nPort == 443 && !u.c.dwUserNameLength &&
                 !u.c.dwPasswordLength && value.find(L'#') == value.npos,
             "HTTPS without credentials/fragment required");
        Need(h == L"bing.com" || (h.size() > 9 && h.compare(h.size() - 9, 9, L".bing.com") == 0),
             "Bing HTTPS host required");
    }
    void RequestUrl(const std::wstring &value, const std::wstring &source, bool metadata)
    {
        Endpoint(source);
        if (metadata && source == L"custom")
        {
            Url u(value);
            Need(value == Endpoint(L"custom") && u.c.nScheme == INTERNET_SCHEME_HTTPS && u.c.nPort == 443 &&
                     !u.c.dwUserNameLength && !u.c.dwPasswordLength,
                 "Exact custom metadata endpoint required");
        }
        else
            Https(value);
    }
    std::wstring Proxy(const std::wstring &value)
    {
        if (value.empty())
            return {};
        Url u(value);
        Need(u.c.nScheme == INTERNET_SCHEME_HTTP && !u.c.dwUserNameLength && !u.c.dwPasswordLength &&
                 u.Resource() == L"/" && value.find(L'#') == value.npos,
             "Proxy must be http://HOST:PORT without credentials/path");
        std::wstring host = u.Host();
        if (host.find(L':') != host.npos)
            host = L"[" + host + L"]";
        return host + L":" + std::to_wstring(u.c.nPort);
    }
    Options Options::Parse(int argc, wchar_t **argv)
    {
        Options o;
        std::map<std::wstring, std::wstring> d;
        for (int i = 1; i < argc; i++)
        {
            std::wstring k = argv[i];
            if (k == L"--allow-wallpaper")
            {
                Need(!o.allowWallpaper, "Duplicate flag");
                o.allowWallpaper = true;
                continue;
            }
            Need(k == L"--readonly" || k == L"--write" || k == L"--out" || k == L"--response" ||
                     k == L"--dll" || k == L"--proxy" || k == L"--source" || k == L"--timeout-ms" ||
                     k == L"--resume-from",
                 "Unknown argument");
            Need(i + 1 < argc, "Missing value");
            Need(d.emplace(k, argv[++i]).second, "Duplicate argument");
        }
        for (auto k : {L"--readonly", L"--write", L"--out", L"--dll"})
            Need(d.count(k) > 0, "Required path argument missing");
        o.readonlyDir = Full(d[L"--readonly"]);
        o.write = Full(d[L"--write"]);
        o.out = Full(d[L"--out"]);
        if (d.count(L"--response"))
            o.response = Full(d[L"--response"]);
        o.dll = Full(d[L"--dll"]);
        if (d.count(L"--proxy"))
            o.proxy = d[L"--proxy"];
        if (d.count(L"--source"))
            o.source = d[L"--source"];
        if (d.count(L"--timeout-ms"))
        {
            size_t n = 0;
            o.timeout = std::stoi(d[L"--timeout-ms"], &n);
            Need(n == d[L"--timeout-ms"].size(), "Timeout integer");
        }
        Need(o.timeout >= 1000 && o.timeout <= 60000, "Timeout range 1000..60000ms");
        if (d.count(L"--resume-from"))
        {
            o.resumeFrom = Full(d[L"--resume-from"]);
            NoLinks(o.resumeFrom);
            Need(fs::is_directory(o.resumeFrom) && !Overlap(o.out, o.resumeFrom),
                 "Resume source missing or overlaps output");
        }
        Endpoint(o.source);
        Proxy(o.proxy);
        Need(Inside(o.out, o.write), "--write must be NEW child of --out");
        for (const auto &p : {o.readonlyDir, o.dll})
            Need(!Overlap(o.out, p), "Inputs overlap output");
        if (!o.response.empty())
            Need(!Overlap(o.out, o.response), "Response input overlaps output");
        for (const auto &p : {o.readonlyDir, o.dll, o.out, o.write})
            NoLinks(p);
        if (!o.response.empty())
            NoLinks(o.response);
        Ansi(o.readonlyDir.wstring());
        Ansi(o.write.wstring());
        Need(fs::is_directory(o.readonlyDir) && fs::is_regular_file(o.dll), "Missing input");
        Need(o.response.empty() || fs::is_regular_file(o.response), "Offline response file missing");
        Need(FileHash(o.dll) == DllHash, "Unapproved gallery.dll");
        return o;
    }
    Decoded Decode(const Bytes &b)
    {
        Need(b.size() > 4 && b.size() <= ImageLimit, "Image byte cap");
        ComPtr<IWICImagingFactory> f;
        Need(SUCCEEDED(
                 CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))),
             "WIC factory");
        ComPtr<IWICStream> s;
        ComPtr<IWICBitmapDecoder> d;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> convert;
        Need(SUCCEEDED(f->CreateStream(&s)) &&
                 SUCCEEDED(s->InitializeFromMemory(const_cast<BYTE *>(b.data()), (DWORD)b.size())) &&
                 SUCCEEDED(f->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &d)) &&
                 SUCCEEDED(d->GetFrame(0, &frame)),
             "Image decode header");
        Decoded r;
        Need(SUCCEEDED(frame->GetSize(&r.width, &r.height)) && r.width > 0 && r.height > 0 &&
                 (unsigned long long)r.width * r.height <= 40000000,
             "Image pixel cap");
        Need(SUCCEEDED(f->CreateFormatConverter(&convert)) &&
                 SUCCEEDED(convert->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, nullptr, 0,
                                               WICBitmapPaletteTypeCustom)),
             "WIC pixel conversion");
        ComPtr<IWICBitmap> bitmap;
        Need(SUCCEEDED(f->CreateBitmapFromSource(convert.Get(), WICBitmapCacheOnLoad, &bitmap)),
             "WIC complete decode");
        r.source = bitmap;
        return r;
    }
    Api::Api(const fs::path &p)
    {
        module = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        Win(module != nullptr, "LoadLibraryEx gallery");
#define BIND(member, name)                                                                                   \
    member = reinterpret_cast<decltype(member)>(GetProcAddress(module, name));                               \
    Need(member != nullptr, "Missing gallery export")
        BIND(open, "Gallery_Open");
        BIND(close, "Gallery_Close");
        BIND(count, "Gallery_Count");
        BIND(date, "Gallery_GetDate");
        BIND(field, "Gallery_GetField");
        BIND(favorite, "Gallery_GetFavorite");
        BIND(setFavorite, "Gallery_SetFavorite");
        BIND(image, "Gallery_GetImage");
        BIND(exportImage, "Gallery_ExportImage");
        BIND(deleteImage, "Gallery_Delete");
        BIND(check, "Gallery_Check");
        BIND(importRecord, "Gallery_Import");
#undef BIND
    }
    Api::~Api()
    {
        if (module)
            FreeLibrary(module);
    }
    std::wstring Gallery::Field(const std::string &d, int field)
    {
        int n = api.field(d.c_str(), field, nullptr, 0);
        Need(n > 0 && n <= 65536, "Field length");
        std::vector<char> b(n);
        Need(api.field(d.c_str(), field, b.data(), n) == n, "Field read");
        std::string s(b.data(), n - 1);
        if (s.compare(0, 3, "u8:") == 0)
            return Wide(s.substr(3));
        int m = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
        Need(m >= 0, "ANSI field decode");
        std::wstring r(m, 0);
        if (m)
            Need(MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), r.data(), m) == m,
                 "ANSI field decode");
        return r;
    }
    std::vector<std::wstring> Gallery::Dates(bool favorites)
    {
        return Use(false,
                   [&]
                   {
                       int n = api.count(favorites ? 1 : 0);
                       Need(n >= 0 && n <= 10000, "Gallery_Count");
                       std::vector<std::wstring> r;
                       for (int i = 1; i <= n; i++)
                       {
                           char b[32]{};
                           Need(api.date(i, favorites ? 1 : 0, b, 32) == 9, "Date ABI");
                           r.push_back(Wide(b));
                       }
                       return r;
                   });
    }
    Record Gallery::Get(const std::wstring &date, int size)
    {
        return Use(false,
                   [&]
                   {
                       std::string d = Ansi(date);
                       int n = api.image(d.c_str(), size, nullptr, 0);
                       Need(n > 4 && (size_t)n <= ImageLimit, "Gallery image size");
                       Record r;
                       r.date = date;
                       r.image.resize(n);
                       Need(api.image(d.c_str(), size, r.image.data(), n) == n, "Gallery image read");
                       r.bitmap = Decode(r.image);
                       r.title = Field(d, 3);
                       r.description = Field(d, 4);
                       r.author = Field(d, 5);
                       r.link = Field(d, 6);
                       int f = api.favorite(d.c_str());
                       Need(f == 0 || f == 1, "Favorite read");
                       r.favorite = f == 1;
                       return r;
                   });
    }
    bool Gallery::Toggle(const std::wstring &date)
    {
        int expected = Use(true,
                           [&]
                           {
                               auto d = Ansi(date);
                               int f = api.favorite(d.c_str());
                               Need(f == 0 || f == 1, "Favorite read");
                               Need(api.setFavorite(d.c_str(), 1 - f) == 0, "Favorite write");
                               return 1 - f;
                           });
        bool actual = Get(date).favorite;
        Need(actual == (expected == 1), "Favorite reopen mismatch");
        return actual;
    }
    void Gallery::Export(const std::wstring &date, int size, const fs::path &to)
    {
        NoLinks(to);
        Need(!fs::exists(to), "Export conflict");
        Use(false,
            [&]
            {
                Need(api.exportImage(Ansi(date).c_str(), size, Ansi(to.wstring()).c_str()) == 0,
                     "Export failed");
                return 0;
            });
        Decode(Read(to, ImageLimit));
    }
    void Gallery::Delete(const std::wstring &date)
    {
        Use(true,
            [&]
            {
                Need(api.deleteImage(Ansi(date).c_str()) == 0, "Delete failed");
                return 0;
            });
        auto dates = Dates();
        Need(std::find(dates.begin(), dates.end(), date) == dates.end(), "Delete reopen mismatch");
    }
    void Gallery::Check(const std::wstring &date)
    {
        Use(false,
            [&]
            {
                Need(api.check(Ansi(date).c_str(), 1) == 0, "Gallery_Check");
                return 0;
            });
    }
    Engine::Engine(const Options &a) : args(a), api(a.dll), gallery(api, a.write)
    {
        NewDir(a.out);
        TextNew(a.out / L"process.guard",
                "build=" + std::string(Build) + "\npid=" + std::to_string(GetCurrentProcessId()) + "\n");
        Initialize();
    }
    fs::path Engine::PathIn(const std::wstring &s) const
    {
        auto p = Full((args.out / s).wstring());
        Need(Inside(args.out, p), "Output escapes round");
        NoLinks(p);
        return p;
    }
    void Engine::Log(const std::string &s) const
    {
        SYSTEMTIME t;
        GetSystemTime(&t);
        char b[80];
        sprintf_s(b, "%04u-%02u-%02uT%02u:%02u:%02uZ ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
        TextNew(PathIn(L"event-" + Id() + L".log"), std::string(b) + s + "\n");
    }
    void Engine::Save()
    {
        state.date = hasCurrent ? current.date : L"";
        ReplaceOwn(
            PathIn(L"settings.json"),
            "{\"Date\":" + Quote(state.date) + ",\"Source\":" + Quote(state.source) + ",\"Paused\":" +
                (state.paused ? "true" : "false") + ",\"Random\":" + (state.random ? "true" : "false") +
                ",\"Seconds\":" + std::to_string(state.seconds) + ",\"Size\":" + std::to_string(state.size) +
                ",\"ViewMode\":" + std::to_string(state.viewMode) +
                ",\"GalleryRelative\":" + Quote(state.galleryRelative) +
                ",\"FavoritesOnly\":" + (state.favoritesOnly ? "true" : "false") +
                ",\"ThemeDates\":" + Quote(state.themeDates) + ",\"ThemeColor\":" + Quote(state.themeColor) +
                ",\"ThemeScope\":" + std::to_string(state.themeScope) + ",\"ThemeSize\":" +
                std::to_string(state.themeSize) + ",\"ThemeStyle\":" + std::to_string(state.themeStyle) +
                ",\"ThemeSeconds\":" + std::to_string(state.themeSeconds) +
                ",\"ThemeShuffle\":" + (state.themeShuffle ? "true" : "false") +
                ",\"ThemeTransparent\":" + (state.themeTransparent ? "true" : "false") + "}\n");
    }
    void Engine::Reload()
    {
        auto settings = ReadSettings(PathIn(L"settings.json"));
        auto next = PathIn(settings.galleryRelative);
        Gallery candidate(api, next);
        ValidateGallery(candidate);
        state = std::move(settings);
        gallery.SwapPath(next);
        RefreshSelection();
    }
    void Engine::Select(const std::wstring &date)
    {
        current = gallery.Get(date, state.size);
        hasCurrent = true;
        state.date = date;
    }
    void Engine::Move(int delta, bool timer)
    {
        if (timer && state.paused)
            return;
        auto dates = VisibleDates();
        if (dates.empty())
        {
            hasCurrent = false;
            current = Record{};
            return;
        }
        auto found = std::find(dates.begin(), dates.end(), current.date);
        int at = found == dates.end() ? 0 : (int)(found - dates.begin());
        int n = (int)dates.size(), next = (at + delta + n) % n;
        if (state.random && n > 1)
        {
            HCRYPTPROV p = 0;
            Win(CryptAcquireContextW(&p, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE,
                "RNG context");
            DWORD value = 0;
            BOOL ok = CryptGenRandom(p, sizeof(value), reinterpret_cast<BYTE *>(&value));
            CryptReleaseContext(p, 0);
            Win(ok != FALSE, "RNG bytes");
            next = (at + 1 + (int)(value % (DWORD)(n - 1))) % n;
        }
        Select(dates[next]);
        Save();
    }
    void Engine::PauseRotation()
    {
        rotationStarted = false;
        state.paused = true;
        Save();
    }
    void Engine::StartRotation()
    {
        try
        {
            state.paused = false;
            Save();
            rotationStarted = true;
        }
        catch (...)
        {
            rotationStarted = false;
            state.paused = true;
            throw;
        }
    }
    std::wstring Engine::RotationTick()
    {
        if (!rotationStarted || state.paused)
            return L"";
        try
        {
            Move(1, true);
            Need(hasCurrent, "No record available for rotation");
            if (args.allowWallpaper)
                return Wallpaper(false);
            Log("rotation dry-run; no system wallpaper settings accessed");
            return L"dry-run：已轮换预览；应用桌面需 --allow-wallpaper";
        }
        catch (...)
        {
            // Stop in memory before Save; preserve the original operation exception.
            try
            {
                PauseRotation();
            }
            catch (...)
            {
            }
            throw;
        }
    }
    void Engine::Toggle()
    {
        Need(hasCurrent, "No record");
        gallery.Toggle(current.date);
        RefreshSelection();
        Save();
    }
    void Engine::Delete()
    {
        Need(hasCurrent, "No record");
        auto receipt = ImportReceiptPath(ActiveWrite(), current.date);
        gallery.Delete(current.date);
        ArchiveImportReceipt(receipt, args.out);
        auto dates = VisibleDates();
        if (dates.empty())
        {
            hasCurrent = false;
            current = Record{};
        }
        else
            Select(dates.front());
        Save();
    }
    fs::path Engine::Export()
    {
        Need(hasCurrent, "No record");
        auto dir = PathIn(L"exports");
        fs::create_directory(dir);
        auto p = dir / (current.date + L"-" + Id() + L".jpg");
        gallery.Export(current.date, state.size, p);
        return p;
    }
    fs::path Engine::IosGuide()
    {
        Need(hasCurrent, "No record");
        auto dir = PathIn(L"ios-guide-" + Id());
        NewDir(dir);
        gallery.Export(current.date, state.size, dir / L"wallpaper.jpg");
        TextNew(dir / L"iOS-操作说明.txt",
                Utf8(L"iOS 手动操作说明（非已签名 .shortcut 文件）\n当前图库日期：" + current.date +
                     L"\n标题：" + current.title + L"\n作者：" + current.author +
                     L"\n\n将同目录 wallpaper.jpg 传到 "
                     L"iPhone，再在照片/墙纸设置中选择该图。\n可自行在快捷指令中使用以下元数据地址，再读取 "
                     L"images[0].url 并获取对应 HTTPS 图像：\n" +
                     Endpoint(state.source) + L"\n本程序不会向手机推送、安装快捷指令或修改手机设置。\n"));
        return dir;
    }
    Online ParseBing(const Bytes &bytes, const std::wstring &source)
    {
        Json j = ParseJson(bytes);
        Endpoint(source);
        if (source == L"custom")
        {
            Online r;
            r.date = j.At(L"enddate").Str();
            r.title = j.At(L"title").Str();
            r.description = j.At(L"description").Str();
            r.author = j.At(L"author").Str();
            r.copyright = r.author;
            r.link = j.At(L"copyrightlink").Str();
            r.urlbase = j.At(L"urlbase").Str();
            const auto &wp = j.At(L"wp");
            Need(wp.type == Json::Type::String || wp.type == Json::Type::Boolean ||
                     wp.type == Json::Type::Number || wp.type == Json::Type::Null,
                 "Custom wp scalar");
            r.wp = wp.type == Json::Type::String ? Wide(Quote(wp.Str()))
                   : wp.type == Json::Type::Null ? L"null"
                                                 : wp.text;
            auto base = r.urlbase;
            if (!base.empty() && base.front() == L'/')
                base.erase(0, 1);
            const std::wstring prefix = L"th?id=OHR.";
            Need(base.rfind(prefix, 0) == 0 && base.size() > prefix.size() &&
                     std::all_of(base.begin() + prefix.size(), base.end(),
                                 [](wchar_t c)
                                 {
                                     return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                                            (c >= L'0' && c <= L'9') || c == L'_' || c == L'.' || c == L'-';
                                 }),
                 "Invalid custom Bing urlbase");
            r.url = L"https://www.bing.com/" + base + L"_UHD.jpg&rf=LaDigue_UHD.jpg&pid=hp";
            Https(r.url);
            return r;
        }
        const auto &images = j.At(L"images");
        Need(images.type == Json::Type::Array && !images.array.empty(), "images empty");
        const auto &item = images.array.front();
        auto field = [&](const std::wstring &k)
        {
            return item.object.count(k) ? item.At(k).Str() : L"";
        };
        Online r;
        r.date = field(L"enddate");
        r.title = field(L"title");
        r.copyright = field(L"copyright");
        r.description = r.copyright;
        auto split = r.copyright.rfind(L"(\u00a9"), fullWidth = r.copyright.rfind(L"\uff08\u00a9");
        if (fullWidth != std::wstring::npos && (split == std::wstring::npos || fullWidth > split))
            split = fullWidth;
        if (split != std::wstring::npos && split > 0 &&
            (r.copyright.back() == L')' || r.copyright.back() == L'\uff09'))
        {
            r.description = r.copyright.substr(0, split);
            while (!r.description.empty() && iswspace(r.description.back()))
                r.description.pop_back();
            r.author = r.copyright.substr(split + 1, r.copyright.size() - split - 2);
            while (!r.author.empty() && iswspace(r.author.back()))
                r.author.pop_back();
        }
        else if (r.copyright.rfind(L"©", 0) == 0)
        {
            r.author = r.copyright;
            r.description.clear();
        }
        r.urlbase = field(L"urlbase");
        r.link = field(L"copyrightlink");
        r.url = field(L"url");
        if (r.url.size() > 1 && r.url[0] == L'/' && r.url[1] != L'/')
            r.url = L"https://www.bing.com" + r.url;
        Https(r.url);
        return r;
    }
    Online Pipeline(const Options &, const std::wstring &market, const fs::path &cache,
                    const std::function<void(const std::string &)> &log, const Getter &get)
    {
        RequestUrl(Endpoint(market), market, true);
        Bytes meta = get(Endpoint(market), MetadataLimit);
        auto r = ParseBing(meta, market);
        RequestUrl(r.url, market, false);
        auto image = get(r.url, ImageLimit);
        auto decoded = Decode(image);
        fs::create_directory(cache);
        auto dir = cache / (L"bing-" + Id());
        NewDir(dir);
        WriteNew(dir / L"metadata.json", meta);
        WriteNew(dir / L"image.jpg", image);
        auto reread = Read(dir / L"image.jpg", ImageLimit);
        Need(Hash(reread) == Hash(image), "Cache image mismatch");
        auto parsed = ParseBing(Read(dir / L"metadata.json", MetadataLimit), market);
        Need(parsed.url == r.url, "Cache metadata mismatch");
        parsed.bytes = std::move(reread);
        parsed.bitmap = Decode(parsed.bytes);
        parsed.cache = dir;
        parsed.source = market;
        parsed.metadataHash = Hash(meta);
        log("Bing decoded+cache-reread market=" + Utf8(market) + " sha256=" + Utf8(Hash(parsed.bytes)));
        return parsed;
    }
    struct Internet
    {
        HINTERNET h = nullptr;
        ~Internet()
        {
            if (h)
                WinHttpCloseHandle(h);
        }
    };
    static void CALLBACK Observe(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD length)
    {
        if (context && status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE && info && length == sizeof(DWORD))
            InterlockedOr(reinterpret_cast<volatile LONG *>(context),
                          (LONG) * reinterpret_cast<DWORD *>(info));
    }
    static Bytes HttpGet(const Options &a, const std::wstring &address, size_t cap,
                         const std::function<void(const std::string &)> &log, const std::wstring &source,
                         bool metadata)
    {
        RequestUrl(address, source, metadata);
        Url u(address);
        std::wstring proxy = Proxy(a.proxy);
        Internet session, connection, request;
        volatile LONG failures = 0;
        ULONGLONG start = GetTickCount64();
        try
        {
            session.h = WinHttpOpen(
                L"StoutWall-v6/20261120-v6-04",
                proxy.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxy.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
            Win(session.h != nullptr, "WinHttpOpen");
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            Win(WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)) !=
                    FALSE,
                "TLS12 policy");
            Win(WinHttpSetTimeouts(session.h, a.timeout, a.timeout, a.timeout, a.timeout) != FALSE,
                "WinHTTP timeouts");
            connection.h = WinHttpConnect(session.h, u.Host().c_str(), u.c.nPort, 0);
            Win(connection.h != nullptr, "WinHttpConnect");
            request.h =
                WinHttpOpenRequest(connection.h, L"GET", u.Resource().c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            Win(request.h != nullptr, "WinHttpOpenRequest");
            DWORD disable =
                WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
            Win(WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) !=
                    FALSE,
                "Disable redirects/auth/cookies");
            DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
            Win(WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)) !=
                    FALSE,
                "Disable autologon");
            DWORD revocation = WINHTTP_ENABLE_SSL_REVOCATION;
            Win(WinHttpSetOption(request.h, WINHTTP_OPTION_ENABLE_FEATURE, &revocation, sizeof(revocation)) !=
                    FALSE,
                "Certificate revocation");
            DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&failures);
            Win(WinHttpSetOption(request.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE,
                "Diagnostic context");
            Win(WinHttpSetStatusCallback(request.h, Observe, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0) !=
                    WINHTTP_INVALID_STATUS_CALLBACK,
                "Diagnostic callback");
            const wchar_t headers[] =
                L"Accept: */*\r\nAccept-Encoding: identity\r\nCache-Control: no-cache\r\n";
            Win(WinHttpSendRequest(request.h, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, context) !=
                    FALSE,
                "WinHttpSendRequest");
            Win(WinHttpReceiveResponse(request.h, nullptr) != FALSE, "WinHttpReceiveResponse");
            DWORD status = 0, n = sizeof(status);
            Win(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &n,
                                    WINHTTP_NO_HEADER_INDEX) != FALSE,
                "HTTP status");
            log("HTTPS status=" + std::to_string(status) + " host=" + Utf8(u.Host()));
            Need(status == 200, "HTTP non-200");
            Bytes body;
            BYTE buffer[8192];
            for (;;)
            {
                Need(GetTickCount64() - start <= (ULONGLONG)a.timeout, "HTTP elapsed deadline");
                DWORD received = 0;
                Win(WinHttpReadData(request.h, buffer, sizeof(buffer), &received) != FALSE,
                    "WinHttpReadData");
                if (!received)
                    break;
                Need(body.size() + received <= cap, "Response byte cap");
                body.insert(body.end(), buffer, buffer + received);
            }
            return body;
        }
        catch (...)
        {
            log("HTTPS failure secure_flags=" + std::to_string(failures));
            throw;
        }
    }
    Online Fetch(const Options &a, const std::wstring &market, const fs::path &cache,
                 const std::function<void(const std::string &)> &log)
    {
        return Pipeline(a, market, cache, log,
                        [&](const std::wstring &u, size_t cap)
                        {
                            return HttpGet(a, u, cap, log, market, cap == MetadataLimit);
                        });
    }
    Online Replay(const Options &a, const std::wstring &market, const fs::path &cache,
                  const std::function<void(const std::string &)> &log)
    {
        int calls = 0;
        auto result = Pipeline(
            a, market, cache,
            [&](const std::string &m)
            {
                log("OFFLINE_REPLAY " + m);
            },
            [&](const std::wstring &, size_t cap)
            {
                return Read(++calls == 1 ? a.response : a.response.parent_path() / L"offline-image.jpg", cap);
            });
        result.isReplay = true;
        return result;
    }
    struct Original
    {
        std::wstring path, style, tile;
        bool hadStyle = false, hadTile = false;
    };
    fs::path Engine::PrepareWallpaper()
    {
        Need(hasCurrent, "No record");
        ComPtr<IWICImagingFactory> factory;
        Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory))),
             "BMP factory");
        ComPtr<IStream> stream;
        Need(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "BMP stream");
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        Need(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &encoder)) &&
                 SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                 SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
                 SUCCEEDED(frame->Initialize(properties.Get())),
             "BMP encoder");
        Need(SUCCEEDED(frame->SetSize(current.bitmap.width, current.bitmap.height)), "BMP size");
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        Need(SUCCEEDED(frame->SetPixelFormat(&format)), "BMP pixel format");
        ComPtr<IWICFormatConverter> convert;
        Need(SUCCEEDED(factory->CreateFormatConverter(&convert)) &&
                 SUCCEEDED(convert->Initialize(current.bitmap.source.Get(), format, WICBitmapDitherTypeNone,
                                               nullptr, 0, WICBitmapPaletteTypeCustom)),
             "BMP conversion");
        Need(SUCCEEDED(frame->WriteSource(convert.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
                 SUCCEEDED(encoder->Commit()),
             "BMP encode complete");
        STATSTG stat{};
        Need(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart <= 160000054,
             "BMP size cap");
        LARGE_INTEGER zero{};
        Need(SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)), "BMP seek");
        Bytes bytes((size_t)stat.cbSize.QuadPart);
        ULONG done = 0;
        Need(SUCCEEDED(stream->Read(bytes.data(), (ULONG)bytes.size(), &done)) && done == bytes.size(),
             "BMP read");
        auto path = PathIn(L"wallpaper-" + Id() + L".bmp");
        WriteNew(path, bytes);
        return path;
    }
    struct RegKey
    {
        HKEY key = nullptr;
        ~RegKey()
        {
            if (key)
                RegCloseKey(key);
        }
    };
    static std::wstring RegString(HKEY k, const wchar_t *name, bool &had)
    {
        DWORD type = 0, size = 0;
        LONG e = RegQueryValueExW(k, name, nullptr, &type, nullptr, &size);
        if (e == ERROR_FILE_NOT_FOUND)
        {
            had = false;
            return {};
        }
        Need(e == ERROR_SUCCESS && type == REG_SZ && size <= 4096, "Wallpaper mode type");
        std::vector<wchar_t> b(size / 2 + 1, 0);
        Need(RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE *>(b.data()), &size) ==
                 ERROR_SUCCESS,
             "Wallpaper mode read");
        had = true;
        return b.data();
    }
    static Original CaptureWallpaper()
    {
        Original r;
        std::vector<wchar_t> b(32768);
        Win(SystemParametersInfoW(SPI_GETDESKWALLPAPER, (UINT)b.size(), b.data(), 0) != FALSE,
            "SPI_GETDESKWALLPAPER");
        r.path = b.data();
        RegKey k;
        Need(RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_QUERY_VALUE, &k.key) ==
                 ERROR_SUCCESS,
             "Wallpaper mode open");
        r.style = RegString(k.key, L"WallpaperStyle", r.hadStyle);
        r.tile = RegString(k.key, L"TileWallpaper", r.hadTile);
        return r;
    }
    static void SetWallpaper(const Original &r)
    {
        RegKey k;
        Need(RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_SET_VALUE, &k.key) ==
                 ERROR_SUCCESS,
             "Wallpaper mode write open");
        auto set = [&](const wchar_t *name, const std::wstring &s, bool had)
        {
            LONG e = had ? RegSetValueExW(k.key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(s.c_str()),
                                          (DWORD)(s.size() + 1) * 2)
                         : RegDeleteValueW(k.key, name);
            Need(e == ERROR_SUCCESS || (!had && e == ERROR_FILE_NOT_FOUND), "Wallpaper mode write");
        };
        set(L"WallpaperStyle", r.style, r.hadStyle);
        set(L"TileWallpaper", r.tile, r.hadTile);
        Win(SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t *>(r.path.c_str()),
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE,
            "SPI_SETDESKWALLPAPER; saved recovery remains");
    }
    std::wstring Engine::Wallpaper(bool restore)
    {
        if (!args.allowWallpaper)
        {
            Log(restore ? "wallpaper dry-run restore" : "wallpaper dry-run apply");
            return L"dry-run：未调用壁纸 API；需 --allow-wallpaper";
        }
        auto saved = PathIn(L"wallpaper-original.json");
        Original original;
        if (restore)
        {
            Need(fs::exists(saved), "No saved original wallpaper in this round");
            auto j = ParseJson(Read(saved, MetadataLimit));
            original.path = j.At(L"Path").Str();
            original.style = j.At(L"Style").Str();
            original.tile = j.At(L"Tile").Str();
            original.hadStyle = j.At(L"HadStyle").Bool();
            original.hadTile = j.At(L"HadTile").Bool();
            SetWallpaper(original);
            return L"已请求恢复原壁纸与模式";
        }
        Need(hasCurrent, "No record");
        auto bmp = PrepareWallpaper();
        if (!fs::exists(saved))
        {
            original = CaptureWallpaper();
            TextNew(saved, "{\"Path\":" + Quote(original.path) + ",\"Style\":" + Quote(original.style) +
                               ",\"Tile\":" + Quote(original.tile) +
                               ",\"HadStyle\":" + (original.hadStyle ? "true" : "false") +
                               ",\"HadTile\":" + (original.hadTile ? "true" : "false") + "}\n");
        }
        Original next;
        next.path = bmp.wstring();
        next.style = L"10";
        next.tile = L"0";
        next.hadStyle = next.hadTile = true;
        SetWallpaper(next);
        return L"已请求设置壁纸；原路径与模式已保存";
    }
} // namespace v6
