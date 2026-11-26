#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <utility>
#include <d2d1.h>
#include <wrl/client.h>

namespace v6 { class AssetModule; }

namespace stoutwall {
struct UiEvent {
    std::wstring window;
    std::wstring control;
    UINT notification = 0;
    std::wstring text;
    int selection = -1;
    bool checked = false;
};

class ReplicaUi final {
public:
    using EventHandler = std::function<void(const UiEvent&)>;
    using MessageHandler = std::function<bool(UINT, WPARAM, LPARAM)>;
    using CloseGuard = std::function<bool(const std::wstring&)>;
    explicit ReplicaUi(HINSTANCE instance, EventHandler handler, HICON icon, const v6::AssetModule& assets);
    ~ReplicaUi();
    bool CreateMain();
    bool PreTranslateMessage(MSG& message);
    void SetMessageHandler(MessageHandler handler) { messageHandler_ = std::move(handler); }
    void SetCloseGuard(CloseGuard guard) { closeGuard_ = std::move(guard); }
    void Quit();
    HWND MainWindow() const noexcept { return main_; }
    HWND WindowHandle(const std::wstring& window) const;
    void Show(const std::wstring& window, HWND owner = nullptr);
    void Close(const std::wstring& window);
    void SetText(const std::wstring& window,const std::wstring& control,const std::wstring& value);
    void SetChecked(const std::wstring& window,const std::wstring& control,bool checked);
    void SetSelection(const std::wstring& window,const std::wstring& control,int selection);
    void SetItems(const std::wstring& window,const std::wstring& control,const std::vector<std::wstring>& values);
    void SetEnabled(const std::wstring& window,const std::wstring& control,bool enabled);
    void SetVisible(const std::wstring& window,const std::wstring& control,bool visible);
    void SetProgress(const std::wstring& window,const std::wstring& control,int position,bool busy);
    std::wstring GetText(const std::wstring& window,const std::wstring& control);
    int Selection(const std::wstring& window,const std::wstring& control);
    enum class ImageMode { Fit, Center, Tile, Stretch };
    void SetImageMode(const std::wstring& window,const std::wstring& control,ImageMode mode);
    bool SetImage(const std::wstring& window,const std::wstring& control,const std::vector<unsigned char>& bytes);
    static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
private:
#ifdef STOUTWALL_PREVIEW_TEST
    friend struct PreviewProbe;
#endif
    struct Child {
        std::wstring name,type;
        HWND hwnd=nullptr;
        UINT id=0;
        int page=-1;
        bool visible=true;
        UINT imageWidth=0,imageHeight=0;
        std::vector<unsigned char> image;
        mutable Microsoft::WRL::ComPtr<ID2D1Bitmap> imageBitmap;
        ImageMode imageMode=ImageMode::Stretch;
    };
    struct Window {
        std::wstring name;
        HWND hwnd=nullptr;
        HWND blockedOwner=nullptr;
        POINT viewerCursor{};
        BYTE viewerCloseOpacity=255;
        std::vector<Child> children;
        std::vector<HWND> subclassed;
    };
    HINSTANCE instance_;
    const v6::AssetModule& assets_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> imageTarget_;
    int dpi_=96;
    HFONT font_=nullptr;
    HICON icon_ = nullptr;
    bool ownsClass_ = false;
    EventHandler handler_;
    MessageHandler messageHandler_;
    CloseGuard closeGuard_;
    std::unordered_map<std::wstring,Window> windows_;
    HWND main_=nullptr;
    UINT nextId_=100;
    bool RegisterClass();
    HWND CreateProductWindow(const wchar_t* name, HWND owner = nullptr);
    bool CreateChildren(Window& window);
    void ActivatePage(Window& window, HWND tab, int page);
    void LoadControlImage(Window& window, Child& child);
    bool AttachSubclass(Window& window,HWND target);
    void DetachSubclasses(Window& window);
    void DestroyWindowState(Window& window);
    void RestoreOwner(Window& window);
    HWND TopModalWindow(HWND window) const;
    void OnSubclassNcDestroy(HWND);
    void OnCommand(HWND,UINT,UINT,HWND);
    void OnNotify(HWND,NMHDR*);
    void OnDrawItem(HWND,DRAWITEMSTRUCT*);
    void OnViewerTimer(HWND);
    bool OnContextMenu(HWND,HWND,LPARAM);
    void OnClose(HWND);
    void OnNcDestroy(HWND);
    static LRESULT CALLBACK ParentProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    Child* Find(HWND,const std::wstring& window,const std::wstring& control);
    Window* WindowFor(HWND);
};
} // namespace stoutwall
