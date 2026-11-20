#pragma once
#include "Core.hpp"
#include <d2d1.h>
namespace v6
{
    struct PageRoute
    {
        int page = 0, viewerReturn = 0;
        void Open(int next)
        {
            Need(next >= 0 && next < 9, "Invalid page");
            if (next == 4 && (page == 0 || page == 1))
                viewerReturn = page;
            page = next;
        }
        int Back() const
        {
            return page == 4 ? viewerReturn : 0;
        }
    };
    struct GalleryRow
    {
        std::wstring date, title;
        bool favorite;
    };
    std::vector<GalleryRow> VisibleRows(Engine &e);
    void SelectRow(Engine &e, const std::wstring &date);
    inline const wchar_t *PreviewModes[] = {L"适应", L"居中", L"平铺", L"拉伸"};
    D2D1_RECT_F PreviewDestination(UINT width, UINT height, float fw, float fh, int mode);
    void PaintPreview(ID2D1RenderTarget *target, ID2D1Bitmap *image, UINT width, UINT height,
                      D2D1_RECT_F area, int mode);
    Bytes PreviewPng(const Decoded &image, UINT width, UINT height, int mode);
} // namespace v6
