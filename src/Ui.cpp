#include "Presentation.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <future>
#include <chrono>
using namespace v6;
namespace
{
    struct Hit
    {
        D2D1_RECT_F r;
        int id;
    };
    struct Shell
    {
        std::unique_ptr<Engine> engine;
        HWND window = nullptr, edit = nullptr, list = nullptr;
        HFONT editFont = nullptr;
        int page = 0;
        PageRoute route;
        D2D1_RECT_F previewArea{};
        std::vector<GalleryRow> rows;
        size_t records = 0;
        bool busy = false, showOnline = false;
        std::wstring status = L"已加载三记录独立图库；联网需点击获取，壁纸默认 dry-run。",
                     name = L"胖哥每日壁纸 © 测试主题";
        ULONGLONG due = 0;
        Online online;
        std::future<Online> future;
        std::vector<Hit> hits;
        ComPtr<ID2D1Factory> d2d;
        ComPtr<IDWriteFactory> dw;
        ComPtr<ID2D1HwndRenderTarget> target;
        ComPtr<ID2D1SolidColorBrush> ink;
        ComPtr<ID2D1Bitmap> bitmap;
    } s;
    D2D1_COLOR_F Color(UINT rgb)
    {
        return D2D1::ColorF(rgb);
    }
    void Fill(D2D1_RECT_F r, UINT color)
    {
        s.ink->SetColor(Color(color));
        s.target->FillRectangle(r, s.ink.Get());
    }
    void Text(const std::wstring &t, float x, float y, float w, float h, float size = 15,
              UINT color = 0x243444)
    {
        ComPtr<IDWriteTextFormat> f;
        Need(SUCCEEDED(s.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                              L"zh-CN", &f)),
             "DWrite font");
        s.ink->SetColor(Color(color));
        s.target->DrawTextW(t.c_str(), (UINT32)t.size(), f.Get(), D2D1::RectF(x, y, x + w, y + h),
                            s.ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Button(const std::wstring &t, int id, float x, float y, float w = 154, float h = 36,
                bool enabled = true)
    {
        auto r = D2D1::RectF(x, y, x + w, y + h);
        Fill(r, enabled ? 0xFFFFFF : 0xE4E8EB);
        s.ink->SetColor(Color(0xB6C4D0));
        s.target->DrawRectangle(r, s.ink.Get());
        Text(t, x + 9, y + 7, w - 16, h - 10, 14);
        if (enabled)
            s.hits.push_back({r, id});
    }
    void RefreshList()
    {
        if (!s.list)
            return;
        if (s.page != 1)
        {
            ShowWindow(s.list, SW_HIDE);
            return;
        }
        s.rows = VisibleRows(*s.engine);
        SendMessageW(s.list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(s.list, LB_RESETCONTENT, 0, 0);
        int selected = -1;
        for (size_t i = 0; i < s.rows.size(); i++)
        {
            auto label = s.rows[i].date + (s.rows[i].favorite ? L"  ★" : L"  ☆") + L"\n" + s.rows[i].title;
            SendMessageW(s.list, LB_ADDSTRING, 0, (LPARAM)label.c_str());
            if (s.engine->hasCurrent && s.rows[i].date == s.engine->current.date)
                selected = (int)i;
        }
        SendMessageW(s.list, LB_SETCURSEL, selected, 0);
        EnableWindow(s.list, !s.busy);
        SendMessageW(s.list, WM_SETREDRAW, TRUE, 0);
        ShowWindow(s.list, SW_SHOWNA);
        InvalidateRect(s.list, nullptr, TRUE);
    }
    void Dirty()
    {
        s.records = s.engine->gallery.Dates().size();
        s.bitmap.Reset();
        RefreshList();
        InvalidateRect(s.window, nullptr, FALSE);
    }
    void Note(const std::wstring &message)
    {
        s.status = message;
        InvalidateRect(s.window, nullptr, FALSE);
    }
    void Page(int p)
    {
        s.route.Open(p);
        s.page = s.route.page;
        s.showOnline = false;
        if (s.engine->hasCurrent)
            s.engine->Select(s.engine->current.date);
        ShowWindow(s.edit, p == 3 ? SW_SHOWNA : SW_HIDE);
        Dirty();
    }
    void EnsureTarget()
    {
        if (s.target)
            return;
        RECT r;
        GetClientRect(s.window, &r);
        Need(SUCCEEDED(s.d2d->CreateHwndRenderTarget(
                 D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96),
                 D2D1::HwndRenderTargetProperties(s.window, D2D1::SizeU(r.right, r.bottom)), &s.target)),
             "D2D target");
        Need(SUCCEEDED(s.target->CreateSolidColorBrush(Color(0x243444), &s.ink)), "D2D brush");
    }
    bool Draw()
    {
        EnsureTarget();
        auto &e = *s.engine;
        s.hits.clear();
        s.target->BeginDraw();
        s.target->Clear(Color(0xF5F7FA));
        Fill(D2D1::RectF(0, 0, 210, 700), 0xE7EDF2);
        Text(L"胖哥每日壁纸", 16, 22, 185, 40, 23);
        Text(L"v6 · 真实集成候选", 16, 66, 185, 30, 12);
        for (int p = 0; p < 9; p++)
            Button(Pages[p], 100 + p, 12, 111 + (float)p * 53, 184, 37);
        Text(L"本轮隔离写入\n自启动功能未实现", 16, 604, 180, 55, 12);
        Text(Pages[s.page], 234, 21, 540, 45, 25);
        Button(s.page == 4 ? (s.route.viewerReturn == 1 ? L"返回图库" : L"返回每日") : L"返回主页", 200, 962,
               24, 116, 32);
        Text(L"共享夹具只读 · 当前页操作读写本轮图库 · " + std::to_wstring(s.records) + L" 条记录", 234, 71,
             850, 28, 13);
        Button(L"上一张", 201, 234, 98, 100, 34);
        Button(L"下一张", 202, 342, 98, 100, 34);
        Button(e.hasCurrent && e.current.favorite ? L"取消收藏" : L"收藏", 203, 450, 98, 110, 34,
               !s.showOnline);
        Button(L"横/竖图尺寸 " + std::to_wstring(e.state.size), 204, 568, 98, 162, 34, !s.showOnline);
        auto area = D2D1::RectF(234, 132, 874, 492);
        s.previewArea = area;
        if (s.page == 0 || s.page == 1)
            Button(L"大图查看", 280, 748, 98, 126, 34);
        const Decoded *im = s.showOnline ? &s.online.bitmap : e.hasCurrent ? &e.current.bitmap : nullptr;
        if (im && im->source)
        {
            if (!s.bitmap)
            {
                auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(), 96, 96);
                Need(SUCCEEDED(s.target->CreateBitmapFromWicBitmap(im->source.Get(), &properties, &s.bitmap)),
                     "D2D image");
            }
        }
        PaintPreview(s.target.Get(), s.bitmap.Get(), im ? im->width : 0, im ? im->height : 0, area,
                     s.page == 4 ? e.state.viewMode : 0);
        std::wstring info =
            s.showOnline   ? (s.online.isReplay ? L"离线重放 " : L"Bing ") + s.online.date + L"\n" +
                                 s.online.title + L"\n\n" + s.online.copyright + L"\n\n已解码并从隔离缓存回读"
            : e.hasCurrent ? e.current.date + L"\n" + e.current.title + L"\n\n" + e.current.author + L"\n\n" +
                                 std::to_wstring(e.current.bitmap.width) + L" × " +
                                 std::to_wstring(e.current.bitmap.height) + L"\n" +
                                 (e.current.favorite ? L"★ 已收藏" : L"☆ 未收藏")
                           : L"本轮图库已空";
        Text(info, 892, 132, 205, s.page == 1 ? 142.0f : 360.0f);
        if (s.page == 1)
            Text(L"记录列表（点击直接选择）", 892, 286, 205, 22, 12);
        switch (s.page)
        {
        case 0:
        case 1:
            Button(L"导出当前图像", 210, 234, 512);
            Button(L"删除本轮记录", 211, 400, 512);
            Button(L"应用当前壁纸", 212, 566, 512);
            Button(L"恢复原壁纸", 213, 732, 512);
            Button(e.state.paused ? L"开始轮换" : L"暂停轮换", 250, 234, 559, 130, 30);
            Button(e.state.random ? L"随机" : L"顺序", 251, 374, 559, 120, 30);
            Button(L"间隔 " + std::to_wstring(e.state.seconds) + L"s", 252, 504, 559, 140, 30);
            Button(L"回读设置", 253, 654, 559, 130, 30);
            Button(e.state.favoritesOnly ? L"筛选：收藏" : L"筛选：全部", 254, 794, 559, 230, 30);
            Text(L"当前工作库：" + e.state.galleryRelative +
                     L"；删除/导出/收藏作用于该库。\n"
                     L"仅带 --allow-wallpaper 且点击开始轮换才应用桌面；默认暂停，失败暂停。",
                 234, 603, 845, 35, 12);
            break;
        case 4:
            for (int i = 0; i < 4; i++)
                Button(std::wstring(e.state.viewMode == i ? L"● " : L"○ ") + PreviewModes[i], 290 + i,
                       234 + (float)i * 160, 512, 150, 36);
            Button(L"导出当前图像", 210, 234, 562, 180, 32);
            Text(L"同一 640×360 查看框；居中/平铺使用原图像素，超出部分裁切。返回保留记录与进入页。", 434,
                 562, 640, 42, 13);
            break;
        case 2:
            Button(L"复制校验并切换工作库", 220, 234, 512, 205);
            Text(L"复制当前写库至本轮 migrated-gallery；逐文件 SHA256 "
                 L"全五档解码后原子写配置并切库；后续实际读写目标。冲突/失败不切换。",
                 454, 512, 625, 72, 14);
            break;
        case 3:
        {
            const wchar_t *scopes[] = {L"全部", L"收藏", L"自选"},
                          *styles[] = {L"居中", L"拉伸", L"适应", L"填充", L"平铺"};
            Button(L"集合：" + std::wstring(scopes[e.state.themeScope]), 231, 490, 510, 125, 32);
            Button(L"主题尺寸 " + std::to_wstring(e.state.themeSize), 232, 625, 510, 135, 32);
            Button(styles[e.state.themeStyle], 233, 770, 510, 115, 32);
            Button(L"间隔 " + std::to_wstring(e.state.themeSeconds) + L"s", 234, 895, 510, 155, 32);
            Button(e.state.themeShuffle ? L"随机播放" : L"顺序播放", 235, 234, 550, 125, 32);
            Button(e.state.themeTransparent ? L"透明 开" : L"透明 关", 236, 369, 550, 125, 32);
            Button(L"颜色 " + e.state.themeColor, 237, 504, 550, 200, 32);
            Button(L"导出真实 CAB .themepack", 230, 720, 550, 330, 32);
            Text(L"至少两图；P08 自选集合。相对图片引用；支持 Win7 主题字段；所有上述选项写入实际输出。", 234,
                 595, 845, 40, 13);
            break;
        }
        case 5:
            Button(L"Bing 中国", 240, 234, 512);
            Button(L"Bing 美国", 241, 400, 512);
            Button(L"自建来源", 244, 566, 512);
            Button(s.busy ? L"请求中…" : L"联网获取并五档入库", 242, 234, 559, 220);
            Button(L"离线响应重放", 243, 466, 559, 220);
            Text(L"当前来源 " + e.state.source +
                     L"；TLS1.2 only / 正常证书链与主机名验证 / 显式代理 / 超时与大小上限。",
                 700, 512, 380, 110, 13);
            break;
        case 6:
            Text(L"七日壁纸：明确占位；本候选仅提供单日真实联网流程，尚未实现七日聚合。", 234, 515, 845, 75,
                 16);
            break;
        case 7:
            Button(L"选择/取消当前图", 270, 234, 512, 190, 34);
            Button(L"选择全部图", 271, 436, 512, 170, 34);
            Button(L"选择收藏图", 272, 618, 512, 170, 34);
            Button(L"前往主题导出", 273, 800, 512, 205, 34);
            Text(L"自选日期：" + e.state.themeDates +
                     L"\n用上方前后切换预览，再加入/取消。至少选择两个真实记录。",
                 234, 562, 845, 65, 14);
            break;
        case 8:
            Button(L"导出 iOS 操作说明与图片", 260, 234, 512, 280);
            Text(L"附真实当前记录 JPEG 与所选 Bing 元数据地址；手动传输，不生成/安装签名快捷指令。", 234, 564,
                 845, 60, 14);
            break;
        }
        Text(s.status, 234, 643, 862, 50, 13);
        HRESULT hr = s.target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            s.bitmap.Reset();
            s.ink.Reset();
            s.target.Reset();
        }
        else
            Need(SUCCEEDED(hr), "D2D EndDraw");
        return hr != D2DERR_RECREATE_TARGET;
    }
    void Act(int id)
    {
        auto &e = *s.engine;
        if (s.busy)
        {
            Note(L"后台导入进行中，请稍后操作。");
            return;
        }
        if (s.showOnline && (id == 203 || id == 204))
        {
            Note(L"请先用上一张/下一张返回本轮图库，再操作收藏或尺寸。");
            return;
        }
        if (id >= 100 && id <= 108)
        {
            Page(id - 100);
            return;
        }
        if (id >= 290 && id <= 293)
        {
            e.state.viewMode = id - 290;
            e.Save();
            Dirty();
            return;
        }
        switch (id)
        {
        case 200:
            Page(s.route.Back());
            break;
        case 280:
            Page(4);
            break;
        case 244:
            e.state.source = L"custom";
            e.Save();
            s.showOnline = false;
            Note(L"下次实际请求自建精确接口；失败不回退。");
            break;
        case 201:
        case 202:
            s.showOnline = false;
            e.Move(id == 201 ? -1 : 1);
            break;
        case 203:
            e.Toggle();
            s.showOnline = false;
            break;
        case 204:
            e.state.size = e.state.size % 5 + 1;
            if (e.hasCurrent)
                e.Select(e.current.date);
            e.Save();
            s.showOnline = false;
            break;
        case 210:
            Note(L"已导出：" + e.Export().wstring());
            break;
        case 211:
            e.Delete();
            s.showOnline = false;
            Note(L"本轮记录已删除并重开库确认。");
            break;
        case 212:
            Note(e.Wallpaper(false));
            break;
        case 213:
            e.PauseRotation();
            Note(e.Wallpaper(true));
            break;
        case 220:
            Note(L"迁移完成：" + e.Migrate().wstring());
            break;
        case 230:
        {
            int n = GetWindowTextLengthW(s.edit);
            std::vector<wchar_t> b(n + 1);
            GetWindowTextW(s.edit, b.data(), n + 1);
            s.name = b.data();
            Note(L"已输出（未安装）：" + e.Theme(s.name).wstring());
            break;
        }
        case 231:
            e.state.themeScope = (e.state.themeScope + 1) % 3;
            e.Save();
            break;
        case 232:
            e.state.themeSize = e.state.themeSize % 5 + 1;
            e.Save();
            break;
        case 233:
            e.state.themeStyle = (e.state.themeStyle + 1) % 5;
            e.Save();
            break;
        case 234:
            e.state.themeSeconds = e.state.themeSeconds == 60 ? 600 : e.state.themeSeconds == 600 ? 3600 : 60;
            e.Save();
            break;
        case 235:
            e.state.themeShuffle = !e.state.themeShuffle;
            e.Save();
            break;
        case 236:
            e.state.themeTransparent = !e.state.themeTransparent;
            e.Save();
            break;
        case 237:
            e.state.themeColor = e.state.themeColor == L"C40078D4"   ? L"A8CE0F0F"
                                 : e.state.themeColor == L"A8CE0F0F" ? L"80555555"
                                                                     : L"C40078D4";
            e.Save();
            break;
        case 254:
            e.Filter(!e.state.favoritesOnly);
            break;
        case 270:
            e.ThemeSelectCurrent();
            break;
        case 271:
        case 272:
            e.state.themeDates = L"";
            for (auto &date : e.gallery.Dates(id == 272))
            {
                if (!e.state.themeDates.empty())
                    e.state.themeDates += L",";
                e.state.themeDates += date;
            }
            e.state.themeScope = 2;
            e.Save();
            break;
        case 273:
            Page(3);
            break;
        case 240:
        case 241:
            e.state.source = id == 240 ? L"zh-CN" : L"en-US";
            e.Save();
            s.showOnline = false;
            Note(L"下次实际请求 mkt=" + e.state.source);
            break;
        case 242:
        case 243:
            if (!s.busy)
            {
                bool replay = id == 243;
                Options args = e.args;
                std::wstring market = e.state.source;
                auto cache = e.PathIn(replay ? L"cache-replay" : L"cache");
                auto source = e.ActiveWrite();
                s.busy = true;
                Note(L"异步请求元数据 → 图片 → 解码 → 缓存回读…");
                s.future = std::async(std::launch::async,
                                      [args, market, cache, replay, source]
                                      {
                                          ComApartment com(COINIT_MULTITHREADED);
                                          auto result = (replay ? Replay : Fetch)(args, market, cache,
                                                                                  [](const std::string &m)
                                                                                  {
                                                                                      s.engine->Log(m);
                                                                                  });
                                          result = s.engine->StageImport(std::move(result), source);
                                          result.bitmap.source.Reset();
                                          return result;
                                      });
            }
            break;
        case 250:
            if (e.state.paused)
                e.StartRotation();
            else
                e.PauseRotation();
            s.due = GetTickCount64() + (ULONGLONG)e.state.seconds * 1000;
            Note(e.state.paused          ? L"轮换已暂停。"
                 : e.args.allowWallpaper ? L"轮换已开始；每次切图将应用桌面，恢复原壁纸会先暂停。"
                                         : L"dry-run：只轮换预览；应用桌面需 --allow-wallpaper。");
            break;
        case 251:
            e.state.random = !e.state.random;
            e.Save();
            break;
        case 252:
            e.state.seconds = e.state.seconds == 5 ? 60 : e.state.seconds == 60 ? 3600 : 5;
            s.due = GetTickCount64() + (ULONGLONG)e.state.seconds * 1000;
            e.Save();
            break;
        case 253:
            e.Reload();
            e.PauseRotation();
            s.due = GetTickCount64() + (ULONGLONG)e.state.seconds * 1000;
            break;
        case 260:
            Note(L"已输出手动说明（非 .shortcut）：" + e.IosGuide().wstring());
            break;
        default:
            break;
        }
        Dirty();
    }
    void Failure(const std::exception &ex)
    {
        std::string detail = ex.what();
        std::wstring message;
        try
        {
            message = Wide(detail);
        }
        catch (...)
        {
            message = L"文件系统操作失败";
            detail = "non_UTF8 filesystem exception";
        }
        Note(L"失败：" + message);
        try
        {
            s.engine->Log("operation failure: " + detail);
        }
        catch (...)
        {
        }
    }
    LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        try
        {
            switch (m)
            {
            case WM_CREATE:
                s.window = h;
                s.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s.name.c_str(), WS_CHILD | ES_AUTOHSCROLL,
                                         234, 510, 245, 32, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                Need(s.edit != nullptr, "Theme edit");
                SendMessageW(s.edit, EM_SETLIMITTEXT, 80, 0);
                s.editFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                         DEFAULT_PITCH, L"Segoe UI");
                SendMessageW(s.edit, WM_SETFONT, (WPARAM)s.editFont, TRUE);
                ShowWindow(s.edit, SW_HIDE);
                s.list = CreateWindowExW(0, L"LISTBOX", L"",
                                         WS_CHILD | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
                                             LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                                         892, 308, 205, 184, h, (HMENU)(INT_PTR)300,
                                         GetModuleHandleW(nullptr), nullptr);
                Need(s.list != nullptr, "Gallery list");
                SendMessageW(s.list, WM_SETFONT, (WPARAM)s.editFont, TRUE);
                ShowWindow(s.list, SW_HIDE);
                SetTimer(h, 1, 1000, nullptr);
                return 0;
            case WM_PAINT:
            {
                PAINTSTRUCT p;
                BeginPaint(h, &p);
                bool rendered = false;
                try
                {
                    rendered = Draw();
                }
                catch (...)
                {
                    s.bitmap.Reset();
                    s.ink.Reset();
                    s.target.Reset();
                    EndPaint(h, &p);
                    throw;
                }
                EndPaint(h, &p);
                if (!rendered)
                    InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_MEASUREITEM:
                if (((MEASUREITEMSTRUCT *)l)->CtlID == 300)
                {
                    ((MEASUREITEMSTRUCT *)l)->itemHeight = 48;
                    return TRUE;
                }
                break;
            case WM_DRAWITEM:
            {
                auto d = (DRAWITEMSTRUCT *)l;
                if (d->CtlID != 300)
                    break;
                bool selected = (d->itemState & ODS_SELECTED) != 0;
                HBRUSH bg = CreateSolidBrush(selected ? RGB(212, 230, 246) : RGB(255, 255, 255));
                FillRect(d->hDC, &d->rcItem, bg);
                DeleteObject(bg);
                if (d->itemID < s.rows.size())
                {
                    auto old = SelectObject(d->hDC, s.editFont);
                    SetBkMode(d->hDC, TRANSPARENT);
                    SetTextColor(d->hDC, RGB(36, 52, 68));
                    RECT line = d->rcItem;
                    line.left += 4;
                    line.right -= 3;
                    line.top += 3;
                    line.bottom = line.top + 20;
                    auto first = s.rows[d->itemID].date + (s.rows[d->itemID].favorite ? L"  ★" : L"  ☆");
                    DrawTextW(d->hDC, first.c_str(), -1, &line,
                              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    line.top += 21;
                    line.bottom += 21;
                    DrawTextW(d->hDC, s.rows[d->itemID].title.c_str(), -1, &line,
                              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(d->hDC, old);
                }
                if (d->itemState & ODS_FOCUS)
                    DrawFocusRect(d->hDC, &d->rcItem);
                return TRUE;
            }
            case WM_COMMAND:
                if (LOWORD(w) == 300 && (HIWORD(w) == LBN_SELCHANGE || HIWORD(w) == LBN_DBLCLK))
                {
                    if (s.busy)
                    {
                        RefreshList();
                        return 0;
                    }
                    auto index = SendMessageW(s.list, LB_GETCURSEL, 0, 0);
                    if (index >= 0 && (size_t)index < s.rows.size())
                    {
                        SelectRow(*s.engine, s.rows[(size_t)index].date);
                        s.showOnline = false;
                        if (HIWORD(w) == LBN_DBLCLK)
                            Page(4);
                        else
                            Dirty();
                    }
                    return 0;
                }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONUP:
            {
                float x = (float)(short)LOWORD(l), y = (float)(short)HIWORD(l);
                for (const auto &hit : s.hits)
                    if (x >= hit.r.left && x <= hit.r.right && y >= hit.r.top && y <= hit.r.bottom)
                    {
                        Act(hit.id);
                        break;
                    }
                return 0;
            }
            case WM_KEYDOWN:
                if (w == VK_LEFT)
                    Act(201);
                else if (w == VK_RIGHT)
                    Act(202);
                else if (w == VK_HOME)
                    Act(200);
                return 0;
            case WM_TIMER:
                if (s.busy && s.future.valid() &&
                    s.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    s.busy = false;
                    try
                    {
                        s.online = s.future.get();
                        s.engine->AdoptImport(s.online);
                        s.showOnline = false;
                        Note((s.online.isReplay ? L"离线重放已五档入库（非联网）："
                                                : L"Bing 五档入库并重开：") +
                             s.online.cache.wstring());
                    }
                    catch (const std::exception &ex)
                    {
                        Failure(ex);
                    }
                    Dirty();
                }
                if (!s.busy && s.engine->rotationStarted && !s.engine->state.paused &&
                    GetTickCount64() >= s.due)
                {
                    try
                    {
                        Note(s.engine->RotationTick());
                        s.showOnline = false;
                    }
                    catch (const std::exception &ex)
                    {
                        try
                        {
                            s.engine->PauseRotation();
                        }
                        catch (...)
                        {
                        }
                        Failure(ex);
                        s.status = L"轮换已暂停；" + s.status;
                    }
                    s.due = GetTickCount64() + (ULONGLONG)s.engine->state.seconds * 1000;
                    Dirty();
                }
                return 0;
            case WM_CLOSE:
                if (s.busy)
                {
                    Note(L"正在等待有界网络请求结束，请稍后关闭。");
                    return 0;
                }
                DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                KillTimer(h, 1);
                if (s.editFont)
                {
                    DeleteObject(s.editFont);
                    s.editFont = nullptr;
                }
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(h, m, w, l);
            }
            return DefWindowProcW(h, m, w, l);
        }
        catch (const std::exception &ex)
        {
            Failure(ex);
            return 0;
        }
    }
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show)
{
    try
    {
        SetProcessDPIAware();
        ComApartment apartment;
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        Need(argv != nullptr, "CommandLineToArgvW");
        Options options;
        try
        {
            options = Options::Parse(argc, argv);
        }
        catch (...)
        {
            LocalFree(argv);
            throw;
        }
        LocalFree(argv);
        s.engine = std::make_unique<Engine>(options);
        s.engine->PauseRotation(); // Startup/resume never starts desktop rotation.
        s.records = s.engine->gallery.Dates().size();
        Need(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, s.d2d.GetAddressOf())),
             "D2D factory");
        Need(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown **>(s.dw.GetAddressOf()))),
             "DWrite factory");
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = Proc;
        wc.lpszClassName = L"StoutWallV6D2D";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Need(RegisterClassW(&wc) != 0, "RegisterClass");
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT r{0, 0, ClientWidth, ClientHeight};
        Win(AdjustWindowRectEx(&r, style, FALSE, 0) != FALSE, "AdjustWindowRect");
        RECT work{};
        Win(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) != FALSE, "SPI_GETWORKAREA");
        int width = r.right - r.left, height = r.bottom - r.top;
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"胖哥每日壁纸 · ReBuild 2", style,
                                      work.left + std::max<LONG>(0, (work.right - work.left - width) / 2),
                                      work.top + std::max<LONG>(0, (work.bottom - work.top - height) / 2),
                                      width, height, nullptr, nullptr, instance, nullptr);
        Need(window != nullptr, "CreateWindow");
        s.due = GetTickCount64() + 60000;
        ShowWindow(window, show);
        UpdateWindow(window);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        s.bitmap.Reset();
        s.ink.Reset();
        s.target.Reset();
        s.dw.Reset();
        s.d2d.Reset();
        s.online = Online{};
        s.engine.reset();
        return 0;
    }
    catch (const std::exception &ex)
    {
        OutputDebugStringA((std::string("V6_STARTUP_ERROR ") + ex.what()).c_str());
        return 2;
    }
}
