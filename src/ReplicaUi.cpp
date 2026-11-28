#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ReplicaUi.hpp"
#include "OriginalLayout.hpp"
#include "ProductLayout.hpp"
#include "Assets.hpp"
#include "Version.h"
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <uxtheme.h>
#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>
#include <cmath>

#pragma comment(lib,"comctl32.lib")

namespace stoutwall {
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t kClassName[]=L"StoutWallOriginalWindow";
constexpr UINT_PTR kViewerCursorTimer=187;
constexpr UINT kRequestThumbnails=WM_APP+0x315;
double Luminance(COLORREF color) {
    const auto channel=[](BYTE v) {const double s=v/255.;return s<=0.04045?s/12.92:std::pow((s+0.055)/1.055,2.4);};
    return 0.2126*channel(GetRValue(color))+0.7152*channel(GetGValue(color))+0.0722*channel(GetBValue(color));
}
COLORREF ReadableAccent(COLORREF accent,COLORREF background) {
    const double bg=Luminance(background);
    const int target=bg<0.4?255:0;
    for(int step=0;step<=20;++step) {
        const auto mix=[&](BYTE value) {return static_cast<BYTE>((value*(20-step)+target*step)/20);};
        const COLORREF candidate=RGB(mix(GetRValue(accent)),mix(GetGValue(accent)),mix(GetBValue(accent)));
        const double fg=Luminance(candidate);
        if(((std::max)(fg,bg)+0.05)/((std::min)(fg,bg)+0.05)>=4.5)return candidate;
    }
    return RGB(target,target,target);
}
const WindowDef* Definition(const std::wstring& name) {
    for(const auto& w:OriginalWindows) if(name==w.name) return &w;
    return nullptr;
}
bool IsImage(const std::wstring& type) { return type==L"Lib3.4"||type==L"Lib3.7"; }
bool ReadOnlyEdit(const ControlDef& c) {
    return std::wstring(c.window)!=L"库迁移窗口"&&std::wstring(c.window)!=L"主题包窗口";
}
DWORD Style(const std::wstring& window,const ControlDef& c) {
    const std::wstring t=c.type;
    // The EXUI snapshot marks the host controls hidden while the original
    // program makes them visible during startup. Recreate that runtime state.
    const DWORD base=WS_CHILD|WS_CLIPSIBLINGS;
    if(t==L"Lib3.2") return base|BS_GROUPBOX|WS_CLIPCHILDREN;
    if(t==L"Lib3.3") return base|WS_TABSTOP|TCS_TABS|TCS_BUTTONS|TCS_FLATBUTTONS|TCS_HOTTRACK|TCS_SINGLELINE|TCS_FIXEDWIDTH|TCS_OWNERDRAWFIXED|WS_CLIPCHILDREN;
    if(t==L"Lib3.5") return base|SS_LEFT|SS_NOPREFIX|SS_NOTIFY;
    if(t==L"Lib3.9") return base|WS_TABSTOP|BS_SPLITBUTTON;
    if(t==L"Lib3.7"||t==L"Lib3.8"||t==L"Lib3.6") return base|WS_TABSTOP|BS_PUSHBUTTON|BS_OWNERDRAW;
    if(t==L"Lib3.10") return base|WS_TABSTOP|BS_RADIOBUTTON|BS_NOTIFY;
    if(t==L"Lib3.11") return base|WS_TABSTOP|BS_AUTOCHECKBOX|BS_NOTIFY|BS_MULTILINE;
    if(t==L"Lib3.12") return base|PBS_SMOOTH;
    if(t==L"Lib3.15") return base|WS_TABSTOP|WS_BORDER|ES_LEFT|(ReadOnlyEdit(c)
        ? ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL : ES_AUTOHSCROLL);
    if(t==L"Lib3.16") return base|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL;
    if(t==L"Lib3.18") return window==L"库管理窗口"
        ?base|WS_TABSTOP|LVS_ICON|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_AUTOARRANGE|WS_VSCROLL
        :base|WS_TABSTOP|WS_BORDER|LBS_NOTIFY|WS_VSCROLL;
    if(t==L"Lib3.4") return base|SS_OWNERDRAW|SS_NOTIFY|WS_CLIPCHILDREN;
    return base;
}
const wchar_t* Class(const ControlDef& c) {
    const std::wstring t=c.type;
    if(t==L"Lib3.2"||t==L"Lib3.6"||t==L"Lib3.7"||t==L"Lib3.8"||t==L"Lib3.9"||t==L"Lib3.10"||t==L"Lib3.11") return L"BUTTON";
    if(t==L"Lib3.3") return WC_TABCONTROLW;
    if(t==L"Lib3.5"||t==L"Lib3.4") return L"STATIC";
    if(t==L"Lib3.15") return L"EDIT";
    if(t==L"Lib3.16") return L"COMBOBOX";
    if(t==L"Lib3.18") return std::wstring(c.window)==L"库管理窗口"?WC_LISTVIEWW:L"LISTBOX";
    if(t==L"Lib3.12") return PROGRESS_CLASSW;
    return nullptr;
}

void ReadWindowsAppearance(bool& supported,bool& dark,COLORREF& accent) {
    supported=false;
    dark=false;
    accent=GetSysColor(COLOR_HIGHLIGHT);

    using RtlGetVersionFn=LONG (WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion=ntdll?reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll,"RtlGetVersion")):nullptr;
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize=sizeof(version);
    if(rtlGetVersion&&rtlGetVersion(reinterpret_cast<OSVERSIONINFOW*>(&version))==0)
        supported=version.dwMajorVersion>=10&&version.dwBuildNumber>=17763&&
            (version.wProductType==VER_NT_WORKSTATION||version.dwBuildNumber>=26100);

    DWORD light=1,lightSize=sizeof(light);
    if(RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&lightSize)==ERROR_SUCCESS)
        dark=light==0;
    else supported=false;

    using DwmColorFn=HRESULT (WINAPI*)(DWORD*,BOOL*);
    const HMODULE dwm=LoadLibraryW(L"dwmapi.dll");
    if(dwm) {
        const auto getColor=reinterpret_cast<DwmColorFn>(GetProcAddress(dwm,"DwmGetColorizationColor"));
        DWORD argb=0;
        BOOL opaque=FALSE;
        if(getColor&&SUCCEEDED(getColor(&argb,&opaque)))
            accent=RGB((argb>>16)&0xff,(argb>>8)&0xff,argb&0xff);
        FreeLibrary(dwm);
    }
}
}

ReplicaUi::ReplicaUi(HINSTANCE h,EventHandler handler,HICON icon,const v6::AssetModule& assets)
    : instance_(h),assets_(assets),icon_(icon),handler_(std::move(handler)) {
    if(HDC dc=GetDC(nullptr)) { dpi_=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(nullptr,dc); }
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const double fit=(std::min)({1.0,(work.right-work.left-32)/static_cast<double>(MulDiv(1120,dpi_,96)),
        (work.bottom-work.top-70)/static_cast<double>(MulDiv(740,dpi_,96))});
    dpi_=(std::max)(72,static_cast<int>(dpi_*fit));
    font_=CreateFontW(-MulDiv(14,dpi_,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei");
    titleFont_=CreateFontW(-MulDiv(26,dpi_,96),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
    bodyFont_=CreateFontW(-MulDiv(16,dpi_,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d_.GetAddressOf());
    ReadWindowsAppearance(supportsSystemAppearance_,systemDark_,accentColor_);
    appearanceMode_=supportsSystemAppearance_?0:1;
    ApplyAppearance();
    RegisterClass();
}
ReplicaUi::~ReplicaUi() {
    for(auto& item:windows_) DestroyWindowState(item.second);
    if(font_)DeleteObject(font_);
    if(titleFont_)DeleteObject(titleFont_);
    if(bodyFont_)DeleteObject(bodyFont_);
    if(windowBrush_)DeleteObject(windowBrush_);
    if(panelBrush_)DeleteObject(panelBrush_);
    if(ownsClass_) UnregisterClassW(kClassName,instance_);
    if(icon_) DestroyIcon(icon_);
}

void ReplicaUi::RefreshSystemAppearance() {
    ReadWindowsAppearance(supportsSystemAppearance_,systemDark_,accentColor_);
}

void ReplicaUi::ApplyAppearance() {
    const bool dark=appearanceMode_==2||(appearanceMode_==0&&systemDark_);
    if(dark) {
        windowColor_=RGB(25,28,33);
        panelColor_=RGB(34,38,45);
        textColor_=RGB(238,241,245);
        mutedTextColor_=RGB(169,177,188);
        borderColor_=RGB(62,69,80);
    } else {
        windowColor_=RGB(246,248,251);
        panelColor_=RGB(255,255,255);
        textColor_=RGB(35,40,48);
        mutedTextColor_=RGB(101,109,120);
        borderColor_=RGB(222,226,232);
    }
    accentTextColor_=ReadableAccent(accentColor_,windowColor_);
    accentInkColor_=Luminance(accentColor_)>0.179?RGB(20,24,28):RGB(255,255,255);
    if(windowBrush_)DeleteObject(windowBrush_);
    if(panelBrush_)DeleteObject(panelBrush_);
    windowBrush_=CreateSolidBrush(windowColor_);
    panelBrush_=CreateSolidBrush(panelColor_);
    for(auto& entry:windows_) {
        if(entry.second.hwnd)InvalidateRect(entry.second.hwnd,nullptr,TRUE);
        if(entry.second.hwnd&&!(GetWindowLongPtrW(entry.second.hwnd,GWL_STYLE)&WS_CHILD)) {
            const HMODULE dwm=LoadLibraryW(L"dwmapi.dll");
            if(dwm) {
                using SetAttribute=HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
                const auto set=reinterpret_cast<SetAttribute>(GetProcAddress(dwm,"DwmSetWindowAttribute"));
                const BOOL enabled=dark;
                if(set&&FAILED(set(entry.second.hwnd,20,&enabled,sizeof(enabled))))
                    set(entry.second.hwnd,19,&enabled,sizeof(enabled));
                FreeLibrary(dwm);
            }
        }
        for(auto& child:entry.second.children)if(child.hwnd) {
            if(child.type==L"Lib3.12") {
                SendMessageW(child.hwnd,PBM_SETBARCOLOR,0,accentColor_);
                SendMessageW(child.hwnd,PBM_SETBKCOLOR,0,panelColor_);
            }
            if(entry.first==L"库管理窗口"&&child.type==L"Lib3.18") {
                ListView_SetBkColor(child.hwnd,windowColor_);
                ListView_SetTextBkColor(child.hwnd,windowColor_);
                ListView_SetTextColor(child.hwnd,textColor_);
            }
            InvalidateRect(child.hwnd,nullptr,TRUE);
        }
    }
}

void ReplicaUi::SetAppearance(int mode) {
    if(mode<0||mode>2)mode=1;
    if(mode==0&&!supportsSystemAppearance_)mode=1;
    appearanceMode_=mode;
    RefreshSystemAppearance();
    ApplyAppearance();
}

HBRUSH ReplicaUi::OnControlColor(HDC dc,HWND control,UINT message) {
    SetTextColor(dc,IsWindowEnabled(control)?textColor_:mutedTextColor_);
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN) {
        SetBkMode(dc,TRANSPARENT);
        return windowBrush_;
    }
    SetBkMode(dc,OPAQUE);
    SetBkColor(dc,panelColor_);
    return panelBrush_;
}
bool ReplicaUi::RegisterClass() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WindowProc;
    wc.hInstance=instance_;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon_ ? icon_ : LoadIconW(nullptr,IDI_APPLICATION);
    wc.hIconSm=wc.hIcon;wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpszClassName=kClassName;
    if(RegisterClassExW(&wc)) { ownsClass_=true; return true; }
    return GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
}
HWND ReplicaUi::CreateProductWindow(const wchar_t* rawName, HWND owner) {
    const std::wstring name=rawName; auto found=windows_.find(name);
    if(found!=windows_.end()&&found->second.hwnd&&IsWindow(found->second.hwnd)) return found->second.hwnd;
    if(found!=windows_.end()) {
        DetachSubclasses(found->second);
        found->second.hwnd=nullptr;
        found->second.children.clear();
    }
    const WindowDef* d=Definition(name);if(!d)return nullptr;
    const bool fullscreen=name==L"大图窗口";
    const bool embedded=name==L"主题包窗口"&&owner;
    const DWORD exStyle=embedded?WS_EX_CONTROLPARENT:(owner?0:WS_EX_APPWINDOW)|(fullscreen?WS_EX_TOPMOST:0);
    const DWORD style=WS_CLIPCHILDREN|(embedded?WS_CHILD:fullscreen?WS_POPUP:
        (WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX));
    const auto placement=ProductWindowSize(name);
    const int logicalWidth=placement.customized?placement.width:d->width;
    const int logicalHeight=placement.customized?placement.height:d->height;
    RECT r{0,0,MulDiv(logicalWidth,dpi_,96),MulDiv(logicalHeight,dpi_,96)};
    if(fullscreen) {
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(main_,MONITOR_DEFAULTTOPRIMARY),&monitor)) r=monitor.rcMonitor;
    } else if(embedded) {
        OffsetRect(&r,MulDiv(24,dpi_,96),MulDiv(96,dpi_,96));
    } else {
        AdjustWindowRectEx(&r,style,FALSE,exStyle);
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        OffsetRect(&r,(work.left+work.right-r.left-r.right)/2,(work.top+work.bottom-r.top-r.bottom)/2);
    }
    Window state;state.name=name;
    const std::wstring title=name==L"_启动窗口"?L"胖哥每日壁纸 V" STOUTWALL_VERSION_WIDE:
        name==L"库管理窗口"?L"壁纸库工作台":d->title;
    HWND h=CreateWindowExW(exStyle,kClassName,title.c_str(),style,r.left,r.top,
        r.right-r.left,r.bottom-r.top,owner,nullptr,instance_,this);
    if(!h)return nullptr;
    if(icon_) {
        SendMessageW(h,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon_));
        SendMessageW(h,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon_));
    }
    state.hwnd=h;
    windows_[name]=std::move(state);
    auto it=windows_.find(name);
    if(it==windows_.end()||!CreateChildren(it->second)) {
        if(it!=windows_.end()) {
            DestroyWindowState(it->second);
            windows_.erase(it);
        }
        return nullptr;
    }
    if(fullscreen) {
        GetCursorPos(&it->second.viewerCursor);
        SetTimer(h,kViewerCursorTimer,800,nullptr);
    }
    ApplyAppearance();
    return h;
}
bool ReplicaUi::CreateMain() {
    HWND h=CreateProductWindow(L"_启动窗口");if(!h)return false;main_=h;
    ShowWindow(h,SW_SHOWNORMAL);UpdateWindow(h);return true;
}
bool ReplicaUi::CreateChildren(Window& window) {
    bool success=true;
    std::unordered_map<std::wstring,HWND> parentHandles;parentHandles.emplace(window.name,window.hwnd);
    for(const auto& c:OriginalControls) {
        if(window.name!=c.window)continue;
        const wchar_t* cls=Class(c);if(!cls)continue;
        const auto placement=ProductControlLayout(window.name,c);
        const std::wstring parentName=placement.customized?placement.parent:std::wstring(c.parent);
        HWND parent=window.hwnd;auto p=parentHandles.find(parentName);if(p!=parentHandles.end())parent=p->second;
        const std::wstring type=c.type;
        UINT id=nextId_++;
        int x=MulDiv(placement.customized?placement.x:c.x,dpi_,96);
        int y=MulDiv(placement.customized?placement.y:c.y,dpi_,96);
        int width=MulDiv(placement.customized?placement.width:c.w,dpi_,96);
        int height=MulDiv(placement.customized?placement.height:c.h,dpi_,96);
        if(window.name==L"大图窗口") {
            RECT bounds{};GetClientRect(window.hwnd,&bounds);
            if(type==L"Lib3.4") { width=bounds.right;height=bounds.bottom; }
            if(type==L"Lib3.8") { x=(bounds.right-width)/2;y=MulDiv(35,dpi_,96); }
        }
        const DWORD exStyle=type==L"Lib3.2"||type==L"Lib3.3"?WS_EX_CONTROLPARENT:0;
        const std::wstring text=placement.customized&&!placement.text.empty()?placement.text:
            (std::wstring(c.text)==L"V1.1.0.2509"?std::wstring(L"V" STOUTWALL_VERSION_WIDE):std::wstring(c.text));
        HWND child=CreateWindowExW(exStyle,cls,text.c_str(),Style(window.name,c),x,y,width,
            type==L"Lib3.16" ? height+MulDiv(170,dpi_,96) : height,parent,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),instance_,nullptr);
        if(!child) { success=false;continue; }
        if(parent!=window.hwnd&&!AttachSubclass(window,parent)) success=false;
        if(type==L"Lib3.4"&&!AttachSubclass(window,child)) success=false;
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_?font_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        if(window.name==L"_启动窗口"&&(std::wstring(c.name)==L"标签EX1"||std::wstring(c.name)==L"标签EX9"))
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont_),TRUE);
        if(window.name==L"_启动窗口"&&std::wstring(c.name)==L"标签EX2")
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont_),TRUE);
        if(type==L"Lib3.10"||type==L"Lib3.11"||type==L"Lib3.12")SetWindowTheme(child,L"",L"");
        if(type==L"Lib3.15") SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(4,4));
        if(type==L"Lib3.16") SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),height-MulDiv(6,dpi_,96));
        if(type==L"Lib3.9") {
            BUTTON_SPLITINFO split{};split.mask=BCSIF_SIZE;split.size.cx=MulDiv(28,dpi_,96);
            SendMessageW(child,BCM_SETSPLITINFO,0,reinterpret_cast<LPARAM>(&split));
        }
        if(type==L"Lib3.3") {
            const auto titles=ProductTabTitles(window.name);
            if(!titles.empty()) {
                for(size_t index=0;index<titles.size();++index) {
                    TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(titles[index].c_str());
                    if(TabCtrl_InsertItem(child,static_cast<int>(index),&item)<0)success=false;
                }
            } else {
                for(const auto& tab:OriginalTabs) if(window.name==tab.window&&std::wstring(c.name)==tab.control) {
                    TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(tab.title);
                    if(TabCtrl_InsertItem(child,tab.index,&item)<0) success=false;
                }
            }
            TabCtrl_SetItemSize(child,MulDiv(titles.empty()?105:142,dpi_,96),MulDiv(titles.empty()?38:36,dpi_,96));
            SetWindowTheme(child,L"",L"");
        }
        Child state;
        state.name=c.name;state.type=type;state.hwnd=child;state.id=id;
        state.page=placement.page;state.visible=placement.visible;
        if(type==L"Lib3.4")state.imageMode=ImageMode::Fit;
        if(window.name==L"库管理窗口"&&type==L"Lib3.18") {
            state.placeholderImages=ImageList_Create(MulDiv(180,dpi_,96),MulDiv(102,dpi_,96),ILC_COLOR32,1,1);
            if(state.placeholderImages) {
                ImageList_SetImageCount(state.placeholderImages,1);
                ListView_SetImageList(child,state.placeholderImages,LVSIL_NORMAL);
            }
            ListView_SetIconSpacing(child,MulDiv(214,dpi_,96),MulDiv(170,dpi_,96));
            ListView_SetBkColor(child,windowColor_);
            ListView_SetTextBkColor(child,windowColor_);
            ListView_SetTextColor(child,textColor_);
            ListView_SetExtendedListViewStyle(child,LVS_EX_DOUBLEBUFFER|LVS_EX_BORDERSELECT);
            AttachSubclass(window,child);
        }
        if(window.name==L"大图窗口"&&type==L"Lib3.4") state.imageMode=ImageMode::Fit;
        window.children.push_back(std::move(state));
        parentHandles[c.name]=child;
        LoadControlImage(window,window.children.back());
        // EXUI draws into the host while the XML child HWNDs are hidden.
        // Reproduce visibility from page membership rather than that HWND flag.
        if(placement.visible&&(placement.page==-1||placement.page==0)) ShowWindow(child,SW_SHOWNA);
        else ShowWindow(child,SW_HIDE);
        if(window.name==L"_启动窗口"&&(window.children.back().name==L"按钮EX2"||window.children.back().name==L"按钮EX13"))
            EnableWindow(child,FALSE);
    }
    // XML order is serialization order; native Tab traversal follows sibling order.
    // Keep the full-screen picture/button stack intact, and order forms visually.
    if(window.name!=L"大图窗口") {
        for(const auto& parent:parentHandles) {
            std::vector<HWND> siblings;
            for(const auto& child:window.children)
                if(GetParent(child.hwnd)==parent.second)siblings.push_back(child.hwnd);
            std::stable_sort(siblings.begin(),siblings.end(),[](HWND a,HWND b) {
                RECT ra{},rb{};GetWindowRect(a,&ra);GetWindowRect(b,&rb);
                return ra.top==rb.top?ra.left<rb.left:ra.top<rb.top;
            });
            for(HWND child:siblings)SetWindowPos(child,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
    }
    if(window.name==L"_启动窗口") {
        HWND tab=nullptr;
        if(auto* control=Find(window.hwnd,window.name,L"选择夹EX1"))tab=control->hwnd;
        if(tab) {
            success=AddControl(window,L"外观标签",L"Lib3.5",L"界面外观",32,430,350,30,1,tab)&&success;
            success=AddControl(window,L"外观模式",L"Lib3.16",L"",680,422,268,38,1,tab)&&success;
            success=AddControl(window,L"运行模式标签",L"Lib3.5",L"运行模式",32,492,300,30,0,tab)&&success;
            success=AddControl(window,L"模式说明",L"Lib3.6",L"模式说明",610,530,154,38,0,tab)&&success;
            success=AddControl(window,L"界面状态",L"Lib3.5",L"正在加载壁纸…",32,612,916,36,0,tab)&&success;
        }
    } else if(window.name==L"库管理窗口") {
        success=AddControl(window,L"浏览页",L"Lib3.6",L"壁纸浏览",24,8,144,36,-1,window.hwnd)&&success;
        success=AddControl(window,L"按钮EX4",L"Lib3.6",L"主题导出",182,8,144,36,-1,window.hwnd)&&success;
        success=AddControl(window,L"全部壁纸",L"Lib3.6",L"全部壁纸",24,54,112,36,-1,window.hwnd)&&success;
        success=AddControl(window,L"仅收藏",L"Lib3.6",L"仅收藏",148,54,112,36,-1,window.hwnd)&&success;
        success=AddControl(window,L"显示尺寸标签",L"Lib3.5",L"图库预览尺寸",290,60,144,28,-1,window.hwnd)&&success;
        success=AddControl(window,L"图库统计",L"Lib3.5",L"",24,704,1068,28,-1,window.hwnd)&&success;
    } else if(window.name==L"主题包窗口") {
        success=AddControl(window,L"导出说明",L"Lib3.5",L"将壁纸库导出为 Windows 主题包\r\n\r\n选择整个壁纸库或收藏壁纸，按所选尺寸导出并保留原图文件。\r\n\r\n新系统格式：.deskthemepack\r\nWindows 7 / Vista：.themepack\r\n\r\n主题颜色、外观、播放间隔与随机选项仅用于导出的主题包。\r\n\r\n手动选图沿用原版暂未开放的范围。\r\n\r\n导出后由您自行安装，不会自动改变系统主题。",684,28,348,470,-1,window.hwnd)&&success;
    }
    return success;
}

bool ReplicaUi::AddControl(Window& window,const wchar_t* name,const wchar_t* type,const wchar_t* text,
                           int x,int y,int width,int height,int page,HWND parent) {
    const std::wstring kind(type);
    const wchar_t* cls=kind==L"Lib3.5"?L"STATIC":kind==L"Lib3.6"?L"BUTTON":kind==L"Lib3.16"?L"COMBOBOX":nullptr;
    if(!cls||!parent)return false;
    DWORD style=WS_CHILD|WS_CLIPSIBLINGS;
    if(kind==L"Lib3.5")style|=SS_LEFT|SS_NOPREFIX;
    else if(kind==L"Lib3.6")style|=WS_TABSTOP|BS_PUSHBUTTON|BS_OWNERDRAW;
    else style|=WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL;
    const UINT id=nextId_++;
    HWND child=CreateWindowExW(0,cls,text,style,MulDiv(x,dpi_,96),MulDiv(y,dpi_,96),MulDiv(width,dpi_,96),
        kind==L"Lib3.16"?MulDiv(height+170,dpi_,96):MulDiv(height,dpi_,96),parent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),instance_,nullptr);
    if(!child)return false;
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_?font_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    if(kind==L"Lib3.16")SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),MulDiv(height-6,dpi_,96));
    if(parent!=window.hwnd&&!AttachSubclass(window,parent))return false;
    Child state;state.name=name;state.type=kind;state.hwnd=child;state.id=id;state.page=page;
    window.children.push_back(std::move(state));
    if(page==-1||page==0)ShowWindow(child,SW_SHOWNA);
    else ShowWindow(child,SW_HIDE);
    return true;
}
bool ReplicaUi::PreTranslateMessage(MSG& message) {
    const HWND root=GetAncestor(message.hwnd,GA_ROOT);
    if(!WindowFor(root)||!IsWindowEnabled(root))return false;
    if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE) {
        const HWND focus=GetFocus();wchar_t cls[32]{};
        if(focus)GetClassNameW(focus,cls,_countof(cls));
        // Escape first dismisses an open combo, then closes the product window.
        if(_wcsicmp(cls,L"ComboBox")==0&&SendMessageW(focus,CB_GETDROPPEDSTATE,0,0))return false;
        SendMessageW(root,WM_CLOSE,0,0);return true;
    }
    return IsDialogMessageW(root,&message)!=FALSE;
}
void ReplicaUi::ActivatePage(Window& window,HWND tab,int page) {
    for(const auto& child:window.children)
        if(child.page!=-1&&GetParent(child.hwnd)==tab)
            ShowWindow(child.hwnd,child.visible&&child.page>=0&&child.page==page?SW_SHOWNA:SW_HIDE);
    // Group boxes have transparent interiors. Erase the complete tab surface
    // after hiding the old page so its image cannot remain behind them.
    RedrawWindow(tab,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
void ReplicaUi::LoadControlImage(Window& window,Child& child) {
    if(!IsImage(child.type)&&!(window.name==L"大图窗口"&&child.type==L"Lib3.8")) return;
    if(window.name==L"_启动窗口"&&(child.type==L"Lib3.7"||child.name==L"图片框EX1"))return;
    if(window.name==L"库管理窗口"&&child.name==L"图片框EX1")return;
    const std::wstring key=L"XML/"+window.name+L"/"+child.name+L"/扩展属性数据/0";
    const BYTE* data=nullptr;DWORD size=0;
    if(assets_.Bytes(key,&data,&size)) SetImage(window.name,child.name,{data,data+size});
}
bool ReplicaUi::AttachSubclass(Window& window,HWND target) {
    if(!target)return false;
    if(std::find(window.subclassed.begin(),window.subclassed.end(),target)!=window.subclassed.end())return true;
    if(!SetWindowSubclass(target,ParentProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) {
        OutputDebugStringW(L"ReplicaUi: SetWindowSubclass failed.\n");
        return false;
    }
    window.subclassed.push_back(target);
    return true;
}
void ReplicaUi::DetachSubclasses(Window& window) {
    for(HWND target:window.subclassed)
        if(target&&IsWindow(target))RemoveWindowSubclass(target,ParentProc,reinterpret_cast<UINT_PTR>(this));
    window.subclassed.clear();
}
void ReplicaUi::OnSubclassNcDestroy(HWND target) {
    for(auto& entry:windows_) {
        auto& handles=entry.second.subclassed;
        auto it=std::find(handles.begin(),handles.end(),target);
        if(it!=handles.end()) {
            RemoveWindowSubclass(target,ParentProc,reinterpret_cast<UINT_PTR>(this));
            handles.erase(it);
            return;
        }
    }
}
void ReplicaUi::DestroyWindowState(Window& window) {
    HWND h=window.hwnd;
    const HWND activeOwner=GetActiveWindow()==h?window.blockedOwner:nullptr;
    RestoreOwner(window);
    DetachSubclasses(window);
    for(auto& child:window.children)if(child.placeholderImages)ImageList_Destroy(child.placeholderImages);
    window.hwnd=nullptr;
    window.children.clear();
    if(h&&IsWindow(h))DestroyWindow(h);
    if(activeOwner&&IsWindow(activeOwner)&&IsWindowEnabled(activeOwner))SetActiveWindow(activeOwner);
    if(main_==h)main_=nullptr;
}
void ReplicaUi::RestoreOwner(Window& window) {
    const HWND owner=std::exchange(window.blockedOwner,nullptr);
    if(owner&&IsWindow(owner))EnableWindow(owner,TRUE);
}
HWND ReplicaUi::TopModalWindow(HWND window) const {
    if(!window)return nullptr;
    for(;;) {
        HWND next=nullptr;
        for(const auto& entry:windows_) {
            const auto& state=entry.second;
            if(state.hwnd&&state.blockedOwner==window) {next=state.hwnd;break;}
        }
        if(!next)return window;
        window=next;
    }
}
ReplicaUi::Window* ReplicaUi::WindowFor(HWND h) {
    for(auto& entry:windows_)if(entry.second.hwnd==h)return &entry.second;
    return nullptr;
}
ReplicaUi::Child* ReplicaUi::Find(HWND hwnd,const std::wstring& window,const std::wstring& control) {
    auto it=windows_.find(window);if(it==windows_.end())return nullptr;
    for(auto& c:it->second.children)if(c.name==control&&(!hwnd||GetParent(c.hwnd)==hwnd||it->second.hwnd==hwnd))return &c;
    for(auto& c:it->second.children)if(c.name==control)return &c;
    return nullptr;
}
void ReplicaUi::OnCommand(HWND hwnd,UINT id,UINT notification,HWND source) {
    if(hwnd!=main_ && !WindowFor(hwnd)) hwnd=GetAncestor(hwnd,GA_ROOT);
    Window* w=WindowFor(hwnd);if(!w||!handler_||!source||!IsWindowEnabled(hwnd))return;
    for(const auto& c:w->children)if(c.id==id&&c.hwnd==source) {
        if(c.type==L"Lib3.10"&&notification==BN_CLICKED)SendMessageW(source,BM_SETCHECK,BST_CHECKED,0);
        UiEvent e;e.window=w->name;e.control=c.name;e.notification=notification;
        const int len=GetWindowTextLengthW(source);std::wstring text(static_cast<size_t>(len)+1,L'\0');
        if(len)GetWindowTextW(source,text.data(),len+1);text.resize(static_cast<size_t>(len));e.text=text;
        if(c.type==L"Lib3.16")e.selection=static_cast<int>(SendMessageW(source,CB_GETCURSEL,0,0));
        else if(c.type==L"Lib3.18")e.selection=Selection(w->name,c.name);
        else if(c.type==L"Lib3.10"||c.type==L"Lib3.11")
            e.checked=SendMessageW(source,BM_GETCHECK,0,0)==BST_CHECKED;
        handler_(e);return;
    }
}
void ReplicaUi::OnClose(HWND h) {
    if(!IsWindowEnabled(h))return;
    if(h==main_){
        if(handler_) handler_({L"_启动窗口",L"__close__",0,L"",-1,false});
        return;
    }
    for(auto& entry:windows_)if(entry.second.hwnd==h){
        if(!closeGuard_||closeGuard_(entry.second.name))DestroyWindowState(entry.second);
        return;
    }
}
void ReplicaUi::OnNcDestroy(HWND h) {
    for(auto& entry:windows_)if(entry.second.hwnd==h) {
        RestoreOwner(entry.second);
        DetachSubclasses(entry.second);
        for(auto& child:entry.second.children)if(child.placeholderImages)ImageList_Destroy(child.placeholderImages);
        entry.second.hwnd=nullptr;
        entry.second.children.clear();
        if(main_==h)main_=nullptr;
        return;
    }
}
LRESULT CALLBACK ReplicaUi::WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    ReplicaUi* self=reinterpret_cast<ReplicaUi*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);self=static_cast<ReplicaUi*>(cs->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self) {
        if(m==WM_NCDESTROY)self->OnNcDestroy(h);
        else if(self->messageHandler_ && self->messageHandler_(m,w,l)) return 0;
        if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX)
            return reinterpret_cast<LRESULT>(self->OnControlColor(reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l),m));
        if(m==WM_SETTINGCHANGE||m==WM_THEMECHANGED||m==0x0320 /* WM_DWMCOLORIZATIONCOLORCHANGED */) {
            self->RefreshSystemAppearance();
            self->ApplyAppearance();
        }
        if(m==WM_COMMAND){self->OnCommand(h,LOWORD(w),HIWORD(w),reinterpret_cast<HWND>(l));return 0;}
        if(m==WM_NOTIFY)return self->OnNotify(h,reinterpret_cast<NMHDR*>(l));
        if(m==kRequestThumbnails) {
            if(self->handler_&&self->WindowFor(h)&&self->WindowFor(h)->name==L"库管理窗口")
                self->handler_({L"库管理窗口",L"__thumbnails__",0,L"",-1,false});
            return 0;
        }
        if(m==WM_CONTEXTMENU&&self->OnContextMenu(h,reinterpret_cast<HWND>(w),l))return 0;
        if(m==WM_DRAWITEM){self->OnDrawItem(h,reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(m==WM_MEASUREITEM) {
            auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            if(measure&&measure->CtlType==ODT_COMBOBOX) {measure->itemHeight=MulDiv(30,self->dpi_,96);return TRUE;}
        }
        if(m==WM_TIMER&&w==kViewerCursorTimer){self->OnViewerTimer(h);return 0;}
        if(m==WM_ERASEBKGND) {
            RECT bounds{};GetClientRect(h,&bounds);
            FillRect(reinterpret_cast<HDC>(w),&bounds,self->windowBrush_?self->windowBrush_:GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        if(m==WM_CLOSE){self->OnClose(h);return 0;}
        if(m==WM_DESTROY&&h==self->main_)PostQuitMessage(0);
    }
    return DefWindowProcW(h,m,w,l);
}
LRESULT CALLBACK ReplicaUi::ParentProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data) {
    auto* self=reinterpret_cast<ReplicaUi*>(data);
    if(self) {
        if(message==WM_NCDESTROY)self->OnSubclassNcDestroy(hwnd);
        wchar_t controlClass[32]{};
        GetClassNameW(hwnd,controlClass,_countof(controlClass));
        if(message==WM_PAINT&&_wcsicmp(controlClass,WC_TABCONTROLW)==0) {
            PAINTSTRUCT paint{};
            HDC dc=BeginPaint(hwnd,&paint);
            RECT bounds{};GetClientRect(hwnd,&bounds);
            FillRect(dc,&bounds,self->windowBrush_);
            const int count=TabCtrl_GetItemCount(hwnd);
            for(int index=0;index<count;++index) {
                DRAWITEMSTRUCT draw{};draw.CtlType=ODT_TAB;draw.hwndItem=hwnd;draw.hDC=dc;
                draw.itemID=static_cast<UINT>(index);
                draw.itemState=TabCtrl_GetCurSel(hwnd)==index?ODS_SELECTED:0;
                if(GetFocus()==hwnd)draw.itemState|=ODS_FOCUS;
                TabCtrl_GetItemRect(hwnd,index,&draw.rcItem);
                self->OnDrawItem(GetAncestor(hwnd,GA_ROOT),&draw);
            }
            EndPaint(hwnd,&paint);
            return 0;
        }
        if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX)
            return reinterpret_cast<LRESULT>(self->OnControlColor(reinterpret_cast<HDC>(wParam),reinterpret_cast<HWND>(lParam),message));
        if(message==WM_ERASEBKGND && (GetWindowLongPtrW(hwnd,GWL_STYLE)&BS_TYPEMASK)==BS_GROUPBOX) {
            wchar_t cls[16]{};GetClassNameW(hwnd,cls,_countof(cls));
            if(_wcsicmp(cls,L"Button")==0) {
                RECT bounds{};GetClientRect(hwnd,&bounds);
                FillRect(reinterpret_cast<HDC>(wParam),&bounds,self->windowBrush_?self->windowBrush_:GetSysColorBrush(COLOR_WINDOW));
                return 1;
            }
        }
        HWND root=GetAncestor(hwnd,GA_ROOT);
        if(_wcsicmp(controlClass,WC_LISTVIEWW)==0&&
           (message==WM_PAINT||message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_KEYUP))
            PostMessageW(root,kRequestThumbnails,0,0);
        if(message==WM_CONTEXTMENU) {
            HWND source=reinterpret_cast<HWND>(wParam);
            if(!source)source=hwnd;
            if(self->OnContextMenu(root,source,lParam))return 0;
        }
        if(message==WM_COMMAND){self->OnCommand(root,LOWORD(wParam),HIWORD(wParam),reinterpret_cast<HWND>(lParam));return 0;}
        if(message==WM_NOTIFY)return self->OnNotify(root,reinterpret_cast<NMHDR*>(lParam));
        if(message==WM_DRAWITEM){self->OnDrawItem(root,reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;}
        if(message==WM_MEASUREITEM) {
            auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if(measure&&measure->CtlType==ODT_COMBOBOX) {measure->itemHeight=MulDiv(30,self->dpi_,96);return TRUE;}
        }
    }
    return DefSubclassProc(hwnd,message,wParam,lParam);
}
void ReplicaUi::Quit() {
    if(main_ && IsWindow(main_)) {
        Window* mainWindow=WindowFor(main_);
        if(mainWindow)DestroyWindowState(*mainWindow);
    }
}
LRESULT ReplicaUi::OnNotify(HWND hwnd,NMHDR* hdr) {
    Window* w=WindowFor(hwnd);if(!w||!hdr)return 0;
    for(const auto& c:w->children)if(c.hwnd==hdr->hwndFrom) {
        if(c.type==L"Lib3.9"&&hdr->code==NM_CUSTOMDRAW) {
            auto* custom=reinterpret_cast<NMCUSTOMDRAW*>(hdr);
            if(custom->dwDrawStage==CDDS_PREPAINT) {
                DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.hwndItem=c.hwnd;
                draw.hDC=custom->hdc;draw.rcItem=custom->rc;
                draw.itemState=IsWindowEnabled(c.hwnd)?0:ODS_DISABLED;
                if(GetFocus()==c.hwnd)draw.itemState|=ODS_FOCUS;
                OnDrawItem(hwnd,&draw);return CDRF_SKIPDEFAULT;
            }
        }
        if(w->name==L"库管理窗口"&&c.type==L"Lib3.18") {
            if(hdr->code==NM_CUSTOMDRAW) {
                auto* custom=reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
                if(custom->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                if(custom->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_LISTBOX;draw.hwndItem=c.hwnd;
                    draw.itemID=static_cast<UINT>(custom->nmcd.dwItemSpec);draw.hDC=custom->nmcd.hdc;
                    draw.itemState=ListView_GetItemState(c.hwnd,draw.itemID,LVIS_SELECTED)?ODS_SELECTED:0;
                    if(GetFocus()==c.hwnd&&ListView_GetItemState(c.hwnd,draw.itemID,LVIS_FOCUSED))draw.itemState|=ODS_FOCUS;
                    ListView_GetItemRect(c.hwnd,draw.itemID,&draw.rcItem,LVIR_ICON);
                    draw.rcItem.right=draw.rcItem.left+MulDiv(202,dpi_,96);
                    draw.rcItem.bottom=draw.rcItem.top+MulDiv(162,dpi_,96);
                    OnDrawItem(hwnd,&draw);
                    return CDRF_SKIPDEFAULT;
                }
            }
            if(hdr->code==LVN_ITEMCHANGED) {
                const auto* changed=reinterpret_cast<NMLISTVIEW*>(hdr);
                if(!suppressEvents_&&(changed->uChanged&LVIF_STATE)&&(changed->uNewState&LVIS_SELECTED)&&!(changed->uOldState&LVIS_SELECTED)) {
                    if(handler_)handler_({w->name,c.name,LBN_SELCHANGE,L"",changed->iItem,false});
                }
            }
            return 0;
        }
        UiEvent e;e.window=w->name;e.control=c.name;e.notification=hdr->code;
        if(c.type==L"Lib3.3"&&hdr->code==TCN_SELCHANGE) {
            e.selection=TabCtrl_GetCurSel(hdr->hwndFrom);
            ActivatePage(*w,hdr->hwndFrom,e.selection);
        }
        if(handler_) handler_(e);
        return 0;
    }
    return 0;
}
void ReplicaUi::OnViewerTimer(HWND hwnd) {
    auto* window=WindowFor(hwnd);
    if(!window||window->name!=L"大图窗口")return;
    auto* button=Find(hwnd,window->name,L"超级按钮EX1");
    if(!button)return;
    POINT current{};
    RECT bounds{};
    if(!GetCursorPos(&current)||!GetWindowRect(button->hwnd,&bounds))return;
    const POINT previous=window->viewerCursor;
    window->viewerCursor=current;
    const BYTE opacity=PtInRect(&bounds,previous)?222:
        previous.x==current.x&&previous.y==current.y?40:166;
    if(window->viewerCloseOpacity==opacity)return;
    window->viewerCloseOpacity=opacity;
    InvalidateRect(button->hwnd,nullptr,FALSE);
}

void ReplicaUi::OnDrawItem(HWND hwnd,DRAWITEMSTRUCT* draw) {
    if(!draw||(draw->CtlType!=ODT_STATIC&&draw->CtlType!=ODT_BUTTON&&draw->CtlType!=ODT_LISTBOX&&draw->CtlType!=ODT_TAB&&draw->CtlType!=ODT_COMBOBOX))return;
    Window* window=WindowFor(hwnd);if(!window)return;
    for(const auto& child:window->children)if(child.hwnd==draw->hwndItem) {
        if(draw->CtlType==ODT_COMBOBOX) {
            const bool selected=(draw->itemState&ODS_SELECTED)!=0;
            HBRUSH brush=CreateSolidBrush(selected?accentColor_:panelColor_);
            FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
            wchar_t text[1024]{};
            if(draw->itemID!=static_cast<UINT>(-1))SendMessageW(child.hwnd,CB_GETLBTEXT,draw->itemID,reinterpret_cast<LPARAM>(text));
            RECT caption=draw->rcItem;caption.left+=MulDiv(10,dpi_,96);caption.right-=4;
            SetBkMode(draw->hDC,TRANSPARENT);
            SetTextColor(draw->hDC,(draw->itemState&ODS_DISABLED)?mutedTextColor_:selected?accentInkColor_:textColor_);
            if(font_)SelectObject(draw->hDC,font_);
            DrawTextW(draw->hDC,text,-1,&caption,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);
            return;
        }
        if(draw->CtlType==ODT_TAB) {
            wchar_t text[128]{};
            TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=_countof(text);
            TabCtrl_GetItem(child.hwnd,draw->itemID,&item);
            const bool selected=(draw->itemState&ODS_SELECTED)!=0;
            FillRect(draw->hDC,&draw->rcItem,windowBrush_);
            SetBkMode(draw->hDC,TRANSPARENT);
            SetTextColor(draw->hDC,selected?accentTextColor_:mutedTextColor_);
            if(font_)SelectObject(draw->hDC,font_);
            DrawTextW(draw->hDC,text,-1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(selected) {
                RECT line=draw->rcItem;line.top=line.bottom-MulDiv(3,dpi_,96);
                line.left+=20;line.right-=20;
                HBRUSH brush=CreateSolidBrush(accentTextColor_);FillRect(draw->hDC,&line,brush);DeleteObject(brush);
            }
            if((draw->itemState&ODS_FOCUS)&&selected)DrawFocusRect(draw->hDC,&draw->rcItem);
            return;
        }
        if(draw->CtlType==ODT_LISTBOX&&child.type==L"Lib3.18") {
            const bool selected=(draw->itemState&ODS_SELECTED)!=0;
            const COLORREF fill=selected?RGB(GetRValue(accentColor_)*3/10+GetRValue(panelColor_)*7/10,
                GetGValue(accentColor_)*3/10+GetGValue(panelColor_)*7/10,
                GetBValue(accentColor_)*3/10+GetBValue(panelColor_)*7/10):panelColor_;
            HBRUSH brush=CreateSolidBrush(fill);
            FillRect(draw->hDC,&draw->rcItem,brush);
            DeleteObject(brush);
            HPEN pen=CreatePen(PS_SOLID,1,borderColor_);
            HGDIOBJ oldPen=SelectObject(draw->hDC,pen);
            HGDIOBJ oldBrush=SelectObject(draw->hDC,GetStockObject(HOLLOW_BRUSH));
            RoundRect(draw->hDC,draw->rcItem.left+2,draw->rcItem.top+2,draw->rcItem.right-2,
                draw->rcItem.bottom-2,8,8);
            SelectObject(draw->hDC,oldBrush);SelectObject(draw->hDC,oldPen);DeleteObject(pen);
            const int item=static_cast<int>(draw->itemID);
            auto image=child.tileImages.find(static_cast<size_t>(item));
            if(image!=child.tileImages.end()&&!image->second.pixels.empty()&&d2d_) {
                const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
                if(!imageTarget_)d2d_->CreateDCRenderTarget(&properties,imageTarget_.GetAddressOf());
                if(imageTarget_&&SUCCEEDED(imageTarget_->BindDC(draw->hDC,&draw->rcItem))) {
                    const auto format=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
                    auto& tile=image->second;
                    if(!tile.bitmap)imageTarget_->CreateBitmap(D2D1::SizeU(tile.width,tile.height),tile.pixels.data(),
                        tile.width*4,format,tile.bitmap.GetAddressOf());
                    imageTarget_->BeginDraw();
                    if(tile.bitmap) {
                        const float x=((draw->rcItem.right-draw->rcItem.left)-static_cast<float>(tile.width))/2.f;
                        imageTarget_->DrawBitmap(tile.bitmap.Get(),D2D1::RectF(x,6.f,x+tile.width,6.f+tile.height));
                    }
                    if(imageTarget_->EndDraw()==D2DERR_RECREATE_TARGET)ResetImageTarget();
                }
            }
            wchar_t text[1024]{};
            if(draw->itemID!=static_cast<UINT>(-1))ListView_GetItemText(child.hwnd,draw->itemID,0,text,_countof(text));
            SetBkMode(draw->hDC,TRANSPARENT);
            SetTextColor(draw->hDC,textColor_);
            if(font_)SelectObject(draw->hDC,font_);
            RECT caption=draw->rcItem;
            caption.left+=MulDiv(8,dpi_,96);caption.right-=MulDiv(8,dpi_,96);
            caption.top+=MulDiv(112,dpi_,96);caption.bottom=caption.top+MulDiv(22,dpi_,96);
            const std::wstring value(text);
            const auto split=value.find(L"\r\n");
            const auto title=value.substr(0,split);
            DrawTextW(draw->hDC,title.c_str(),-1,&caption,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(split!=std::wstring::npos) {
                caption.top+=MulDiv(24,dpi_,96);caption.bottom=draw->rcItem.bottom-2;
                SetTextColor(draw->hDC,mutedTextColor_);
                DrawTextW(draw->hDC,value.c_str()+split+2,-1,&caption,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);
            return;
        }
        const bool styledButton=draw->CtlType==ODT_BUTTON&&
            (child.type==L"Lib3.6"||child.type==L"Lib3.9"||child.type==L"Lib3.10"||child.type==L"Lib3.11"||
             (child.type==L"Lib3.8"&&child.image.empty())||(child.type==L"Lib3.7"&&child.image.empty()));
        if(styledButton) {
            const bool disabled=(draw->itemState&ODS_DISABLED)!=0;
            const bool checked=SendMessageW(child.hwnd,BM_GETCHECK,0,0)==BST_CHECKED;
            const bool radio=child.type==L"Lib3.10";
            const bool checkbox=child.type==L"Lib3.11";
            const bool themeVisible=IsWindowVisible(WindowHandle(L"主题包窗口"))!=FALSE;
            const bool workbenchPage=window->name==L"库管理窗口"&&
                (child.name==(themeVisible?L"按钮EX4":L"浏览页"));
            const bool primary=workbenchPage||child.name==L"超级按钮EX1"||child.name==L"多功能按钮EX1"||
                (window->name==L"主题包窗口"&&child.name==L"按钮EX1");
            const bool filled=primary||((radio||checkbox)&&checked);
            const COLORREF background=filled?(disabled?borderColor_:accentColor_):panelColor_;
            const COLORREF foreground=filled?accentInkColor_:textColor_;
            HBRUSH brush=CreateSolidBrush(background);
            HPEN pen=CreatePen(PS_SOLID,1,filled?accentColor_:borderColor_);
            HGDIOBJ oldBrush=SelectObject(draw->hDC,brush),oldPen=SelectObject(draw->hDC,pen);
            RoundRect(draw->hDC,draw->rcItem.left+1,draw->rcItem.top+1,draw->rcItem.right-1,
                draw->rcItem.bottom-1,10,10);
            SelectObject(draw->hDC,oldBrush);SelectObject(draw->hDC,oldPen);
            DeleteObject(brush);DeleteObject(pen);
            wchar_t text[512]{};GetWindowTextW(child.hwnd,text,_countof(text));
            RECT caption=draw->rcItem;
            if(child.type==L"Lib3.9") {
                const int split=draw->rcItem.right-MulDiv(28,dpi_,96);
                HPEN divider=CreatePen(PS_SOLID,1,RGB(220,225,230));
                HGDIOBJ old=SelectObject(draw->hDC,divider);
                MoveToEx(draw->hDC,split,draw->rcItem.top+8,nullptr);LineTo(draw->hDC,split,draw->rcItem.bottom-8);
                const int x=(split+draw->rcItem.right)/2,y=(draw->rcItem.top+draw->rcItem.bottom)/2;
                MoveToEx(draw->hDC,x-4,y-2,nullptr);LineTo(draw->hDC,x,y+2);LineTo(draw->hDC,x+4,y-2);
                SelectObject(draw->hDC,old);DeleteObject(divider);caption.right=split;
            }
            if(radio||checkbox) {
                const int mark=MulDiv(16,dpi_,96);
                RECT box{caption.left+12,(caption.top+caption.bottom-mark)/2,
                         caption.left+12+mark,(caption.top+caption.bottom+mark)/2};
                HBRUSH markBrush=CreateSolidBrush(checked?RGB(255,255,255):background);
                HPEN markPen=CreatePen(PS_SOLID,1,checked?RGB(255,255,255):borderColor_);
                HGDIOBJ keepBrush=SelectObject(draw->hDC,markBrush),keepPen=SelectObject(draw->hDC,markPen);
                if(radio)Ellipse(draw->hDC,box.left,box.top,box.right,box.bottom);
                else RoundRect(draw->hDC,box.left,box.top,box.right,box.bottom,3,3);
                if(checked&&radio) {
                    HBRUSH dot=CreateSolidBrush(accentColor_);HGDIOBJ old=SelectObject(draw->hDC,dot);
                    Ellipse(draw->hDC,box.left+4,box.top+4,box.right-4,box.bottom-4);
                    SelectObject(draw->hDC,old);DeleteObject(dot);
                } else if(checked&&checkbox) {
                    HPEN checkPen=CreatePen(PS_SOLID,2,accentColor_);HGDIOBJ old=SelectObject(draw->hDC,checkPen);
                    MoveToEx(draw->hDC,box.left+3,box.top+8,nullptr);LineTo(draw->hDC,box.left+7,box.top+12);
                    LineTo(draw->hDC,box.left+13,box.top+4);SelectObject(draw->hDC,old);DeleteObject(checkPen);
                }
                SelectObject(draw->hDC,keepBrush);SelectObject(draw->hDC,keepPen);
                DeleteObject(markBrush);DeleteObject(markPen);
                caption.left+=36;
            }
            SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,disabled?mutedTextColor_:foreground);
            if(font_)SelectObject(draw->hDC,font_);
            DrawTextW(draw->hDC,text,-1,&caption,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);
            return;
        }
        const bool viewer=window->name==L"大图窗口";
        HBRUSH background=CreateSolidBrush(viewer?RGB(0,0,0):windowColor_);
        FillRect(draw->hDC,&draw->rcItem,background);DeleteObject(background);
        if(!d2d_)return;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        if(!imageTarget_&&FAILED(d2d_->CreateDCRenderTarget(&properties,imageTarget_.GetAddressOf())))return;
        if(FAILED(imageTarget_->BindDC(draw->hDC,&draw->rcItem)))return;
        const auto format=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        const float width=static_cast<float>(draw->rcItem.right-draw->rcItem.left);
        const float height=static_cast<float>(draw->rcItem.bottom-draw->rcItem.top);
        const auto paintImage=[&](const Child& picture,float canvasWidth,float canvasHeight,float opacity) {
            if(picture.image.empty())return;
            if(!picture.imageBitmap && FAILED(imageTarget_->CreateBitmap(D2D1::SizeU(picture.imageWidth,picture.imageHeight),
                picture.image.data(),picture.imageWidth*4,format,picture.imageBitmap.GetAddressOf())))return;
            const auto& bitmap=picture.imageBitmap;
            if(picture.imageMode==ImageMode::Tile) {
                ComPtr<ID2D1BitmapBrush> brush;
                const auto repeat=D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP,D2D1_EXTEND_MODE_WRAP);
                if(SUCCEEDED(imageTarget_->CreateBitmapBrush(bitmap.Get(),repeat,brush.GetAddressOf())))
                    imageTarget_->FillRectangle(D2D1::RectF(0,0,canvasWidth,canvasHeight),brush.Get());
                return;
            }
            float w=static_cast<float>(picture.imageWidth),h=static_cast<float>(picture.imageHeight);
            if(picture.imageMode==ImageMode::Stretch) {w=canvasWidth;h=canvasHeight;}
            if(picture.imageMode==ImageMode::Fit) {const float scale=(std::min)(canvasWidth/w,canvasHeight/h);w*=scale;h*=scale;}
            const float x=(canvasWidth-w)/2,y=(canvasHeight-h)/2;
            imageTarget_->DrawBitmap(bitmap.Get(),D2D1::RectF(x,y,x+w,y+h),opacity);
        };
        imageTarget_->BeginDraw();
        const COLORREF clearColor=viewer?RGB(0,0,0):windowColor_;
        imageTarget_->Clear(D2D1::ColorF(GetRValue(clearColor)/255.f,GetGValue(clearColor)/255.f,GetBValue(clearColor)/255.f));
        float opacity=(draw->CtlType==ODT_BUTTON&&(draw->itemState&ODS_DISABLED))?0.4f:1.f;
        if(viewer&&child.type==L"Lib3.8") {
            // Composite the button on the same wallpaper pixels as its parent.
            if(auto* picture=Find(window->hwnd,window->name,L"图片框EX1")) {
                POINT offset{};
                RECT bounds{};
                MapWindowPoints(child.hwnd,picture->hwnd,&offset,1);
                GetClientRect(picture->hwnd,&bounds);
                imageTarget_->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(offset.x),-static_cast<float>(offset.y)));
                paintImage(*picture,static_cast<float>(bounds.right),static_cast<float>(bounds.bottom),1.f);
                imageTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
            }
            opacity=window->viewerCloseOpacity/255.f;
        }
        paintImage(child,width,height,opacity);
        if(imageTarget_->EndDraw()==D2DERR_RECREATE_TARGET)ResetImageTarget();
        if(draw->CtlType==ODT_BUTTON&&(draw->itemState&ODS_FOCUS)) DrawFocusRect(draw->hDC,&draw->rcItem);
        return;
    }
}
bool ReplicaUi::OnContextMenu(HWND hwnd,HWND source,LPARAM) {
    Window* w=WindowFor(hwnd);if(!w||!handler_||!source||!IsWindowEnabled(hwnd))return false;
    for(const auto& c:w->children)if(c.hwnd==source&&c.type==L"Lib3.4") {
        UiEvent e;e.window=w->name;e.control=c.name;e.notification=WM_CONTEXTMENU;
        handler_(e);return true;
    }
    return false;
}
void ReplicaUi::Show(const std::wstring& n, HWND owner) {
    auto it=windows_.find(n);
    if(it==windows_.end()||!it->second.hwnd) {
        if(owner&&!IsWindowEnabled(owner))return;
        HWND h=CreateProductWindow(n.c_str(),owner);
        if(!h)return;
        it=windows_.find(n);
        if(it==windows_.end()||!it->second.hwnd)return;
        if(owner&&n!=L"主题包窗口") {
            it->second.blockedOwner=owner;
            EnableWindow(owner,FALSE);
        }
    }
    if(n==L"主题包窗口") {
        ShowWorkbenchPage(true);
        return;
    }
    const HWND top=TopModalWindow(it->second.hwnd);
    ShowWindow(top,SW_SHOWNORMAL);
    SetForegroundWindow(top);
}
void ReplicaUi::Close(const std::wstring& n) {auto it=windows_.find(n);if(it!=windows_.end())DestroyWindowState(it->second);}
HWND ReplicaUi::WindowHandle(const std::wstring& n) const {
    const auto it=windows_.find(n);
    return it==windows_.end()?nullptr:it->second.hwnd;
}
void ReplicaUi::SetText(const std::wstring& w,const std::wstring& c,const std::wstring& v){auto* x=Find(nullptr,w,c);if(x)SetWindowTextW(x->hwnd,v.c_str());}
void ReplicaUi::SetChecked(const std::wstring& w,const std::wstring& c,bool v){auto* x=Find(nullptr,w,c);if(x)SendMessageW(x->hwnd,BM_SETCHECK,v?BST_CHECKED:BST_UNCHECKED,0);}
void ReplicaUi::SetSelection(const std::wstring& w,const std::wstring& c,int i){
    auto* x=Find(nullptr,w,c);if(!x)return;
    if(x->type==L"Lib3.16")SendMessageW(x->hwnd,CB_SETCURSEL,i,0);
    else if(x->type==L"Lib3.18"&&w==L"库管理窗口") {
        const bool previous=std::exchange(suppressEvents_,true);
        ListView_SetItemState(x->hwnd,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        if(i>=0)ListView_SetItemState(x->hwnd,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        suppressEvents_=previous;
    }
    else if(x->type==L"Lib3.18")SendMessageW(x->hwnd,LB_SETCURSEL,i,0);
    else if(x->type==L"Lib3.3"&&i>=0&&i<TabCtrl_GetItemCount(x->hwnd)) {
        TabCtrl_SetCurSel(x->hwnd,i);ActivatePage(windows_.at(w),x->hwnd,i);
    }
}
int ReplicaUi::Selection(const std::wstring& w,const std::wstring& c) {
    auto* x=Find(nullptr,w,c);if(!x)return -1;
    if(x->type==L"Lib3.16")return static_cast<int>(SendMessageW(x->hwnd,CB_GETCURSEL,0,0));
    if(x->type==L"Lib3.18")return w==L"库管理窗口"?ListView_GetNextItem(x->hwnd,-1,LVNI_SELECTED):
        static_cast<int>(SendMessageW(x->hwnd,LB_GETCURSEL,0,0));
    if(x->type==L"Lib3.3")return TabCtrl_GetCurSel(x->hwnd);
    return static_cast<int>(SendMessageW(x->hwnd,BM_GETCHECK,0,0));
}
std::wstring ReplicaUi::GetText(const std::wstring& w,const std::wstring& c) {
    auto* x=Find(nullptr,w,c);if(!x)return {};
    const int n=GetWindowTextLengthW(x->hwnd);std::wstring value(static_cast<size_t>(n)+1,L'\0');
    value.resize(static_cast<size_t>(GetWindowTextW(x->hwnd,value.data(),n+1)));return value;
}
void ReplicaUi::SetItems(const std::wstring& w,const std::wstring& c,const std::vector<std::wstring>& values){
    auto* x=Find(nullptr,w,c);if(!x)return;
    if(x->type==L"Lib3.16"){
        SendMessageW(x->hwnd,CB_RESETCONTENT,0,0);
        for(const auto& v:values)SendMessageW(x->hwnd,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(v.c_str()));
    }else if(x->type==L"Lib3.18"){
        if(w==L"库管理窗口") {
            const bool previous=std::exchange(suppressEvents_,true);
            SendMessageW(x->hwnd,WM_SETREDRAW,FALSE,0);
            ListView_DeleteAllItems(x->hwnd);
            for(size_t index=0;index<values.size();++index) {
                LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(index);
                item.pszText=const_cast<wchar_t*>(values[index].c_str());item.iImage=0;
                ListView_InsertItem(x->hwnd,&item);
            }
            SendMessageW(x->hwnd,WM_SETREDRAW,TRUE,0);
            InvalidateRect(x->hwnd,nullptr,TRUE);
            suppressEvents_=previous;
            return;
        }
        SendMessageW(x->hwnd,LB_RESETCONTENT,0,0);
        for(const auto& v:values)SendMessageW(x->hwnd,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(v.c_str()));
    }
}
void ReplicaUi::SetEnabled(const std::wstring& w,const std::wstring& c,bool v){auto* x=Find(nullptr,w,c);if(x)EnableWindow(x->hwnd,v);}
void ReplicaUi::SetVisible(const std::wstring& w,const std::wstring& c,bool visible) {
    auto* x=Find(nullptr,w,c);if(!x)return;
    x->visible=visible;
    ShowWindow(x->hwnd,visible&&(x->page==-1||(x->page>=0&&TabCtrl_GetCurSel(GetParent(x->hwnd))==x->page))?SW_SHOWNA:SW_HIDE);
}
void ReplicaUi::SetProgress(const std::wstring& w,const std::wstring& c,int position,bool busy) {
    auto* child=Find(nullptr,w,c);
    if(!child||child->type!=L"Lib3.12")return;
    const HWND hwnd=child->hwnd;
    const LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    if(!busy)SendMessageW(hwnd,PBM_SETMARQUEE,FALSE,0);
    SetWindowLongPtrW(hwnd,GWL_STYLE,busy?style|PBS_MARQUEE:style&~PBS_MARQUEE);
    if(busy)SendMessageW(hwnd,PBM_SETMARQUEE,TRUE,50);
    else SendMessageW(hwnd,PBM_SETPOS,position,0);
}
void ReplicaUi::SetImageMode(const std::wstring& w,const std::wstring& c,ImageMode mode) {
    auto* child=Find(nullptr,w,c);if(!child)return;
    child->imageMode=mode;InvalidateRect(child->hwnd,nullptr,FALSE);
    if(w==L"大图窗口")if(auto* button=Find(nullptr,w,L"超级按钮EX1"))InvalidateRect(button->hwnd,nullptr,FALSE);
}
bool ReplicaUi::SetImage(const std::wstring& w,const std::wstring& c,const std::vector<unsigned char>& bytes) {
    auto* child=Find(nullptr,w,c);if(!child||(!IsImage(child->type)&&!(w==L"大图窗口"&&child->type==L"Lib3.8")))return false;
    if(bytes.empty()){
        child->image.clear();child->imageWidth=child->imageHeight=0;child->imageBitmap.Reset();
        InvalidateRect(child->hwnd,nullptr,TRUE);return true;
    }
    if(bytes.size()>(std::numeric_limits<UINT>::max)())return false;
    std::vector<BYTE> copy(bytes.begin(),bytes.end());
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return false;
    ComPtr<IWICStream> stream;if(FAILED(factory->CreateStream(&stream)))return false;
    if(FAILED(stream->InitializeFromMemory(copy.data(),static_cast<UINT>(copy.size()))))return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return false;
    ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return false;
    UINT width=0,height=0;if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>12000||height>12000)return false;
    const bool wallpaper=c==L"图片框EX1";
    const double scale=wallpaper
        ? (std::min)({1.0,1920.0/width,1080.0/height})
        : (std::min)(1.0,1200.0/(std::max)(static_cast<double>(width),static_cast<double>(height)));
    const UINT targetWidth=(std::max)(1u,static_cast<UINT>(width*scale));
    const UINT targetHeight=(std::max)(1u,static_cast<UINT>(height*scale));
    // Scale the decoder source before converting to 32-bit pixels, allowing
    // WIC's JPEG decoder to use its reduced-resolution decode path.
    ComPtr<IWICBitmapScaler> scaler;IWICBitmapSource* source=frame.Get();
    if(targetWidth!=width||targetHeight!=height) {
        if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(frame.Get(),targetWidth,targetHeight,WICBitmapInterpolationModeFant)))return false;
        source=scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter)))return false;
    if(FAILED(converter->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut)))return false;
    const UINT stride=targetWidth*4;
    if(targetHeight>(std::numeric_limits<UINT>::max)()/stride)return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride)*targetHeight);
    if(FAILED(converter->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data())))return false;
    child->imageBitmap.Reset();
    child->image=std::move(pixels);child->imageWidth=targetWidth;child->imageHeight=targetHeight;
    if(w==L"大图窗口"&&c==L"图片框EX1")if(auto* button=Find(nullptr,w,L"超级按钮EX1"))InvalidateRect(button->hwnd,nullptr,FALSE);
    InvalidateRect(child->hwnd,nullptr,TRUE);return true;
}

void ReplicaUi::ClearListImages(const std::wstring& w,const std::wstring& c) {
    if(auto* child=Find(nullptr,w,c))child->tileImages.clear();
}

std::vector<size_t> ReplicaUi::VisibleListItems(const std::wstring& w,const std::wstring& c) const {
    std::vector<size_t> result;
    const auto window=windows_.find(w);
    if(window==windows_.end())return result;
    for(const auto& child:window->second.children)if(child.name==c&&IsWindowVisible(child.hwnd)) {
        RECT client{};GetClientRect(child.hwnd,&client);
        const int count=ListView_GetItemCount(child.hwnd);
        for(int index=0;index<count&&result.size()<6;++index) {
            if(child.tileImages.find(static_cast<size_t>(index))!=child.tileImages.end())continue;
            RECT item{},intersection{};
            if(ListView_GetItemRect(child.hwnd,index,&item,LVIR_BOUNDS)&&IntersectRect(&intersection,&client,&item))
                result.push_back(static_cast<size_t>(index));
        }
    }
    return result;
}

void ReplicaUi::ResetImageTarget() {
    for(auto& entry:windows_)for(auto& child:entry.second.children) {
        child.imageBitmap.Reset();
        for(auto& tile:child.tileImages)tile.second.bitmap.Reset();
    }
    imageTarget_.Reset();
}

void ReplicaUi::ShowWorkbenchPage(bool theme) {
    auto gallery=windows_.find(L"库管理窗口");
    if(gallery==windows_.end()||!gallery->second.hwnd)return;
    for(const auto& child:gallery->second.children) {
        const bool navigation=child.name==L"浏览页"||child.name==L"按钮EX4";
        ShowWindow(child.hwnd,child.visible&&(navigation||!theme)?SW_SHOWNA:SW_HIDE);
    }
    const HWND panel=WindowHandle(L"主题包窗口");
    if(panel)ShowWindow(panel,theme?SW_SHOWNA:SW_HIDE);
    for(const auto& child:gallery->second.children)
        if(child.name==L"浏览页"||child.name==L"按钮EX4")InvalidateRect(child.hwnd,nullptr,TRUE);
    InvalidateRect(gallery->second.hwnd,nullptr,TRUE);
    if(theme&&panel)SetFocus(panel);
}

bool ReplicaUi::SetListItemImage(const std::wstring& w,const std::wstring& c,size_t index,
                                 const std::vector<unsigned char>& bytes) {
    auto* child=Find(nullptr,w,c);
    if(!child||child->type!=L"Lib3.18"||bytes.size()>(std::numeric_limits<UINT>::max)())return false;
    // Cache failed/missing previews too, so repainting cannot cause a retry loop.
    if(child->tileImages.size()>=64&&child->tileImages.find(index)==child->tileImages.end())
        child->tileImages.erase(child->tileImages.begin());
    child->tileImages[index]=Child::TileImage{};
    if(bytes.empty())return true;
    std::vector<BYTE> copy(bytes.begin(),bytes.end());
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return false;
    ComPtr<IWICStream> stream;
    if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(copy.data(),static_cast<UINT>(copy.size()))))return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(decoder->GetFrame(0,&frame)))return false;
    UINT width=0,height=0;
    if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>12000||height>12000)return false;
    const double scale=(std::min)({1.0,MulDiv(180,dpi_,96)/static_cast<double>(width),
        MulDiv(102,dpi_,96)/static_cast<double>(height)});
    const UINT targetWidth=(std::max)(1u,static_cast<UINT>(width*scale));
    const UINT targetHeight=(std::max)(1u,static_cast<UINT>(height*scale));
    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* source=frame.Get();
    if(targetWidth!=width||targetHeight!=height) {
        if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(frame.Get(),targetWidth,targetHeight,WICBitmapInterpolationModeFant)))return false;
        source=scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut)))return false;
    const UINT stride=targetWidth*4;
    if(targetHeight>(std::numeric_limits<UINT>::max)()/stride)return false;
    Child::TileImage image;image.width=targetWidth;image.height=targetHeight;
    image.pixels.resize(static_cast<size_t>(stride)*targetHeight);
    if(FAILED(converter->CopyPixels(nullptr,stride,static_cast<UINT>(image.pixels.size()),image.pixels.data())))return false;
    if(child->tileImages.size()>=64&&child->tileImages.find(index)==child->tileImages.end())
        child->tileImages.erase(child->tileImages.begin());
    child->tileImages[index]=std::move(image);
    RECT item{};
    if(ListView_GetItemRect(child->hwnd,index,&item,LVIR_BOUNDS))
        InvalidateRect(child->hwnd,&item,FALSE);
    else InvalidateRect(child->hwnd,nullptr,FALSE);
    return true;
}
} // namespace stoutwall
