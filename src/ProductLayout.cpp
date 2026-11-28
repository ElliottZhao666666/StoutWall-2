#include "ProductLayout.hpp"
#include "GalleryWorkbench.hpp"

namespace stoutwall {
namespace {
struct Entry { const wchar_t* name; int x,y,w,h,page; const wchar_t* text; };
constexpr Entry kMainLayout[] = {
    {L"选择夹EX1",0,0,980,680,-1,L"主导航"},
    {L"超级按钮EX1",756,12,192,38,-1,L"壁纸库工作台"},
    {L"图片框EX1",32,94,540,304,0,nullptr},
    {L"标签EX1",610,94,338,78,0,nullptr},
    {L"标签EX2",610,184,338,76,0,nullptr},
    {L"标签EX3",610,278,338,52,0,nullptr},
    {L"标签EX5",610,344,144,30,0,nullptr},
    {L"标签EX4",774,344,174,30,0,nullptr},
    {L"图片按钮EX1",32,418,60,40,0,L"上一张"},
    {L"图片按钮EX2",104,418,60,40,0,L"下一张"},
    {L"图片按钮EX4",176,418,116,40,0,L"收藏"},
    {L"图片按钮EX5",304,418,116,40,0,L"暂停更换"},
    {L"图片按钮EX3",432,418,140,40,0,L"版权网页 ↗"},
    {L"单选框EX1",32,530,146,38,0,L"每日一图"},
    {L"单选框EX2",194,530,146,38,0,L"壁纸库轮播"},
    {L"单选框EX3",356,530,146,38,0,L"收藏轮播"},
    {L"按钮EX7",610,418,154,40,0,L"刷新今日壁纸"},
    {L"按钮EX9",780,418,168,40,0,L"重置随机顺序"},
    {L"选择框EX1",32,96,916,40,1,L"自动保存每日一图到壁纸库"},
    {L"选择框EX2",32,150,916,40,1,L"乱序更换"},
    {L"选择框EX3",32,204,916,40,1,L"开机自动启动（界面偏好）"},
    {L"标签EX6",32,274,350,34,1,L"壁纸更换频率"},
    {L"组合框EX1",680,268,268,38,1,nullptr},
    {L"标签EX7",32,330,350,34,1,L"桌面壁纸尺寸"},
    {L"组合框EX2",680,324,268,38,1,nullptr},
    {L"标签EX8",32,368,916,38,1,L"按显示器比例选择；入库时保存全部尺寸。4K 界面预览使用 1920×1080。"},
    {L"按钮EX1",32,480,216,40,1,L"壁纸库位置…"},
    {L"按钮EX12",264,480,216,40,1,L"选择壁纸来源…"},
    {L"按钮EX5",496,480,216,40,1,L"清空所有收藏"},
    {L"编辑框EX2",32,540,916,108,1,nullptr},
    {L"图片框EX2",32,92,80,80,2,nullptr},
    {L"标签EX9",134,94,520,44,2,L"胖哥每日壁纸"},
    {L"标签EX10",136,144,500,28,2,nullptr},
    {L"图片框EX5",868,96,64,64,2,nullptr},
    {L"编辑框EX3",32,208,566,334,2,nullptr},
    {L"图片框EX3",626,208,322,120,2,nullptr},
    {L"图片框EX4",636,348,126,126,2,nullptr},
    {L"按钮EX3",780,426,168,40,2,L"退出软件"},
    {L"按钮EX14",32,566,136,40,2,L"壁纸库网站"},
    {L"按钮EX6",182,566,144,40,2,L"壁纸库合集"},
    {L"按钮EX4",340,566,136,40,2,L"获取 iOS 版"},
    {L"按钮EX10",490,566,144,40,2,L"帮助与介绍"},
    {L"按钮EX11",648,566,144,40,2,L"反馈与建议"},
    {L"超级按钮EX2",806,566,142,40,2,L"Bing ↗"},
};
}

ProductWindowPlacement ProductWindowSize(const std::wstring& window) {
    if(window==L"_启动窗口")return {980,680,true};
    if(window==L"库管理窗口")return {1120,740,true};
    if(window==L"主题包窗口")return {1072,628,true};
    if(window==L"壁纸导出主题自选窗口")return {720,520,true};
    if(window==L"壁纸来源窗口")return {520,384,true};
    if(window==L"库迁移窗口")return {680,260,true};
    if(window==L"iOS窗口")return {720,520,true};
    return {};
}

std::vector<std::wstring> ProductTabTitles(const std::wstring& window) {
    return window==L"_启动窗口"?std::vector<std::wstring>{L"今日壁纸",L"设置",L"关于"}:std::vector<std::wstring>{};
}

ProductControlPlacement ProductControlLayout(const std::wstring& window,const ControlDef& control) {
    ProductControlPlacement p;
    p.x=control.x;p.y=control.y;p.width=control.w;p.height=control.h;
    p.page=control.page;p.visible=control.page!=-2;p.parent=control.parent;
    if(window==L"库管理窗口"||window==L"主题包窗口"||window==L"壁纸导出主题自选窗口")
        return GalleryWorkbench::Layout(window,control);
    if(window==L"_启动窗口") {
        p.customized=true;p.visible=false;p.page=-2;p.parent=L"选择夹EX1";
        for(const auto& entry:kMainLayout)if(std::wstring(control.name)==entry.name) {
            p.x=entry.x;p.y=entry.y;p.width=entry.w;p.height=entry.h;p.page=entry.page;p.visible=true;
            if(entry.text)p.text=entry.text;
            if(std::wstring(control.name)==L"选择夹EX1")p.parent=window;
            return p;
        }
    } else if(window==L"iOS窗口") {
        p.customized=true;p.parent=window;
        const std::wstring name=control.name;
        if(name==L"窗口EX1") {p.visible=false;p.page=-2;}
        else if(name==L"标签EX6") {p.x=32;p.y=44;p.width=656;p.height=90;}
        else if(name==L"图片框EX2") {p.x=82;p.y=166;p.width=190;p.height=190;}
        else if(name==L"图片框EX1") {p.x=448;p.y=166;p.width=190;p.height=190;}
        else if(name==L"标签EX1") {p.x=82;p.y=370;p.width=240;p.height=30;}
        else if(name==L"标签EX2") {p.x=422;p.y=370;p.width=266;p.height=30;}
        else if(name==L"标签EX3") {p.x=32;p.y=440;p.width=536;p.height=56;}
        else if(name==L"按钮EX4") {p.x=584;p.y=440;p.width=104;p.height=40;}
    } else if(window==L"库迁移窗口") {
        p.customized=true;p.parent=window;
        const std::wstring name=control.name;
        if(name==L"编辑框EX2") {p.x=32;p.y=40;p.width=504;p.height=38;}
        if(name==L"按钮EX2") {p.x=552;p.y=40;p.width=96;p.height=38;}
        if(name==L"标签EX6") {p.x=32;p.y=100;p.width=616;p.height=54;}
        if(name==L"进度条EX1") {p.x=32;p.y=190;p.width=420;p.height=30;}
        if(name==L"按钮EX1") {p.x=472;p.y=184;p.width=176;p.height=42;}
    } else if(window==L"壁纸来源窗口") {
        p.customized=true;p.parent=window;
        const std::wstring name=control.name;
        if(name==L"单选框EX1") {p.x=24;p.y=28;p.width=470;p.height=34;}
        if(name==L"标签Ex1") {p.x=48;p.y=66;p.width=440;p.height=44;}
        if(name==L"单选框EX2") {p.x=24;p.y=120;p.width=470;p.height=34;}
        if(name==L"标签Ex2") {p.x=48;p.y=158;p.width=440;p.height=32;}
        if(name==L"单选框EX3") {p.x=24;p.y=206;p.width=470;p.height=34;}
        if(name==L"标签Ex3") {p.x=48;p.y=244;p.width=440;p.height=46;}
        if(name==L"按钮EX1") {p.x=268;p.y=324;p.width=122;p.height=36;}
        if(name==L"按钮EX13") {p.x=404;p.y=324;p.width=92;p.height=36;}
    }
    return p;
}
} // namespace stoutwall
