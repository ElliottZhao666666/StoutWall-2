#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include "ProductBackend.hpp"
#include "SequenceRandom.hpp"
#include "TaskRunner.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
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

void CheckCurrent(const stoutwall::ProductSnapshot& snapshot, const fs::path& root,
                  const std::wstring& expected) {
    Require(snapshot.hasCurrent && Date(snapshot.current) == expected &&
                snapshot.config.currentWall.enddate == expected && snapshot.config.galleryCurrent == expected &&
                snapshot.current.favorite == static_cast<int32_t>(snapshot.config.currentWall.favorite),
            "current wallpaper metadata does not match the applied record");
    Require(snapshot.image == Read(root / L"Current_Wall.jpg"),
            "current wallpaper cache differs from the snapshot image");
}

void CheckNoRotation(const stoutwall::ProductSnapshot& before, const stoutwall::ProductSnapshot& after) {
    Require(after.intervalMs == 0 && after.hasCurrent == before.hasCurrent &&
                Date(after.current) == Date(before.current) && after.image == before.image &&
                after.config.renderPoint == before.config.renderPoint,
            "disabled rotation changed the wallpaper or cursor");
}

void CheckTick(const fs::path& runtime, const fs::path& fixture, const fs::path& root) {
    using stoutwall::ProductAction;
    CopyFixture(fixture, root / L"Gallery");
    stoutwall::ProductBackend backend;
    const fs::path config = root / L"StoutWall.cfg";
    auto snapshot = backend.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
    Require(snapshot.records.size() >= 3, "rotation fixture needs at least three records");
    const auto first = Date(snapshot.records[0]);
    const auto second = Date(snapshot.records[1]);
    const auto third = Date(snapshot.records[2]);
    Run(backend, ProductAction::AutoSave, 1);
    snapshot = Run(backend, ProductAction::Tick);
    Require(!snapshot.hasCurrent && !fs::exists(root / L"Current_Wall.jpg"),
            "isolated daily tick unexpectedly downloaded an image");

    Run(backend, ProductAction::BrowseSelect, 0, first);
    snapshot = Run(backend, ProductAction::BrowseApply);
    CheckCurrent(snapshot, root, first);
    Require(snapshot.config.mode == SWP_GALLERY && snapshot.config.renderPoint == 1 && snapshot.intervalMs > 0,
            "browse apply did not align the gallery cursor with the selected image");
    snapshot = Run(backend, ProductAction::Tick);
    CheckCurrent(snapshot, root, second);
    Require(snapshot.config.autoSave == 1 && snapshot.config.renderPoint == 2,
            "gallery tick with AutoSave enabled did not advance exactly one record");
    const auto paused = Run(backend, ProductAction::Pause, 1);
    CheckNoRotation(paused, Run(backend, ProductAction::Tick));
    Run(backend, ProductAction::Pause, 0);

    // Force the atomic cache replacement to fail while the previous image stays readable.
    const HANDLE held = CreateFileW((root / L"Current_Wall.jpg").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(held != INVALID_HANDLE_VALUE, "unable to lock the isolated current image");
    bool stepFailed = false, resizeFailed = false;
    try { Run(backend, ProductAction::Tick); }
    catch (const std::runtime_error&) { stepFailed = true; }
    try { Run(backend, ProductAction::ChangeSize, 1); }
    catch (const std::runtime_error&) { resizeFailed = true; }
    CloseHandle(held);
    Require(stepFailed && resizeFailed, "cache write failures were not reported");
    snapshot = Run(backend, ProductAction::AutoSave, 1);
    CheckCurrent(snapshot, root, second);
    Require(snapshot.config.renderPoint == 2 && snapshot.config.currentWall.size == 0,
            "failed wallpaper apply changed the cursor or configured size");
    snapshot = Run(backend, ProductAction::ChangeSize, 0); // Warm the image before holding its output file.
    const HANDLE transient = CreateFileW((root / L"Current_Wall.jpg").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(transient != INVALID_HANDLE_VALUE, "unable to hold image for transient contention");
    std::thread release([transient] { Sleep(100); CloseHandle(transient); });
    try { snapshot = Run(backend, ProductAction::ChangeSize, 0); }
    catch (...) { release.join(); throw; }
    release.join();
    CheckCurrent(snapshot, root, second);
    std::wcout << L"PASS: short image-file contention recovers; persistent locks preserve state.\n";
    snapshot = Run(backend, ProductAction::Tick);
    CheckCurrent(snapshot, root, third);

    Run(backend, ProductAction::ClearFavorites);
    Run(backend, ProductAction::BrowseSelect, 0, first);
    Run(backend, ProductAction::BrowseFavorite);
    snapshot = Run(backend, ProductAction::ChangeMode, SWP_FAVORITES);
    CheckCurrent(snapshot, root, first);
    Require(snapshot.config.mode == SWP_FAVORITES && snapshot.favorites == 1,
            "one favorite cannot be displayed in favorites mode");
    CheckNoRotation(snapshot, Run(backend, ProductAction::Tick));
    CheckNoRotation(snapshot, Run(backend, ProductAction::Next));
    Run(backend, ProductAction::BrowseSelect, 0, third);
    snapshot = Run(backend, ProductAction::BrowseFavorite);
    Require(snapshot.intervalMs > 0, "adding a second favorite did not enable rotation");
    snapshot = Run(backend, ProductAction::Tick);
    CheckCurrent(snapshot, root, third);
    Require(snapshot.config.autoSave == 1 && snapshot.current.favorite == 1,
            "favorites tick with AutoSave did not stay in the favorites sequence");
    snapshot = Run(backend, ProductAction::Tick);
    CheckCurrent(snapshot, root, first);

    snapshot = Run(backend, ProductAction::ClearFavorites);
    Require(snapshot.config.mode == SWP_GALLERY && snapshot.favorites == 0 && !snapshot.current.favorite,
            "clearing favorites did not return to gallery mode with updated metadata");
    snapshot = Run(backend, ProductAction::ChangeMode, SWP_FAVORITES);
    Require(snapshot.config.mode == SWP_GALLERY, "empty favorites mode replaced the current mode");
    snapshot = Run(backend, ProductAction::Tick);
    CheckCurrent(snapshot, root, second);
    SWP_Config disk{};
    disk.cbSize = sizeof(disk);
    Require(SWP_LoadConfig(config.c_str(), &disk) == ERROR_SUCCESS && disk.mode == SWP_GALLERY &&
                disk.currentWall.enddate == second && disk.renderPoint == 2,
            "successful rotation did not persist the applied wallpaper and cursor together");
    std::wcout << L"PASS: isolated gallery/favorites ticks, pause, sparse favorites, and failed apply state.\n";
}

void CheckSparseGallery(const fs::path& runtime, const fs::path& fixture, const fs::path& root) {
    using stoutwall::ProductAction;
    const fs::path gallery = root / L"Gallery";
    const fs::path config = root / L"StoutWall.cfg";
    CopyFixture(fixture, gallery);
    // Trim the copy using the library API, bypassing the product's two-record deletion guard.
    auto trim = [&](int count) {
        Require(Library_OpenW(runtime.c_str(), gallery.c_str(), 0) == 0, "could not open sparse gallery fixture");
        bool ok = true;
        for (int remaining = Library_Count(0); remaining > count; --remaining) {
            SWL_Record record{};
            if (Library_GetRecord(remaining, 0, &record) < 0 || Library_Delete(record.date) < 0) {
                ok = false;
                break;
            }
        }
        const int actual = Library_Count(0);
        Require(Library_Close() == 0 && ok && actual == count, "could not trim isolated gallery fixture");
    };
    trim(1);
    {
        stoutwall::ProductBackend backend;
        backend.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
        const auto single = Run(backend, ProductAction::ChangeMode, SWP_GALLERY);
        Require(single.total == 1 && single.config.mode == SWP_GALLERY, "single-image gallery mode was not entered");
        CheckCurrent(single, root, Date(single.records.front()));
        CheckNoRotation(single, Run(backend, ProductAction::Tick));
        CheckNoRotation(single, Run(backend, ProductAction::Previous));
    }
    trim(0);
    fs::remove(root / L"Current_Wall.jpg");
    stoutwall::ProductBackend backend;
    const auto empty = backend.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
    Require(empty.total == 0 && !empty.hasCurrent && !empty.hasSelected && empty.config.mode == SWP_GALLERY,
            "persisted gallery mode could not safely open an empty gallery");
    CheckNoRotation(empty, Run(backend, ProductAction::Tick));
    CheckNoRotation(empty, Run(backend, ProductAction::Next));
    Run(backend, ProductAction::ChangeMode, SWP_DAILY);
    const auto rejected = Run(backend, ProductAction::ChangeMode, SWP_GALLERY);
    Require(rejected.config.mode == SWP_DAILY && !rejected.hasCurrent,
            "switch to an empty gallery changed the active mode or current wallpaper");
    std::wcout << L"PASS: zero- and one-image galleries and rejected empty mode change.\n";
}

void CheckRelocationFailure(const fs::path& runtime, const fs::path& fixture, const fs::path& root) {
    using stoutwall::ProductAction;
    const auto gallery = root / L"Gallery";
    const auto target = root / L"Target";
    const auto config = root / L"StoutWall.cfg";
    const auto marker = target / L"unrelated.txt";
    CopyFixture(fixture, gallery);
    fs::create_directory(target);
    std::ofstream(marker) << "retain this file";
    const auto expectedMarker = Read(marker);
    auto backend = std::make_unique<stoutwall::ProductBackend>();
    const auto initial = backend->Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
    const auto date = Date(initial.records.front());
    const auto sourceFile = gallery / (L"Image_" + date + L".EDT");
    const auto conflictFile = target / sourceFile.filename();
    std::ofstream(conflictFile) << "retain this conflicting file";
    const auto expectedConflict = Read(conflictFile);
    const auto expectFailure = [&]() {
        try { Run(*backend, ProductAction::Relocate, 0, target.wstring()); }
        catch (const std::runtime_error&) { return; }
        throw std::runtime_error("expected relocation to fail");
    };
    expectFailure();
    Require(Read(conflictFile) == expectedConflict, "relocation overwrote an existing target file");
    fs::remove(conflictFile);

    const HANDLE held = CreateFileW(sourceFile.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(held != INVALID_HANDLE_VALUE, "unable to lock an isolated source file");
    try { expectFailure(); }
    catch (...) { CloseHandle(held); throw; }
    CloseHandle(held);
    Require(std::distance(fs::directory_iterator(target), fs::directory_iterator()) == 1 &&
                Read(marker) == expectedMarker,
            "failed relocation left partial files or changed the existing target contents");
    auto state = Run(*backend, ProductAction::BrowseSelect, 0, date);
    Require(state.hasSelected && state.total == initial.total && fs::equivalent(state.galleryDir, gallery),
            "failed relocation did not restore the original gallery session");
    SWP_Config disk{};
    disk.cbSize = sizeof(disk);
    Require(SWP_LoadConfig(config.c_str(), &disk) == ERROR_SUCCESS && fs::equivalent(disk.galleryDir, gallery),
            "failed relocation changed the persisted gallery location");

    state = Run(*backend, ProductAction::Relocate, 0, target.wstring());
    Require(state.total == initial.total && fs::equivalent(state.galleryDir, target) && Read(marker) == expectedMarker,
            "retry did not switch to the complete target gallery");
    backend.reset(); // The target index remains exclusively open until shutdown.
    for (const auto& entry : fs::directory_iterator(gallery)) {
        if (!entry.is_regular_file()) continue;
        Require(Read(entry.path()) == Read(target / entry.path().filename()), "migrated gallery bytes differ");
    }
    std::wcout << L"PASS: relocation conflict, locked-source rollback and byte-identical retry.\n";
}

void CheckMetadataDates() {
    const std::pair<const char*, DWORD> cases[] = {{"20261399", ERROR_INVALID_DATA},
        {"20260229", ERROR_INVALID_DATA}, {"19000229", ERROR_INVALID_DATA},
        {"20000229", ERROR_SUCCESS}, {"20261121", ERROR_SUCCESS}};
    for (const auto& [date, expected] : cases) {
        const auto json = std::string(R"({"title":"Fixture","urlbase":"/th?id=OHR.Fixture","enddate":")") + date + "\"}";
        SWN_Metadata metadata{};
        metadata.cbSize = sizeof(metadata);
        Require(SWN_ParseMetadata(SWN_CUSTOM, json.data(), static_cast<DWORD>(json.size()), &metadata) == expected,
                std::string("unexpected metadata parser status for ") + date);
    }
    std::wcout << L"PASS: metadata date validation, including century leap-year rules.\n";
}

void CheckLegacyRandomSequence() {
    std::mt19937 engine(0x5a17u);
    const auto sequence = stoutwall::BuildRandomSequenceIndices(3, 64, engine);
    Require(sequence.size() == 64, "legacy random sequence length changed");
    bool seen[3] = {};
    bool repeated = false;
    for (size_t i = 0; i < sequence.size(); ++i) {
        Require(sequence[i] < 3, "legacy random sequence selected an invalid record");
        if (i) Require(sequence[i] != sequence[i - 1], "legacy random sequence repeated adjacent records");
        if (seen[sequence[i]]) repeated = true;
        seen[sequence[i]] = true;
    }
    Require(repeated, "legacy random sampling unexpectedly became a no-repeat permutation");
    std::mt19937 pinnedEngine(0x62b1u);
    const auto pinned = stoutwall::BuildRandomSequenceIndices(5, 32, pinnedEngine, 17, 3);
    Require(pinned.size() == 32 && pinned[17] == 3, "random sequence failed to retain its pinned current record");
    for (size_t i = 1; i < pinned.size(); ++i)
        Require(pinned[i] != pinned[i - 1], "pinned random sequence repeated adjacent records");
    std::mt19937 singleEngine(7);
    const auto single = stoutwall::BuildRandomSequenceIndices(1, 1, singleEngine);
    Require(single.size() == 1 && single.front() == 0, "single-record random sequence must terminate safely");
    std::wcout << L"PASS: original random sampling allows non-adjacent repeats and rejects adjacent repeats.\n";
}

class LocalProxyProbe {
public:
    enum class Behavior { StallConnect, Reject, StallTls };

    explicit LocalProxyProbe(Behavior behavior = Behavior::StallConnect) : behavior_(behavior) {
        Require(WSAStartup(MAKEWORD(2, 2), &winsock_) == 0, "WSAStartup failed");
        try {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(listener_ != INVALID_SOCKET, "local proxy socket creation failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        Require(bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
                "local proxy bind failed");
        Require(listen(listener_, 1) == 0, "local proxy listen failed");
        int addressSize = sizeof(address);
        Require(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0,
                "local proxy port query failed");
        port_ = ntohs(address.sin_port);
        accepted_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(accepted_ && stop_, "local proxy event creation failed");
        worker_ = std::thread([this] { Serve(); });
        } catch (...) {
            Cleanup();
            throw;
        }
    }

    ~LocalProxyProbe() { Cleanup(); }

    LocalProxyProbe(const LocalProxyProbe&) = delete;
    LocalProxyProbe& operator=(const LocalProxyProbe&) = delete;

    unsigned short Port() const { return port_; }
    bool WaitAccepted(DWORD timeout) const { return WaitForSingleObject(accepted_, timeout) == WAIT_OBJECT_0; }

private:
    void Cleanup() {
        if (stop_) SetEvent(stop_);
        if (worker_.joinable()) worker_.join();
        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (accepted_) CloseHandle(accepted_);
        if (stop_) CloseHandle(stop_);
        WSACleanup();
    }

    void Serve() {
        // Poll the local listener so a pre-cancelled request needs no dummy
        // connection and cleanup never races a close against accept().
        for (;;) {
            if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) return;
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(listener_, &ready);
            timeval timeout{0, 100000};
            const int result = select(0, &ready, nullptr, nullptr, &timeout);
            if (result == SOCKET_ERROR) return;
            if (result > 0) break;
        }
        SOCKET client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        DWORD receiveTimeout = 2000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
        std::string headers;
        char block[1024];
        while (headers.find("\r\n\r\n") == std::string::npos && headers.size() < 16384) {
            const int received = recv(client, block, sizeof(block), 0);
            if (received <= 0) { closesocket(client); return; }
            headers.append(block, static_cast<size_t>(received));
        }
        if (behavior_ == Behavior::Reject) {
            static constexpr char response[] =
                "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client, response, static_cast<int>(sizeof(response) - 1), 0);
            SetEvent(accepted_);
        } else {
            if (behavior_ == Behavior::StallTls) {
                static constexpr char tunnel[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
                send(client, tunnel, static_cast<int>(sizeof(tunnel) - 1), 0);
                const int received = recv(client, block, sizeof(block), 0);
                if (received <= 0 || static_cast<unsigned char>(block[0]) != 0x16) {
                    closesocket(client);
                    return;
                }
            }
            SetEvent(accepted_);
            WaitForSingleObject(stop_, INFINITE);
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    WSADATA winsock_{};
    SOCKET listener_ = INVALID_SOCKET;
    HANDLE accepted_ = nullptr;
    HANDLE stop_ = nullptr;
    unsigned short port_ = 0;
    Behavior behavior_;
    std::thread worker_;
};

SWN_Options ProxyOptions(const LocalProxyProbe& proxy, DWORD timeoutMs, HANDLE cancel = nullptr) {
    SWN_Options options{};
    options.cbSize = sizeof(options);
    options.timeoutMs = timeoutMs;
    options.cancelEvent = cancel;
    swprintf_s(options.proxy, L"127.0.0.1:%u", proxy.Port());
    return options;
}

DWORD FetchThroughProxy(const SWN_Options& options, DWORD* status = nullptr) {
    SWN_Metadata metadata{};
    metadata.cbSize = sizeof(metadata);
    return SWN_FetchLatest(SWN_CN, &options, &metadata, status);
}

void CheckNetworkFailurePaths(const fs::path& root) {
    {
        std::wcout << L"CHECK: stalled local proxy deadline.\n";
        LocalProxyProbe proxy;
        const SWN_Options options = ProxyOptions(proxy, 1200);
        const auto start = std::chrono::steady_clock::now();
        const DWORD result = FetchThroughProxy(options);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::wcout << L"  Deadline elapsed: " << elapsed << L" ms; status " << result << L".\n";
        Require(proxy.WaitAccepted(0), "timeout probe did not reach its local proxy");
        Require(result == ERROR_TIMEOUT,
                "stalled proxy returned an unexpected timeout status: " + std::to_string(result));
        Require(elapsed >= 1000 && elapsed < 3500, "network total timeout missed its local bound: " +
                    std::to_string(elapsed) + " ms, result " + std::to_string(result));
    }
    {
        std::wcout << L"CHECK: local proxy rejection.\n";
        LocalProxyProbe proxy(LocalProxyProbe::Behavior::Reject);
        const SWN_Options options = ProxyOptions(proxy, 5000);
        DWORD status = 0;
        const DWORD result = FetchThroughProxy(options, &status);
        Require(proxy.WaitAccepted(0), "proxy rejection probe did not reach its local proxy");
        Require(result != ERROR_SUCCESS && status != 200,
                "proxy rejection was incorrectly accepted as a successful HTTP response");
    }
    {
        std::wcout << L"CHECK: cancellation with 15 second request budget.\n";
        LocalProxyProbe proxy;
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel != nullptr, "network cancellation event creation failed");
        const SWN_Options options = ProxyOptions(proxy, 15000, cancel);
        std::atomic<DWORD> result{ERROR_GEN_FAILURE};
        const auto start = std::chrono::steady_clock::now();
        std::thread request([&] { result.store(FetchThroughProxy(options), std::memory_order_release); });
        const bool reachedProxy = proxy.WaitAccepted(3000);
        SetEvent(cancel);
        request.join();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        CloseHandle(cancel);
        std::wcout << L"  Cancellation elapsed: " << elapsed << L" ms; status " << result.load() << L".\n";
        Require(reachedProxy, "cancellable request did not reach its local proxy");
        Require(result.load(std::memory_order_acquire) == ERROR_CANCELLED,
                "cancelled request returned status " + std::to_string(result.load()));
        Require(elapsed < 3500, "in-flight cancellation did not return within its bounded wait");
    }
    {
        std::wcout << L"CHECK: deadline while TLS negotiation is stalled.\n";
        LocalProxyProbe proxy(LocalProxyProbe::Behavior::StallTls);
        const SWN_Options options = ProxyOptions(proxy, 900);
        const auto start = std::chrono::steady_clock::now();
        const DWORD result = FetchThroughProxy(options);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        Require(proxy.WaitAccepted(0) && result == ERROR_TIMEOUT && elapsed < 3000,
                "TLS negotiation did not respect the total deadline");
    }
    const fs::path downloadRoot = root / L"network-failures";
    fs::create_directory(downloadRoot);
    const fs::path existing = downloadRoot / L"preserved.jpg";
    {
        std::ofstream output(existing, std::ios::binary);
        output << "existing destination must survive a failed or cancelled download";
        Require(static_cast<bool>(output), "download preservation fixture could not be written");
    }
    const auto original = Read(existing);
    SWN_Metadata metadata{};
    metadata.cbSize = sizeof(metadata);
    wcscpy_s(metadata.urlbase, L"/th?id=OHR.IsolatedFailureProbe");
    {
        LocalProxyProbe proxy;
        HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        Require(cancel != nullptr, "pre-cancel event creation failed");
        const SWN_Options options = ProxyOptions(proxy, 15000, cancel);
        const DWORD fetch = FetchThroughProxy(options);
        const DWORD download = SWN_DownloadImage(&metadata, 2, existing.c_str(), &options, nullptr, nullptr);
        CloseHandle(cancel);
        Require(fetch == ERROR_CANCELLED && download == ERROR_CANCELLED && !proxy.WaitAccepted(0),
                "pre-cancelled metadata/download request reached the proxy or returned the wrong error");
    }
    {
        LocalProxyProbe proxy(LocalProxyProbe::Behavior::Reject);
        const SWN_Options options = ProxyOptions(proxy, 3000);
        Require(SWN_DownloadImage(&metadata, 2, existing.c_str(), &options, nullptr, nullptr) != ERROR_SUCCESS,
                "rejected download unexpectedly succeeded");
    }
    {
        LocalProxyProbe proxy;
        const SWN_Options options = ProxyOptions(proxy, 600);
        Require(SWN_DownloadImage(&metadata, 2, existing.c_str(), &options, nullptr, nullptr) == ERROR_TIMEOUT,
                "download did not report the total deadline");
    }
    // Exercise callback lifetime repeatedly, including cancellation during TLS.
    for (int i = 0; i < 8; ++i) {
        LocalProxyProbe proxy(i % 2 ? LocalProxyProbe::Behavior::StallTls :
                                      LocalProxyProbe::Behavior::StallConnect);
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel != nullptr, "download cancellation event creation failed");
        const SWN_Options options = ProxyOptions(proxy, 15000, cancel);
        DWORD result = ERROR_GEN_FAILURE;
        std::thread request([&] {
            result = SWN_DownloadImage(&metadata, 2, existing.c_str(), &options, nullptr, nullptr);
        });
        const bool reachedProxy = proxy.WaitAccepted(3000);
        const auto start = std::chrono::steady_clock::now();
        SetEvent(cancel);
        request.join();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        CloseHandle(cancel);
        Require(reachedProxy && result == ERROR_CANCELLED && elapsed < 1500,
                "repeated download cancellation failed to release the pending request promptly");
        Require(Read(existing) == original, "failed download modified the existing destination");
    }
    Require(std::distance(fs::directory_iterator(downloadRoot), fs::directory_iterator{}) == 1,
            "failed download left a temporary file");
    {
        LocalProxyProbe proxy;
        const HWND window = CreateWindowExW(0, L"STATIC", L"StoutWall isolated task cancellation",
                                            0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                            GetModuleHandleW(nullptr), nullptr);
        Require(window != nullptr, "task cancellation message-only window creation failed");
        DWORD result = ERROR_GEN_FAILURE;
        bool reachedProxy = false;
        long long elapsed = 0;
        try {
            stoutwall::TaskRunner runner;
            Require(runner.Start(window), "task cancellation worker could not start");
            const SWN_Options options = ProxyOptions(proxy, 15000, runner.CancellationEvent());
            Require(runner.Submit(1, [&] {
                result = FetchThroughProxy(options);
                stoutwall::TaskResult completed;
                completed.code = static_cast<int32_t>(result);
                return completed;
            }) != 0, "task cancellation request was not submitted");
            reachedProxy = proxy.WaitAccepted(3000);
            const auto start = std::chrono::steady_clock::now();
            runner.Stop();
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            MSG pending{};
            Require(!PeekMessageW(&pending, window, stoutwall::WM_STOUTWALL_TASK_DONE,
                                  stoutwall::WM_STOUTWALL_TASK_DONE, PM_NOREMOVE),
                    "stopped worker left a task-result message or payload behind");
        } catch (...) {
            DestroyWindow(window);
            throw;
        }
        DestroyWindow(window);
        Require(reachedProxy && result == ERROR_CANCELLED && elapsed < 1500,
                "TaskRunner::Stop did not cancel and join its active network request promptly");
        std::wcout << L"PASS: worker stop cancelled the request, joined, and drained its result ("
                   << elapsed << L" ms).\n";
    }
    std::wcout << L"PASS: proxy rejection, CONNECT/TLS deadlines, metadata/download cancellation, "
                  L"destination preservation, and repeated callback cleanup.\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wcout << std::unitbuf;
    try {
        CheckMetadataDates();
        CheckLegacyRandomSequence();
        const fs::path runtime = fs::absolute(Argument(argc, argv, L"--runtime"));
        const fs::path fixture = fs::absolute(Argument(argc, argv, L"--fixture"));
        const fs::path root = fs::absolute(Argument(argc, argv, L"--root"));
        Require(!runtime.empty() && !fixture.empty() && !root.empty(),
                "usage: backend-integration-test --runtime DIR --fixture DIR --root DIR");
        Require(!fs::exists(root), "test root already exists: " + root.string());
        fs::create_directories(root);
        CheckNetworkFailurePaths(root);
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
            snapshot = Run(backend, stoutwall::ProductAction::Reshuffle);
            Require(snapshot.config.mode == SWP_DAILY && snapshot.config.changeRandom == 0 &&
                        !snapshot.hasCurrent,
                    "daily test-mode reshuffle must skip network refresh and preserve random preference");

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
            const std::wstring randomCurrentDate = Date(snapshot.current);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, firstDate);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
            Require(Date(snapshot.current) == randomCurrentDate,
                    "changing a browse favorite unexpectedly changed the current wallpaper");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
            Require(Date(snapshot.current) == randomCurrentDate,
                    "restoring a browse favorite unexpectedly changed the current wallpaper");
            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, firstDate);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseApply);
            Require(Date(snapshot.current) == firstDate,
                    "applying a selected record failed while random mode was active");
            snapshot = Run(backend, stoutwall::ProductAction::Next);
            Require(snapshot.hasCurrent, "random sequence lost current wallpaper after applying a browse selection");
            snapshot = Run(backend, stoutwall::ProductAction::Random, 0);
            Require(snapshot.config.changeRandom == 0, "random mode was not disabled");
            snapshot = Run(backend, stoutwall::ProductAction::Reshuffle);
            Require(snapshot.config.changeRandom == 0, "reshuffle changed the disabled random-mode preference");

            snapshot = Run(backend, stoutwall::ProductAction::ClearFavorites);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, firstDate);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_FAVORITES);
            Require(snapshot.config.mode == SWP_FAVORITES && snapshot.hasCurrent && Date(snapshot.current) == firstDate,
                    "single-favorite mode setup failed");
            snapshot = Run(backend, stoutwall::ProductAction::Favorite);
            Require(snapshot.config.mode == SWP_GALLERY && snapshot.hasCurrent && Date(snapshot.current) == firstDate,
                    "removing the last favorite did not safely return to gallery mode");

            const bool currentFavoriteBefore = snapshot.current.favorite != 0;
            snapshot = Run(backend, stoutwall::ProductAction::Favorite);
            Require(snapshot.hasCurrent && (snapshot.current.favorite != 0) != currentFavoriteBefore,
                    "current wallpaper favorite toggle did not change state");
            snapshot = Run(backend, stoutwall::ProductAction::Favorite);
            Require(snapshot.hasCurrent && (snapshot.current.favorite != 0) == currentFavoriteBefore,
                    "current wallpaper favorite toggle did not restore state");
            snapshot = Run(backend, stoutwall::ProductAction::ClearFavorites);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseSelect, 0, firstDate);
            snapshot = Run(backend, stoutwall::ProductAction::BrowseFavorite);
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
            Require(snapshot.config.mode == SWP_GALLERY,
                    "empty favorites mode replaced the current gallery mode");
            snapshot = Run(backend, stoutwall::ProductAction::ChangeMode, SWP_GALLERY);
            Require(snapshot.config.mode == SWP_GALLERY, "could not return to gallery mode");

            std::wcout << L"PASS: backend core actions, exports, migration, deletion, favorites, and themes.\n";
        }

        {
            stoutwall::ProductBackend reopened;
            snapshot = reopened.Initialize(runtime.wstring(), root.wstring(), config.wstring(), true, nullptr, L"");
            Require(snapshot.total >= 2 && snapshot.favorites == 0 && snapshot.config.mode == SWP_GALLERY,
                    "reopened state does not match the completed isolated round");
        }
        std::wcout << L"PASS: isolated configuration and gallery reopen after backend shutdown.\n";
        CheckTick(runtime, fixture, root / L"tick");
        CheckSparseGallery(runtime, fixture, root / L"sparse");
        CheckRelocationFailure(runtime, fixture, root / L"relocation-failure");
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
