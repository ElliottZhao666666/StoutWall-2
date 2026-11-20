#include "Core.hpp"
#include <sstream>
#include <cwctype>
namespace v6
{
    static std::wstring Relative(const fs::path &root, const fs::path &path)
    {
        Need(Inside(root, path), "Gallery must remain in round");
        auto result = path.wstring().substr(root.wstring().size() + 1);
        std::replace(result.begin(), result.end(), L'\\', L'/');
        return result;
    }
    static std::vector<std::wstring> SplitDates(const std::wstring &text)
    {
        std::vector<std::wstring> result;
        std::wstringstream stream(text);
        std::wstring date;
        while (std::getline(stream, date, L','))
            if (!date.empty())
                result.push_back(date);
        return result;
    }
    static void ValidateTheme(const Settings &s)
    {
        Need(s.themeScope >= 0 && s.themeScope <= 2 && s.themeSize >= 1 && s.themeSize <= 5 &&
                 s.themeStyle >= 0 && s.themeStyle <= 4 && s.themeSeconds >= 60 && s.themeSeconds <= 86400,
             "Invalid theme settings");
        Need(s.themeColor.size() == 8 && std::all_of(s.themeColor.begin(), s.themeColor.end(),
                                                     [](wchar_t c)
                                                     {
                                                         return iswxdigit(c) != 0;
                                                     }),
             "Theme color AARRGGBB required");
    }
    Settings ReadSettings(const fs::path &path)
    {
        auto j = ParseJson(Read(path, MetadataLimit));
        Settings s;
        s.date = j.At(L"Date").Str();
        s.source = j.At(L"Source").Str();
        s.paused = j.At(L"Paused").Bool();
        s.random = j.At(L"Random").Bool();
        s.seconds = j.At(L"Seconds").Int();
        s.size = j.At(L"Size").Int();
        s.viewMode = j.object.count(L"ViewMode") ? j.At(L"ViewMode").Int() : 0;
        s.galleryRelative = j.At(L"GalleryRelative").Str();
        s.favoritesOnly = j.At(L"FavoritesOnly").Bool();
        s.themeDates = j.At(L"ThemeDates").Str();
        s.themeColor = j.At(L"ThemeColor").Str();
        s.themeScope = j.At(L"ThemeScope").Int();
        s.themeSize = j.At(L"ThemeSize").Int();
        s.themeStyle = j.At(L"ThemeStyle").Int();
        s.themeSeconds = j.At(L"ThemeSeconds").Int();
        s.themeShuffle = j.At(L"ThemeShuffle").Bool();
        s.themeTransparent = j.At(L"ThemeTransparent").Bool();
        Endpoint(s.source);
        Need(s.viewMode >= 0 && s.viewMode <= 3 && s.size >= 1 && s.size <= 5 && s.seconds >= 5 &&
                 s.seconds <= 86400,
             "Invalid settings");
        ValidateTheme(s);
        Need(!s.galleryRelative.empty() && !fs::path(s.galleryRelative).is_absolute() &&
                 s.galleryRelative.find(L':') == s.galleryRelative.npos,
             "Invalid gallery relative config");
        return s;
    }
    fs::path Engine::ActiveWrite() const
    {
        return PathIn(state.galleryRelative);
    }
    void ValidateGallery(Gallery &gallery)
    {
        for (auto &date : gallery.Dates())
        {
            gallery.Check(date);
            for (int size = 1; size <= 5; size++)
                gallery.Get(date, size);
        }
    }
    void Engine::Initialize()
    {
        auto shared = Snapshot(args.readonlyDir);
        auto source = args.readonlyDir;
        std::wstring configHash;
        if (!args.resumeFrom.empty())
        {
            configHash = FileHash(args.resumeFrom / L"settings.json");
            state = ReadSettings(args.resumeFrom / L"settings.json");
            source = Full((args.resumeFrom / state.galleryRelative).wstring());
            Need(Inside(args.resumeFrom, source), "Resume source escapes round");
        }
        else
            state.source = args.source;
        auto before = Snapshot(source);
        fs::create_directories(args.write.parent_path());
        CopyTree(source, args.write);
        Need(before == Snapshot(args.write), "Private copy differs from source");
        ValidateGallery(gallery);
        Need(before == Snapshot(source) && shared == Snapshot(args.readonlyDir), "Startup changed input");
        if (!args.resumeFrom.empty())
        {
            Need(configHash == FileHash(args.resumeFrom / L"settings.json"),
                 "Resume config changed during copy");
            TextNew(PathIn(L"resume-provenance.json"),
                    "{\"SourceRound\":" + Quote(args.resumeFrom.wstring()) + ",\"SourceGallery\":" +
                        Quote(source.wstring()) + ",\"SettingsSHA256\":" + Quote(configHash) +
                        ",\"GallerySnapshot\":" + Quote(Wide(before)) + ",\"SourceUnchanged\":true}\n");
        }
        TextNew(PathIn(L"source-before.txt"), shared);
        state.galleryRelative = Relative(args.out, args.write);
        RefreshSelection();
        Save();
        Log("startup private copy validated; no network/wallpaper action");
    }
    std::vector<std::wstring> Engine::VisibleDates()
    {
        return gallery.Dates(state.favoritesOnly);
    }
    void Engine::RefreshSelection()
    {
        auto dates = VisibleDates();
        if (dates.empty())
        {
            hasCurrent = false;
            current = Record{};
            state.date = L"";
            return;
        }
        Select(std::find(dates.begin(), dates.end(), state.date) != dates.end() ? state.date : dates.front());
    }
    void Engine::Filter(bool favorites)
    {
        state.favoritesOnly = favorites;
        RefreshSelection();
        Save();
    }
    void Engine::Adopt(const fs::path &path, const std::wstring &date)
    {
        Gallery next(api, path);
        auto dates = next.Dates(state.favoritesOnly);
        auto selected = std::find(dates.begin(), dates.end(), date) != dates.end() ? date
                        : dates.empty()                                            ? L""
                                                                                   : dates.front();
        Record record = selected.empty() ? Record{} : next.Get(selected, state.size);
        auto oldState = state;
        auto old = current;
        bool oldHas = hasCurrent;
        state.galleryRelative = Relative(args.out, path);
        current = std::move(record);
        hasCurrent = !selected.empty();
        fs::path nextPath = path;
        try
        {
            Save();
        }
        catch (...)
        {
            state = std::move(oldState);
            current = std::move(old);
            hasCurrent = oldHas;
            throw;
        }
        gallery.SwapPath(nextPath);
    }
    void Engine::AdoptImport(const Online &online)
    {
        Need(!online.importedGallery.empty(), "No verified import");
        bool filter = state.favoritesOnly;
        state.favoritesOnly = false;
        try
        {
            Adopt(online.importedGallery, online.date);
        }
        catch (...)
        {
            state.favoritesOnly = filter;
            throw;
        }
        Log(std::string(online.isReplay ? "OFFLINE_REPLAY " : "HTTPS ") +
            "five-image import committed and reopened date=" + Utf8(online.date));
    }
    fs::path Engine::Migrate(const std::wstring &name)
    {
        Need(!name.empty() && std::all_of(name.begin(), name.end(),
                                          [](wchar_t c)
                                          {
                                              return iswalnum(c) || c == L'-';
                                          }),
             "Migration name");
        auto dest = PathIn(name), source = ActiveWrite();
        auto before = Snapshot(source);
        CopyTree(source, dest);
        Need(Snapshot(dest) == before, "Migration byte mismatch");
        Gallery copy(api, dest);
        ValidateGallery(copy);
        Need(Snapshot(dest) == before && Snapshot(source) == before, "Migration validation changed bytes");
        Log("migration fully validated before config commit");
        Adopt(dest, hasCurrent ? current.date : L"");
        return dest;
    }
    static std::string GalleryText(const std::wstring &text, const char *field, size_t limit)
    {
        if (!std::all_of(text.begin(), text.end(), [](wchar_t c) { return !iswcntrl(c); }))
            throw std::runtime_error(std::string("Unsafe INI metadata: ") + field);
        auto utf8 = Utf8(text);
        if (utf8.size() + 3 > limit)
            throw std::runtime_error(std::string("Gallery field ") + field + " bytes=" +
                                     std::to_string(utf8.size() + 3) + " limit=" + std::to_string(limit));
        return utf8;
    }
    static const UINT VariantWidths[] = {3840, 1920, 1920, 1024, 1080};
    static const UINT VariantHeights[] = {2160, 1200, 1080, 768, 1920};
    static constexpr wchar_t ReceiptSchema[] = L"stoutwall-gallery-import-1";
    fs::path ImportReceiptPath(const fs::path &root, const std::wstring &date)
    {
        Need(date.size() == 8 && std::all_of(date.begin(), date.end(), [](wchar_t c) {
                 return c >= L'0' && c <= L'9'; }), "Invalid receipt date");
        return root / L"stoutwall-imports" / date;
    }
    void ArchiveImportReceipt(const fs::path &path, const fs::path &out)
    {
        NoLinks(path);
        if (fs::is_directory(path))
            fs::rename(path, out / (L"deleted-import-" + Id()));
    }
    static std::wstring ImportDescription(const Online &online)
    {
        return !online.description.empty() ? online.description
            : online.isReplay ? L"Explicit offline replay; five center-cropped JPEG variants"
                              : L"Bing HTTPS; five center-cropped JPEG variants";
    }
    static void CreateImportReceipt(const fs::path &root, const fs::path &images, const Online &online)
    {
        auto path = ImportReceiptPath(root, online.date);
        auto raw = Read(online.cache / L"metadata.json", MetadataLimit);
        Need(Hash(raw) == online.metadataHash, "Source response changed before import receipt");
        std::string variants;
        for (int i = 1; i <= 5; ++i)
            variants += (i == 1 ? "" : ",") + Quote(FileHash(images / (std::to_wstring(i) + L".jpg")));
        if (!fs::is_directory(path.parent_path()))
            NewDir(path.parent_path());
        NewDir(path);
        WriteNew(path / L"metadata.json", raw);
        TextNew(path / L"receipt.json", "{\"Schema\":" + Quote(ReceiptSchema) + ",\"Date\":" +
                    Quote(online.date) + ",\"Source\":" + Quote(online.source) +
                    ",\"Replay\":" + (online.isReplay ? "true" : "false") +
                    ",\"MetadataSHA256\":" + Quote(online.metadataHash) +
                    ",\"ImageSHA256\":" + Quote(Hash(online.bytes)) + ",\"VariantSHA256\":[" + variants + "]}\n");
    }
    std::string VerifyImportReceipt(Gallery &gallery, const fs::path &root, const std::wstring &date)
    {
        auto path = ImportReceiptPath(root, date);
        auto ev = ParseJson(Read(path / L"receipt.json", 4096));
        auto raw = Read(path / L"metadata.json", MetadataLimit);
        const auto &hashes = ev.At(L"VariantSHA256");
        auto metaHash = ev.At(L"MetadataSHA256").Str();
        Need(ev.At(L"Schema").Str() == ReceiptSchema && ev.At(L"Date").Str() == date &&
                 metaHash == Hash(raw) && hashes.type == Json::Type::Array && hashes.array.size() == 5,
             "Import receipt binding mismatch");
        auto source = ev.At(L"Source").Str();
        Endpoint(source);
        auto original = ParseBing(raw, source);
        original.isReplay = ev.At(L"Replay").Bool();
        Need(original.date == date, "Response date does not match receipt");
        auto record = gallery.Get(date);
        auto base = gallery.UrlBase(date);
        Need(record.title == original.title && record.description == ImportDescription(original) &&
                 record.author == original.author &&
                 record.link == (original.link.empty() ? original.url : original.link) &&
                 base == (original.urlbase.empty() ? original.url : original.urlbase),
             "Import receipt does not match DLL metadata");
        std::string variants;
        for (int size = 1; size <= 5; ++size)
        {
            auto image = gallery.Get(date, size);
            auto hash = Hash(image.image);
            Need(hash == hashes.array[size - 1].Str() && image.bitmap.width == VariantWidths[size - 1] &&
                     image.bitmap.height == VariantHeights[size - 1], "Import variant does not match receipt");
            variants += std::string(size == 1 ? "" : ",") + "{\"Size\":" + std::to_string(size) +
                        ",\"Width\":" + std::to_string(image.bitmap.width) +
                        ",\"Height\":" + std::to_string(image.bitmap.height) + ",\"SHA256\":" + Quote(hash) +
                        ",\"Bytes\":" + std::to_string(image.image.size()) + "}";
        }
        return "{\"Date\":" + Quote(date) + ",\"Source\":" + Quote(source) +
               ",\"MetadataSHA256\":" + Quote(metaHash) + ",\"ImageSHA256\":" + Quote(ev.At(L"ImageSHA256").Str()) +
               ",\"Images\":[" + variants + "],\"Metadata\":{\"Title\":" + Quote(record.title) +
               ",\"Description\":" + Quote(record.description) + ",\"Author\":" + Quote(record.author) +
               ",\"Link\":" + Quote(record.link) + ",\"UrlBase\":" + Quote(base) +
               ",\"Copyright\":" + Quote(original.copyright) + ",\"Wp\":" + Quote(original.wp) + "}}";
    }
    static void JpegVariant(const Decoded &bitmap, UINT width, UINT height, const fs::path &file)
    {
        ComPtr<IWICImagingFactory> factory;
        Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory))),
             "WIC factory");
        UINT cw = bitmap.width, ch = bitmap.height;
        double ratio = (double)width / height;
        if ((double)cw / ch > ratio)
            cw = std::max<UINT>(1, (UINT)(ch * ratio));
        else
            ch = std::max<UINT>(1, (UINT)(cw / ratio));
        WICRect rect{(INT)((bitmap.width - cw) / 2), (INT)((bitmap.height - ch) / 2), (INT)cw, (INT)ch};
        ComPtr<IWICBitmapClipper> crop;
        ComPtr<IWICBitmapScaler> scaled;
        ComPtr<IWICFormatConverter> convert;
        Need(SUCCEEDED(factory->CreateBitmapClipper(&crop)) &&
                 SUCCEEDED(crop->Initialize(bitmap.source.Get(), &rect)) &&
                 SUCCEEDED(factory->CreateBitmapScaler(&scaled)) &&
                 SUCCEEDED(scaled->Initialize(crop.Get(), width, height, WICBitmapInterpolationModeFant)) &&
                 SUCCEEDED(factory->CreateFormatConverter(&convert)) &&
                 SUCCEEDED(convert->Initialize(scaled.Get(), GUID_WICPixelFormat24bppBGR,
                                               WICBitmapDitherTypeNone, nullptr, 0,
                                               WICBitmapPaletteTypeCustom)),
             "WIC crop scale convert");
        NoLinks(file);
        Need(!fs::exists(file), "Variant output conflict");
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        Need(SUCCEEDED(factory->CreateStream(&stream)) &&
                 SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                 SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) &&
                 SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                 SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)),
             "WIC JPEG encoder");
        PROPBAG2 property{};
        property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{};
        value.vt = VT_R4;
        value.fltVal = 0.85f;
        Need(SUCCEEDED(properties->Write(1, &property, &value)) &&
                 SUCCEEDED(frame->Initialize(properties.Get())) && SUCCEEDED(frame->SetSize(width, height)),
             "JPEG quality dimensions");
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        Need(SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) &&
                 SUCCEEDED(frame->WriteSource(convert.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
                 SUCCEEDED(encoder->Commit()),
             "JPEG commit");
    }
    Online Engine::StageImport(Online online, const fs::path &source)
    {
        Need(online.date.size() == 8 && std::all_of(online.date.begin(), online.date.end(),
                                                    [](wchar_t c)
                                                    {
                                                        return c >= L'0' && c <= L'9';
                                                    }),
             "Invalid Bing gallery date");
        SYSTEMTIME day{};
        day.wYear = (WORD)std::stoi(online.date.substr(0, 4));
        day.wMonth = (WORD)std::stoi(online.date.substr(4, 2));
        day.wDay = (WORD)std::stoi(online.date.substr(6, 2));
        FILETIME valid;
        Need(SystemTimeToFileTime(&day, &valid) != FALSE, "Invalid calendar date");
        Gallery original(api, source);
        auto dates = original.Dates();
        Need(std::find(dates.begin(), dates.end(), online.date) == dates.end(),
             "Date exists; will not overwrite private record");
        const auto urlbase = GalleryText(online.urlbase.empty() ? online.url : online.urlbase, "urlbase", 130),
                   title = GalleryText(online.title, "title", 50),
                   description = GalleryText(ImportDescription(online), "description", 200),
                   author = GalleryText(online.author, "author", 100),
                   link = GalleryText(online.link.empty() ? online.url : online.link, "copyrightlink", 200);
        auto dir = PathIn(L"import-" + Id());
        NewDir(dir);
        auto target = dir / L"gallery";
        auto before = Snapshot(source);
        CopyTree(source, target);
        auto images = dir / L"images";
        NewDir(images);
        const char *keys[] = {"UHD", "1920x1200", "1920x1080", "1024x768", "1080x1920"};
        auto bitmap = Decode(online.bytes);
        std::string ini = "[Wallpaper]\r\nurlbase=u8:" + urlbase + "\r\ntitle=u8:" + title +
                          "\r\ndescription=u8:" + description + "\r\nauthor=u8:" + author +
                          "\r\ncopyrightlink=u8:" + link + "\r\n[Images]\r\n";
        for (int i = 0; i < 5; i++)
        {
            auto file = images / (std::to_wstring(i + 1) + L".jpg");
            JpegVariant(bitmap, VariantWidths[i], VariantHeights[i], file);
            ini += std::string(keys[i]) + "=" + Ansi(file.wstring()) + "\r\n";
        }
        auto manifest = dir / L"import.ini";
        TextNew(manifest, ini);
        Gallery imported(api, target);
        imported.Use(true,
                     [&]
                     {
                         int rc = api.importRecord(Ansi(online.date).c_str(), Ansi(manifest.wstring()).c_str());
                         if (rc != 0)
                             throw std::runtime_error("Gallery_Import=" + std::to_string(rc) +
                                                      "; date=" + Utf8(online.date) +
                                                      "; manifest=" + Utf8(manifest.wstring()));
                         return 0;
                     });
        ValidateGallery(imported);
        auto record = imported.Get(online.date);
        auto storedBase = imported.UrlBase(online.date);
        Need(storedBase == Wide(urlbase) && record.title == Wide(title) && record.description == Wide(description) &&
                 record.author == Wide(author) && record.link == Wide(link),
             "Import metadata roundtrip");
        CreateImportReceipt(target, images, online);
        VerifyImportReceipt(imported, target, online.date);
        Need(before == Snapshot(source), "Import changed source");
        online.importedGallery = target;
        TextNew(dir / L"import-validation.txt",
                "five_generated_JPEGs=PASS\nfull_gallery_reopen=PASS\nsource_unchanged=PASS\nimport_receipt=PASS\n");
        return online;
    }
    void Engine::ThemeSelectCurrent()
    {
        Need(hasCurrent, "No record");
        auto dates = SplitDates(state.themeDates);
        auto found = std::find(dates.begin(), dates.end(), current.date);
        if (found != dates.end())
            dates.erase(found);
        else
            dates.push_back(current.date);
        std::sort(dates.begin(), dates.end());
        state.themeDates = L"";
        for (auto &date : dates)
        {
            if (!state.themeDates.empty())
                state.themeDates += L",";
            state.themeDates += date;
        }
        state.themeScope = 2;
        Save();
    }
    struct NativeHandle
    {
        HANDLE h = INVALID_HANDLE_VALUE;
        ~NativeHandle()
        {
            if (h != INVALID_HANDLE_VALUE && h)
                CloseHandle(h);
        }
    };
    static void MakeCab(const fs::path &dir)
    {
        wchar_t system[MAX_PATH];
        Need(GetSystemDirectoryW(system, MAX_PATH) > 0, "System directory");
        auto exe = fs::path(system) / L"makecab.exe";
        auto temp = dir / L"tmp";
        NewDir(temp);
        NativeHandle stdoutFile, stderrFile, stdinFile;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        stdoutFile.h = CreateFileW((dir / L"makecab.stdout.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                   CREATE_NEW, 0, nullptr);
        stderrFile.h = CreateFileW((dir / L"makecab.stderr.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                   CREATE_NEW, 0, nullptr);
        stdinFile.h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr);
        Win(stdoutFile.h != INVALID_HANDLE_VALUE && stderrFile.h != INVALID_HANDLE_VALUE &&
                stdinFile.h != INVALID_HANDLE_VALUE,
            "makecab log handles");
        LPWCH inherited = GetEnvironmentStringsW();
        Need(inherited != nullptr, "Child environment");
        std::vector<wchar_t> env;
        std::vector<std::wstring> variables;
        for (auto p = inherited; *p; p += wcslen(p) + 1)
        {
            std::wstring value = p;
            auto equals = value.find(L'=');
            std::wstring key = value.substr(0, equals);
            for (auto &c : key)
                c = (wchar_t)towupper(c);
            if (key != L"TMP" && key != L"TEMP")
            {
                variables.push_back(value);
            }
        }
        FreeEnvironmentStringsW(inherited);
        for (auto key : {L"TMP=", L"TEMP="})
        {
            auto value = std::wstring(key) + temp.wstring();
            variables.push_back(value);
        }
        std::sort(variables.begin(), variables.end(),
                  [](const std::wstring &a, const std::wstring &b)
                  {
                      return _wcsicmp(a.c_str(), b.c_str()) < 0;
                  });
        for (auto &value : variables)
        {
            env.insert(env.end(), value.begin(), value.end());
            env.push_back(0);
        }
        env.push_back(0);
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = stdoutFile.h;
        startup.hStdError = stderrFile.h;
        startup.hStdInput = stdinFile.h;
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + exe.wstring() + L"\" /F pack.ddf";
        Win(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(), dir.c_str(), &startup,
                           &process) != FALSE,
            "CreateProcess makecab");
        NativeHandle hProcess, hThread;
        hProcess.h = process.hProcess;
        hThread.h = process.hThread;
        if (WaitForSingleObject(hProcess.h, 120000) != WAIT_OBJECT_0)
        {
            TerminateProcess(hProcess.h, 124);
            WaitForSingleObject(hProcess.h, 10000);
            throw std::runtime_error("makecab timeout; partial output retained");
        }
        DWORD code = 0;
        Win(GetExitCodeProcess(hProcess.h, &code) != FALSE, "makecab exit");
        TextNew(dir / L"makecab.exit.txt", std::to_string(code) + "\n");
        Need(code == 0, "makecab failed; logs retained");
    }
    fs::path Engine::Theme(const std::wstring &name)
    {
        Need(!name.empty() && name.size() <= 80 &&
                 std::any_of(name.begin(), name.end(),
                             [](wchar_t c)
                             {
                                 return !iswspace(c);
                             }) &&
                 std::all_of(name.begin(), name.end(),
                             [](wchar_t c)
                             {
                                 return !iswcntrl(c) && c != L'[' && c != L']' && c != L'=';
                             }),
             "Single-line theme name required");
        ValidateTheme(state);
        auto all = gallery.Dates();
        auto dates = state.themeScope == 0   ? all
                     : state.themeScope == 1 ? gallery.Dates(true)
                                             : SplitDates(state.themeDates);
        auto distinct = dates;
        std::sort(distinct.begin(), distinct.end());
        Need(dates.size() >= 2 && std::adjacent_find(distinct.begin(), distinct.end()) == distinct.end() &&
                 std::all_of(dates.begin(), dates.end(),
                             [&](const std::wstring &d)
                             {
                                 return std::find(all.begin(), all.end(), d) != all.end();
                             }),
             "Select two or more distinct existing theme images");
        auto dir = PathIn(L"theme-" + Id());
        NewDir(dir);
        NewDir(dir / L"DesktopBackground");
        for (auto &date : dates)
            gallery.Export(date, state.themeSize, dir / L"DesktopBackground" / (date + L".jpg"));
        const int styles[] = {0, 2, 6, 10, 0};
        std::wstring text =
            L"[Theme]\r\nDisplayName=" + name +
            L"\r\n\r\n[Control Panel\\Desktop]\r\nWallpaper=DesktopBackground\\" + dates.front() +
            L".jpg\r\nTileWallpaper=" + (state.themeStyle == 4 ? L"1" : L"0") + L"\r\nWallpaperStyle=" +
            std::to_wstring(styles[state.themeStyle]) +
            L"\r\nPattern=\r\n\r\n[Slideshow]\r\nImagesRootPath=DesktopBackground\r\nInterval=" +
            std::to_wstring((long long)state.themeSeconds * 1000) + L"\r\nShuffle=" +
            (state.themeShuffle ? L"1" : L"0") + L"\r\n";
        for (size_t i = 0; i < dates.size(); i++)
            text += L"Item" + std::to_wstring(i) + L"Path=DesktopBackground\\" + dates[i] + L".jpg\r\n";
        auto color = state.themeColor;
        for (auto &c : color)
            c = (wchar_t)towupper(c);
        text += L"\r\n[VisualStyles]\r\nPath=%SystemRoot%\\resources\\themes\\Aero\\Aero."
                L"msstyles\r\nColorStyle=NormalColor\r\nSize=NormalSize\r\nColorizationColor=0X" +
                color + L"\r\nTransparency=" + (state.themeTransparent ? L"1" : L"0") +
                L"\r\nComposition=1\r\n\r\n[MasterThemeSelector]\r\nMTSM=DABJDKT\r\n";
        Bytes bytes = {0xff, 0xfe};
        const auto *p = reinterpret_cast<const BYTE *>(text.data());
        bytes.insert(bytes.end(), p, p + text.size() * 2);
        WriteNew(dir / L"StoutWall.theme", bytes);
        std::string ddf =
            ".OPTION EXPLICIT\r\n.Set CabinetNameTemplate=StoutWall.themepack\r\n.Set "
            "DiskDirectoryTemplate=.\r\n.Set InfFileName=pack.inf\r\n.Set RptFileName=pack.rpt\r\n.Set "
            "MaxDiskSize=0\r\n.Set Cabinet=on\r\n.Set Compress=on\r\n.Set CompressionType=LZX\r\n.Set "
            "CompressionMemory=21\r\n\"StoutWall.theme\" \"StoutWall.theme\"\r\n";
        for (auto &date : dates)
            ddf += "\"DesktopBackground\\" + Utf8(date) + ".jpg\" \"DesktopBackground\\" + Utf8(date) +
                   ".jpg\"\r\n";
        TextNew(dir / L"pack.ddf", ddf);
        MakeCab(dir);
        auto cab = Read(dir / L"StoutWall.themepack");
        Need(cab.size() > 36 && std::string(cab.begin(), cab.begin() + 4) == "MSCF", "Not a real CAB");
        TextNew(dir / L"README.txt",
                "Real CAB .themepack; relative DesktopBackground references; never applied. Windows 7 theme "
                "semantics; newer-only settings are not offered.\n");
        return dir;
    }
} // namespace v6
