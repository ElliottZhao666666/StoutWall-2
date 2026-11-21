#define WIN32_LEAN_AND_MEAN
#include "NetworkApi.h"
#include <winhttp.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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
}

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    try {
        const std::wstring rootArgument = Argument(argc, argv, L"--root");
        Require(!rootArgument.empty(), "usage: network-smoke-test --root NEW_DIRECTORY [--proxy HOST:PORT]");
        const fs::path root = fs::absolute(rootArgument);
        Require(!fs::exists(root), "test root must be a new directory");
        fs::create_directories(root);
        std::cout << "Retained network test root: " << root.u8string() << "\n";
        SWN_Options options{};
        options.cbSize = sizeof(options);
        options.timeoutMs = 30000;
        const std::wstring proxy = Argument(argc, argv, L"--proxy");
        Require(proxy.size() < SWN_TEXT_CAP, "proxy value is too long");
        wcscpy_s(options.proxy, proxy.c_str());
        const bool expectCertificateRejection = Argument(argc, argv, L"--expect-error") == L"certificate";
        for (DWORD source = SWN_GLOBAL; source <= SWN_CUSTOM; ++source) {
            SWN_Metadata metadata{};
            metadata.cbSize = sizeof(metadata);
            DWORD status = 0;
            auto start = std::chrono::steady_clock::now();
            const DWORD fetched = SWN_FetchLatest(source, &options, &metadata, &status);
            if (expectCertificateRejection) {
                Require(fetched == ERROR_WINHTTP_SECURE_FAILURE && status == 0,
                        "untrusted TLS certificate was not rejected: " + std::to_string(fetched));
                std::cout << "PASS: untrusted TLS certificate rejected without a successful HTTP response.\n";
                return 0;
            }
            Require(fetched == ERROR_SUCCESS && status == 200,
                    "source " + std::to_string(source) + " metadata failed: " + std::to_string(fetched) +
                        ", HTTP " + std::to_string(status));
            std::string date;
            for (const wchar_t digit : metadata.enddate) {
                if (!digit) break;
                date.push_back(static_cast<char>(digit));
            }
            std::cout << "PASS: source " << source << " metadata " << date << " ("
                       << std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start).count() << " ms).\n";
            for (DWORD size = 0; size < SWN_SIZE_COUNT; ++size) {
                const fs::path destination = root / (L"source-" + std::to_wstring(source) + L"-size-" +
                                                        std::to_wstring(size) + L".jpg");
                SWN_ImageInfo image{};
                image.cbSize = sizeof(image);
                status = 0;
                start = std::chrono::steady_clock::now();
                const DWORD downloaded = SWN_DownloadImage(&metadata, size, destination.c_str(),
                                                           &options, &image, &status);
                Require(downloaded == ERROR_SUCCESS && status == 200,
                        "source " + std::to_string(source) + " size " + std::to_string(size) +
                            " download failed: " + std::to_string(downloaded) + ", HTTP " + std::to_string(status));
                SWN_ImageInfo verified{};
                verified.cbSize = sizeof(verified);
                Require(SWN_ValidateImage(destination.c_str(), size, &verified) == ERROR_SUCCESS &&
                            verified.bytes == image.bytes && verified.width == image.width &&
                            verified.height == image.height && fs::file_size(destination) == image.bytes,
                        "saved image did not match the validated download");
                std::cout << "PASS: source " << source << " size " << size << " " << image.width
                           << "x" << image.height << " " << image.bytes << " bytes ("
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start).count() << " ms).\n";
            }
        }
        Require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 15,
                "successful downloads left unexpected output files");
        std::cout << "PASS: all three live sources and all fifteen JPEG downloads.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
