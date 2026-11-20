#include "Assets.hpp"
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace v6
{
    using Microsoft::WRL::ComPtr;

    namespace
    {
#pragma pack(push, 2)
        struct IconDirectory
        {
            WORD reserved;
            WORD type;
            WORD count;
        };

        struct IconEntry
        {
            BYTE width;
            BYTE height;
            BYTE colorCount;
            BYTE reserved;
            WORD planes;
            WORD bitCount;
            DWORD bytesInResource;
            DWORD imageOffset;
        };
#pragma pack(pop)

        int IconDimension(BYTE value) noexcept
        {
            return value == 0 ? 256 : static_cast<int>(value);
        }
    }

    AssetModule::~AssetModule() noexcept
    {
        Close();
    }

    bool AssetModule::Open(const std::wstring &dllPath) noexcept
    {
        Close();
        module_ = LoadLibraryExW(dllPath.c_str(), nullptr,
                                 LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        return module_ != nullptr;
    }

    void AssetModule::Close() noexcept
    {
        if (module_)
        {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    bool AssetModule::Bytes(const std::wstring &name, const BYTE **data, DWORD *size) const noexcept
    {
        if (!data || !size)
            return false;
        *data = nullptr;
        *size = 0;
        if (!module_)
            return false;

        auto upper = [](std::wstring value) {
            for (wchar_t &character : value)
            {
                if (character >= L'a' && character <= L'z')
                    character = static_cast<wchar_t>(character - (L'a' - L'A'));
            }
            return value;
        };
        auto withQuotes = [](const std::wstring &value) {
            return std::wstring(L"\"") + value + L"\"";
        };
        const std::wstring normalized = upper(name);
        const std::wstring quoted = withQuotes(name);
        const std::wstring quotedNormalized = withQuotes(normalized);
        const std::wstring candidates[] = {name, normalized, quoted, quotedNormalized};
        HRSRC resource = nullptr;
        for (const std::wstring &candidate : candidates)
        {
            resource = FindResourceW(module_, candidate.c_str(), RT_RCDATA);
            if (resource)
                break;
        }
        if (!resource)
            return false;
        const DWORD length = SizeofResource(module_, resource);
        const HGLOBAL loaded = LoadResource(module_, resource);
        if (!loaded || length == 0)
            return false;
        const auto *bytes = static_cast<const BYTE *>(LockResource(loaded));
        if (!bytes)
            return false;
        *data = bytes;
        *size = length;
        return true;
    }

    HICON AssetModule::LoadIcon(const std::wstring &name, int desiredWidth, int desiredHeight) const noexcept
    {
        const BYTE *bytes = nullptr;
        DWORD size = 0;
        if (!Bytes(name, &bytes, &size) || size < sizeof(IconDirectory))
            return nullptr;

        IconDirectory directory{};
        std::memcpy(&directory, bytes, sizeof(directory));
        if (directory.reserved != 0 || directory.type != 1 || directory.count == 0)
            return nullptr;
        const size_t entriesBytes = static_cast<size_t>(directory.count) * sizeof(IconEntry);
        if (entriesBytes > size - sizeof(IconDirectory))
            return nullptr;

        IconEntry best{};
        bool haveBest = false;
        long bestScore = (std::numeric_limits<long>::max)();
        for (WORD i = 0; i < directory.count; ++i)
        {
            IconEntry entry{};
            std::memcpy(&entry, bytes + sizeof(IconDirectory) + i * sizeof(IconEntry), sizeof(entry));
            if (entry.imageOffset > size || entry.bytesInResource > size - entry.imageOffset)
                continue;
            const long width = static_cast<long>(IconDimension(entry.width));
            const long height = static_cast<long>(IconDimension(entry.height));
            const long widthDelta = width >= desiredWidth ? width - desiredWidth : desiredWidth - width;
            const long heightDelta = height >= desiredHeight ? height - desiredHeight : desiredHeight - height;
            const long score = widthDelta + heightDelta;
            if (!haveBest || score < bestScore)
            {
                bestScore = score;
                best = entry;
                haveBest = true;
            }
        }
        if (!haveBest)
            return nullptr;
        const BYTE *image = bytes + best.imageOffset;
        const DWORD imageSize = best.bytesInResource;
        const HICON icon = CreateIconFromResourceEx(const_cast<BYTE *>(image), imageSize, TRUE,
                                                     0x00030000, desiredWidth, desiredHeight,
                                                     LR_DEFAULTCOLOR);
        return icon;
    }

    HRESULT AssetModule::LoadBitmap(const std::wstring &name, IWICBitmap **bitmap) const noexcept
    {
        if (!bitmap)
            return E_POINTER;
        *bitmap = nullptr;

        const BYTE *resourceBytes = nullptr;
        DWORD resourceSize = 0;
        if (!Bytes(name, &resourceBytes, &resourceSize))
            return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);

        try
        {
            // WIC's in-memory stream borrows its input, so keep a private copy through decode.
            std::vector<BYTE> copy(resourceBytes, resourceBytes + resourceSize);
            ComPtr<IWICImagingFactory> factory;
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory));
            if (FAILED(hr))
                return hr;

            ComPtr<IWICStream> stream;
            hr = factory->CreateStream(&stream);
            if (FAILED(hr))
                return hr;
            hr = stream->InitializeFromMemory(copy.data(), resourceSize);
            if (FAILED(hr))
                return hr;

            ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                  &decoder);
            if (FAILED(hr))
                return hr;
            ComPtr<IWICBitmapFrameDecode> frame;
            hr = decoder->GetFrame(0, &frame);
            if (FAILED(hr))
                return hr;
            ComPtr<IWICFormatConverter> converter;
            hr = factory->CreateFormatConverter(&converter);
            if (FAILED(hr))
                return hr;
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeMedianCut);
            if (FAILED(hr))
                return hr;
            ComPtr<IWICBitmap> decoded;
            hr = factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &decoded);
            if (SUCCEEDED(hr))
                *bitmap = decoded.Detach();
            return hr;
        }
        catch (const std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            return E_FAIL;
        }
    }
}
