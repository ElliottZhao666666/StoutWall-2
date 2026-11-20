#pragma once

#include <windows.h>
#include <string>
#include <wincodec.h>

namespace v6
{
    // Resource names are the manifest's UTF-16 names, e.g. L"IMG/nopic".
    class AssetModule
    {
    public:
        AssetModule() noexcept = default;
        ~AssetModule() noexcept;
        AssetModule(const AssetModule &) = delete;
        AssetModule &operator=(const AssetModule &) = delete;

        bool Open(const std::wstring &dllPath) noexcept;
        void Close() noexcept;
        bool Bytes(const std::wstring &name, const BYTE **data, DWORD *size) const noexcept;
        HICON LoadIcon(const std::wstring &name, int desiredWidth, int desiredHeight) const noexcept;
        HRESULT LoadBitmap(const std::wstring &name, IWICBitmap **bitmap) const noexcept;

    private:
        HMODULE module_ = nullptr;
    };
}
