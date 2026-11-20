#include "Presentation.hpp"
#include <cmath>
namespace v6
{
    std::vector<GalleryRow> VisibleRows(Engine &e)
    {
        auto dates = e.VisibleDates();
        std::vector<GalleryRow> rows;
        e.gallery.Use(false,
                      [&]
                      {
                          for (const auto &date : dates)
                          {
                              const auto d = Ansi(date);
                              int n = e.api.field(d.c_str(), 3, nullptr, 0);
                              Need(n > 0 && n <= 65536, "Row title length");
                              std::vector<char> b(n);
                              Need(e.api.field(d.c_str(), 3, b.data(), n) == n, "Row title");
                              std::string text(b.data(), n - 1);
                              std::wstring title;
                              if (text.rfind("u8:", 0) == 0)
                                  title = Wide(text.substr(3));
                              else
                              {
                                  int count = MultiByteToWideChar(CP_ACP, 0, b.data(), n - 1, nullptr, 0);
                                  title.resize(count);
                                  MultiByteToWideChar(CP_ACP, 0, b.data(), n - 1, title.data(), count);
                              }
                              int favorite = e.api.favorite(d.c_str());
                              Need(favorite == 0 || favorite == 1, "Row favorite");
                              rows.push_back({date, title, favorite == 1});
                          }
                          return 0;
                      });
        return rows;
    }
    void SelectRow(Engine &e, const std::wstring &date)
    {
        auto rows = VisibleRows(e);
        Need(std::any_of(rows.begin(), rows.end(),
                         [&](const GalleryRow &r)
                         {
                             return r.date == date;
                         }),
             "Record is absent from visible list");
        e.Select(date);
        e.Save();
    }
    D2D1_RECT_F PreviewDestination(UINT width, UINT height, float fw, float fh, int mode)
    {
        Need(width && height && fw > 0 && fh > 0 && mode >= 0 && mode <= 3, "Preview geometry");
        float w = (float)width, h = (float)height;
        if (mode == 0)
        {
            float ratio = std::min(fw / w, fh / h);
            w *= ratio;
            h *= ratio;
        }
        if (mode == 3)
        {
            w = fw;
            h = fh;
        }
        float x = mode == 2 ? 0 : (fw - w) / 2, y = mode == 2 ? 0 : (fh - h) / 2;
        return D2D1::RectF(x, y, x + w, y + h);
    }
    void PaintPreview(ID2D1RenderTarget *target, ID2D1Bitmap *image, UINT width, UINT height,
                      D2D1_RECT_F area, int mode)
    {
        target->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
        ComPtr<ID2D1SolidColorBrush> background;
        Need(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0x172430), &background)),
             "Preview background");
        target->FillRectangle(area, background.Get());
        if (image)
        {
            auto dest =
                PreviewDestination(width, height, area.right - area.left, area.bottom - area.top, mode);
            if (mode == 2)
            {
                ComPtr<ID2D1BitmapBrush> tile;
                Need(
                    SUCCEEDED(target->CreateBitmapBrush(
                        image, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP),
                        D2D1::BrushProperties(1, D2D1::Matrix3x2F::Translation(area.left, area.top)), &tile)),
                    "Tile brush");
                target->FillRectangle(area, tile.Get());
            }
            else
            {
                dest.left += area.left;
                dest.right += area.left;
                dest.top += area.top;
                dest.bottom += area.top;
                target->DrawBitmap(image, dest, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
        target->PopAxisAlignedClip();
    }
    Bytes PreviewPng(const Decoded &image, UINT width, UINT height, int mode)
    {
        ComPtr<IWICImagingFactory> wic;
        Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&wic))),
             "Preview WIC");
        ComPtr<IWICBitmap> surface;
        Need(SUCCEEDED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                         &surface)),
             "Preview surface");
        ComPtr<ID2D1Factory> factory;
        Need(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
             "Offscreen D2D");
        ComPtr<ID2D1RenderTarget> target;
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        Need(SUCCEEDED(factory->CreateWicBitmapRenderTarget(surface.Get(), props, &target)),
             "Offscreen render target");
        ComPtr<ID2D1Bitmap> bitmap;
        auto bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(), 96, 96);
        Need(SUCCEEDED(target->CreateBitmapFromWicBitmap(image.source.Get(), &bitmapProps, &bitmap)),
             "Offscreen image");
        target->BeginDraw();
        PaintPreview(target.Get(), bitmap.Get(), image.width, image.height,
                     D2D1::RectF(0, 0, (float)width, (float)height), mode);
        Need(SUCCEEDED(target->EndDraw()), "Offscreen EndDraw");
        ComPtr<IStream> stream;
        Need(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "PNG memory stream");
        ComPtr<IWICBitmapEncoder> encoder;
        Need(SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)), "PNG encoder");
        Need(SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "PNG initialize");
        ComPtr<IWICBitmapFrameEncode> frame;
        Need(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)), "PNG frame");
        Need(SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)), "PNG size");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        Need(SUCCEEDED(frame->SetPixelFormat(&format)), "PNG format");
        Need(SUCCEEDED(frame->WriteSource(surface.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
                 SUCCEEDED(encoder->Commit()),
             "PNG save");
        STATSTG stat{};
        Need(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.HighPart == 0, "PNG stream size");
        HGLOBAL memory;
        Need(SUCCEEDED(GetHGlobalFromStream(stream.Get(), &memory)), "PNG memory");
        auto data = static_cast<const BYTE *>(GlobalLock(memory));
        Need(data != nullptr, "PNG lock");
        Bytes bytes(data, data + stat.cbSize.LowPart);
        GlobalUnlock(memory);
        return bytes;
    }
} // namespace v6
