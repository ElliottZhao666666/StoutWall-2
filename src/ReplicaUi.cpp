#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ReplicaUi.hpp"
#include "OriginalLayout.hpp"
#include "Assets.hpp"
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>
#include <utility>

#pragma comment(lib,"comctl32.lib")

namespace stoutwall {
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t kClassName[]=L"StoutWallOriginalWindow";
constexpr UINT_PTR kViewerCursorTimer=187;
const WindowDef* Definition(const std::wstring& name) {
    for(const auto& w:OriginalWindows) if(name==w.name) return &w;
    return nullptr;
}
bool IsImage(const std::wstring& type) { return type==L"Lib3.4"||type==L"Lib3.7"; }
bool ReadOnlyEdit(const ControlDef& c) {
    return std::wstring(c.window)!=L"库迁移窗口"&&std::wstring(c.window)!=L"主题包窗口";
}
DWORD Style(const ControlDef& c) {
    const std::wstring t=c.type;
    // The EXUI snapshot marks the host controls hidden while the original
    // program makes them visible during startup. Recreate that runtime state.
    const DWORD base=WS_CHILD|WS_CLIPSIBLINGS;
    if(t==L"Lib3.2") return base|BS_GROUPBOX|WS_CLIPCHILDREN;
    if(t==L"Lib3.3") return base|WS_TABSTOP|TCS_TABS|TCS_SINGLELINE|TCS_FIXEDWIDTH|WS_CLIPCHILDREN;
    if(t==L"Lib3.5") return base|SS_LEFT|SS_NOPREFIX|SS_NOTIFY;
    if(t==L"Lib3.7"||(t==L"Lib3.8"&&std::wstring(c.window)==L"大图窗口")) return base|WS_TABSTOP|BS_OWNERDRAW;
    if(t==L"Lib3.6"||t==L"Lib3.8"||t==L"Lib3.9") return base|WS_TABSTOP|BS_PUSHBUTTON;
    if(t==L"Lib3.10") return base|WS_TABSTOP|BS_AUTORADIOBUTTON|BS_NOTIFY;
    if(t==L"Lib3.11") return base|WS_TABSTOP|BS_AUTOCHECKBOX|BS_NOTIFY|BS_MULTILINE;
    if(t==L"Lib3.12") return base|PBS_SMOOTH;
    if(t==L"Lib3.15") return base|WS_TABSTOP|WS_BORDER|ES_LEFT|(ReadOnlyEdit(c)
        ? ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL : ES_AUTOHSCROLL);
    if(t==L"Lib3.16") return base|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL;
    if(t==L"Lib3.18") return base|WS_TABSTOP|WS_BORDER|LBS_NOTIFY|WS_VSCROLL;
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
    if(t==L"Lib3.18") return L"LISTBOX";
    if(t==L"Lib3.12") return PROGRESS_CLASSW;
    return nullptr;
}
}

ReplicaUi::ReplicaUi(HINSTANCE h,EventHandler handler,HICON icon,const v6::AssetModule& assets)
    : instance_(h),assets_(assets),icon_(icon),handler_(std::move(handler)) {
    if(HDC dc=GetDC(nullptr)) { dpi_=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(nullptr,dc); }
    // The original EXUI controls use Microsoft YaHei, not the stock bitmap font.
    font_=CreateFontW(-MulDiv(9,dpi_,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei");
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d_.GetAddressOf());
    RegisterClass();
}
ReplicaUi::~ReplicaUi() {
    for(auto& item:windows_) DestroyWindowState(item.second);
    if(font_)DeleteObject(font_);
    if(ownsClass_) UnregisterClassW(kClassName,instance_);
    if(icon_) DestroyIcon(icon_);
}
bool ReplicaUi::RegisterClass() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};
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
    const DWORD exStyle=(owner?0:WS_EX_APPWINDOW)|(fullscreen?WS_EX_TOPMOST:0);
    const DWORD style=WS_CLIPCHILDREN|(fullscreen?WS_POPUP:(WS_CAPTION|WS_SYSMENU|(d->minimize?WS_MINIMIZEBOX:0)));
    RECT r{0,0,MulDiv(d->width,dpi_,96),MulDiv(d->height,dpi_,96)};
    if(fullscreen) {
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(main_,MONITOR_DEFAULTTOPRIMARY),&monitor)) r=monitor.rcMonitor;
    } else {
        AdjustWindowRectEx(&r,style,FALSE,exStyle);
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        OffsetRect(&r,(work.left+work.right-r.left-r.right)/2,(work.top+work.bottom-r.top-r.bottom)/2);
    }
    Window state;state.name=name;
    HWND h=CreateWindowExW(exStyle,kClassName,d->title,style,r.left,r.top,
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
        HWND parent=window.hwnd;auto p=parentHandles.find(c.parent);if(p!=parentHandles.end())parent=p->second;
        const std::wstring type=c.type;
        UINT id=nextId_++;
        int x=MulDiv(c.x,dpi_,96),y=MulDiv(c.y,dpi_,96);
        int width=MulDiv(c.w,dpi_,96),height=MulDiv(c.h,dpi_,96);
        if(window.name==L"主题包窗口"&&type==L"Lib3.5"&&std::wstring(c.parent)==L"分组框EX3")
            width=(std::min)(width,MulDiv(110,dpi_,96));
        if(window.name==L"大图窗口") {
            RECT bounds{};GetClientRect(window.hwnd,&bounds);
            if(type==L"Lib3.4") { width=bounds.right;height=bounds.bottom; }
            if(type==L"Lib3.8") { x=(bounds.right-width)/2;y=MulDiv(35,dpi_,96); }
        }
        const DWORD exStyle=type==L"Lib3.2"||type==L"Lib3.3"?WS_EX_CONTROLPARENT:0;
        HWND child=CreateWindowExW(exStyle,cls,c.text,Style(c),x,y,width,
            type==L"Lib3.16" ? height+MulDiv(170,dpi_,96) : height,parent,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),instance_,nullptr);
        if(!child) { success=false;continue; }
        if(parent!=window.hwnd&&!AttachSubclass(window,parent)) success=false;
        if(type==L"Lib3.4"&&!AttachSubclass(window,child)) success=false;
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_?font_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        if(type==L"Lib3.15") SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(4,4));
        if(type==L"Lib3.16") SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),height-MulDiv(6,dpi_,96));
        if(type==L"Lib3.3") {
            for(const auto& tab:OriginalTabs) if(window.name==tab.window&&std::wstring(c.name)==tab.control) {
                TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(tab.title);
                if(TabCtrl_InsertItem(child,tab.index,&item)<0) success=false;
            }
            TabCtrl_SetItemSize(child,MulDiv(105,dpi_,96),MulDiv(38,dpi_,96));
        }
        Child state;
        state.name=c.name;state.type=type;state.hwnd=child;state.id=id;state.page=c.page;
        if(window.name==L"大图窗口"&&type==L"Lib3.4") state.imageMode=ImageMode::Fit;
        window.children.push_back(std::move(state));
        parentHandles[c.name]=child;
        LoadControlImage(window,window.children.back());
        // EXUI draws into the host while the XML child HWNDs are hidden.
        // Reproduce visibility from page membership rather than that HWND flag.
        if(c.page==-1||c.page==0) ShowWindow(child,SW_SHOWNA);
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
    return success;
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
}
void ReplicaUi::LoadControlImage(Window& window,Child& child) {
    if(!IsImage(child.type)&&!(window.name==L"大图窗口"&&child.type==L"Lib3.8")) return;
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
        UiEvent e;e.window=w->name;e.control=c.name;e.notification=notification;
        const int len=GetWindowTextLengthW(source);std::wstring text(static_cast<size_t>(len)+1,L'\0');
        if(len)GetWindowTextW(source,text.data(),len+1);text.resize(static_cast<size_t>(len));e.text=text;
        if(c.type==L"Lib3.16")e.selection=static_cast<int>(SendMessageW(source,CB_GETCURSEL,0,0));
        else if(c.type==L"Lib3.18")e.selection=static_cast<int>(SendMessageW(source,LB_GETCURSEL,0,0));
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
        if(m==WM_COMMAND){self->OnCommand(h,LOWORD(w),HIWORD(w),reinterpret_cast<HWND>(l));return 0;}
        if(m==WM_NOTIFY){self->OnNotify(h,reinterpret_cast<NMHDR*>(l));return 0;}
        if(m==WM_CONTEXTMENU&&self->OnContextMenu(h,reinterpret_cast<HWND>(w),l))return 0;
        if(m==WM_DRAWITEM){self->OnDrawItem(h,reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(m==WM_TIMER&&w==kViewerCursorTimer){self->OnViewerTimer(h);return 0;}
        if(m==WM_ERASEBKGND) {
            auto* window=self->WindowFor(h);
            const auto* definition=window?Definition(window->name):nullptr;
            if(definition&&definition->white) {
                RECT bounds{};GetClientRect(h,&bounds);
                FillRect(reinterpret_cast<HDC>(w),&bounds,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));return 1;
            }
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
        HWND root=GetAncestor(hwnd,GA_ROOT);
        if(message==WM_CONTEXTMENU) {
            HWND source=reinterpret_cast<HWND>(wParam);
            if(!source)source=hwnd;
            if(self->OnContextMenu(root,source,lParam))return 0;
        }
        if(message==WM_COMMAND){self->OnCommand(root,LOWORD(wParam),HIWORD(wParam),reinterpret_cast<HWND>(lParam));return 0;}
        if(message==WM_NOTIFY){self->OnNotify(root,reinterpret_cast<NMHDR*>(lParam));return 0;}
        if(message==WM_DRAWITEM){self->OnDrawItem(root,reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;}
    }
    return DefSubclassProc(hwnd,message,wParam,lParam);
}
void ReplicaUi::Quit() {
    if(main_ && IsWindow(main_)) {
        Window* mainWindow=WindowFor(main_);
        if(mainWindow)DestroyWindowState(*mainWindow);
    }
}
void ReplicaUi::OnNotify(HWND hwnd,NMHDR* hdr) {
    Window* w=WindowFor(hwnd);if(!w||!hdr)return;
    for(const auto& c:w->children)if(c.hwnd==hdr->hwndFrom) {
        UiEvent e;e.window=w->name;e.control=c.name;e.notification=hdr->code;
        if(c.type==L"Lib3.3"&&hdr->code==TCN_SELCHANGE) {
            e.selection=TabCtrl_GetCurSel(hdr->hwndFrom);
            ActivatePage(*w,hdr->hwndFrom,e.selection);
        }
        if(handler_) handler_(e);
        return;
    }
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
    if(!draw||(draw->CtlType!=ODT_STATIC&&draw->CtlType!=ODT_BUTTON))return;
    Window* window=WindowFor(hwnd);if(!window)return;
    for(const auto& child:window->children)if(child.hwnd==draw->hwndItem) {
        const bool viewer=window->name==L"大图窗口";
        const int color=viewer?COLOR_WINDOW:COLOR_BTNFACE;
        FillRect(draw->hDC,&draw->rcItem,reinterpret_cast<HBRUSH>(color+1));
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
            ComPtr<ID2D1Bitmap> bitmap;
            if(FAILED(imageTarget_->CreateBitmap(D2D1::SizeU(picture.imageWidth,picture.imageHeight),
                picture.image.data(),picture.imageWidth*4,format,bitmap.GetAddressOf())))return;
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
        const COLORREF background=GetSysColor(color);
        imageTarget_->Clear(D2D1::ColorF(GetRValue(background)/255.f,GetGValue(background)/255.f,GetBValue(background)/255.f));
        float opacity=(draw->itemState&ODS_DISABLED)?0.4f:1.f;
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
        if(imageTarget_->EndDraw()==D2DERR_RECREATE_TARGET)imageTarget_.Reset();
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
        if(owner) {
            it->second.blockedOwner=owner;
            EnableWindow(owner,FALSE);
        }
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
    else if(x->type==L"Lib3.18")SendMessageW(x->hwnd,LB_SETCURSEL,i,0);
    else if(x->type==L"Lib3.3"&&i>=0&&i<TabCtrl_GetItemCount(x->hwnd)) {
        TabCtrl_SetCurSel(x->hwnd,i);ActivatePage(windows_.at(w),x->hwnd,i);
    }
}
int ReplicaUi::Selection(const std::wstring& w,const std::wstring& c) {
    auto* x=Find(nullptr,w,c);if(!x)return -1;
    if(x->type==L"Lib3.16")return static_cast<int>(SendMessageW(x->hwnd,CB_GETCURSEL,0,0));
    if(x->type==L"Lib3.18")return static_cast<int>(SendMessageW(x->hwnd,LB_GETCURSEL,0,0));
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
    child->image.clear();child->imageWidth=child->imageHeight=0;
    if(bytes.empty()){InvalidateRect(child->hwnd,nullptr,TRUE);return true;}
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
    ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter)))return false;
    if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut)))return false;
    const double scale=w==L"大图窗口" ? 1.0 : (std::min)(1.0,1200.0/(std::max)(static_cast<double>(width),static_cast<double>(height)));
    const UINT targetWidth=(std::max)(1u,static_cast<UINT>(width*scale));
    const UINT targetHeight=(std::max)(1u,static_cast<UINT>(height*scale));
    ComPtr<IWICBitmapScaler> scaler;IWICBitmapSource* source=converter.Get();
    if(targetWidth!=width||targetHeight!=height) {
        if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(converter.Get(),targetWidth,targetHeight,WICBitmapInterpolationModeFant)))return false;
        source=scaler.Get();
    }
    const UINT stride=targetWidth*4;
    if(targetHeight>(std::numeric_limits<UINT>::max)()/stride)return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride)*targetHeight);
    if(FAILED(source->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data())))return false;
    child->image=std::move(pixels);child->imageWidth=targetWidth;child->imageHeight=targetHeight;
    if(w==L"大图窗口"&&c==L"图片框EX1")if(auto* button=Find(nullptr,w,L"超级按钮EX1"))InvalidateRect(button->hwnd,nullptr,FALSE);
    InvalidateRect(child->hwnd,nullptr,TRUE);return true;
}
} // namespace stoutwall
