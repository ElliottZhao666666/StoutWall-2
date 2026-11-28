#include "GalleryWorkbench.hpp"

namespace stoutwall {
namespace {
struct Entry { const wchar_t* name; int x,y,w,h; const wchar_t* text; };
constexpr Entry kBrowse[] = {
    {L"列表框EX1",24,104,668,588,nullptr},
    {L"图片框EX1",716,104,380,214,nullptr},
    {L"编辑框EX1",716,336,380,244,nullptr},
    {L"组合框EX2",440,54,252,36,nullptr},
    {L"按钮EX1",716,602,182,40,L"收藏"},
    {L"按钮EX2",914,602,182,40,L"设为壁纸"},
    {L"按钮EX3",716,654,182,40,L"删除本图"},
    {L"多功能按钮EX1",914,654,182,40,L"导出/保存原图"},
    {L"按钮EX6",970,54,126,36,L"刷新"},
};
constexpr Entry kTheme[] = {
    {L"标签EX1",16,30,128,34,L"主题名称"},
    {L"编辑框EX1",162,24,462,38,nullptr},
    {L"标签EX2",16,86,128,34,L"导出位置"},
    {L"编辑框EX2",162,80,366,38,nullptr},
    {L"按钮EX2",542,80,82,38,L"浏览…"},
    {L"单选框EX2",16,148,178,36,L"整个壁纸库"},
    {L"单选框EX1",210,148,124,36,L"仅收藏"},
    {L"单选框EX5",348,148,276,36,L"手动选择"},
    {L"按钮EX3",510,148,114,36,L"选择…"},
    {L"单选框EX4",16,206,300,40,L"Windows 8 / 10 / 11"},
    {L"单选框EX3",336,206,288,40,L"Windows 7 / Vista"},
    {L"标签EX6",16,286,140,34,L"更换频率"},
    {L"组合框EX1",162,280,260,38,nullptr},
    {L"选择框EX2",450,280,174,38,L"乱序更换"},
    {L"标签EX3",16,344,140,34,L"壁纸应用方式"},
    {L"组合框EX2",162,338,260,38,nullptr},
    {L"选择框EX3",450,338,174,38,L"深色模式"},
    {L"标签EX4",16,402,140,34,L"默认主题颜色"},
    {L"组合框EX3",162,396,260,38,nullptr},
    {L"选择框EX1",450,396,174,38,L"自动主题色"},
    {L"标签EX5",16,460,140,34,L"壁纸导出尺寸"},
    {L"组合框EX4",162,454,260,38,nullptr},
    {L"进度条EX1",16,538,406,28,nullptr},
    {L"按钮EX1",450,530,174,44,L"开始导出"},
};
}

std::vector<std::wstring> GalleryWorkbench::Rows() {
    visibleItems.clear();
    std::vector<std::wstring> rows;
    for(size_t index=0;index<items.size();++index) {
        const auto& item=items[index];
        if(favoritesOnly&&!item.favorite)continue;
        visibleItems.push_back(index);
        rows.push_back(item.title+L"\r\n"+item.date+(item.current?L"  · 当前桌面":L"")+
                       (item.favorite?L"  ★":L""));
    }
    return rows;
}

int GalleryWorkbench::DisplayIndex(const std::wstring& date) const {
    for(size_t index=0;index<visibleItems.size();++index)
        if(items[visibleItems[index]].date==date)return static_cast<int>(index);
    return -1;
}

std::wstring GalleryWorkbench::DateAt(int displayIndex) const {
    if(displayIndex<0||static_cast<size_t>(displayIndex)>=visibleItems.size())return {};
    return items[visibleItems[static_cast<size_t>(displayIndex)]].date;
}

ProductControlPlacement GalleryWorkbench::Layout(const std::wstring& window,const ControlDef& control) {
    ProductControlPlacement p;
    p.customized=true;p.visible=false;p.page=-2;p.parent=window;
    const Entry* entries=nullptr;size_t count=0;
    if(window==L"库管理窗口") {entries=kBrowse;count=_countof(kBrowse);}
    else if(window==L"主题包窗口") {entries=kTheme;count=_countof(kTheme);}
    if(entries)for(size_t index=0;index<count;++index)if(std::wstring(control.name)==entries[index].name) {
        const auto& e=entries[index];p.x=e.x;p.y=e.y;p.width=e.w;p.height=e.h;
        p.visible=true;p.page=-1;if(e.text)p.text=e.text;return p;
    }
    if(window==L"壁纸导出主题自选窗口") {
        const std::wstring name=control.name;
        if(name==L"列表框Ex") {p.x=24;p.y=48;p.width=210;p.height=440;p.page=-1;p.visible=true;}
        if(name==L"图片框EX1") {p.x=258;p.y=48;p.width=438;p.height=440;p.page=-1;p.visible=true;}
    }
    return p;
}
} // namespace stoutwall
