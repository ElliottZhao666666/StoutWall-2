#pragma once
#include <cwctype>
#include <string>
#include <string_view>

namespace stoutwall {
inline bool ValidThemeName(std::wstring_view name) {
    if(name.empty()||name.size()>100||name==L"."||name==L".."||
       name.back()==L'.'||name.back()==L' ')return false;
    for(wchar_t c:name)
        if(c<32||std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)return false;
    std::wstring stem(name.substr(0,name.find(L'.')));
    while(!stem.empty()&&stem.back()==L' ')stem.pop_back();
    for(auto& c:stem)c=static_cast<wchar_t>(towupper(c));
    if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL")return false;
    if(stem.size()==4&&(stem.compare(0,3,L"COM")==0||stem.compare(0,3,L"LPT")==0)) {
        const wchar_t n=stem[3];
        if((n>=L'1'&&n<=L'9')||n==L'¹'||n==L'²'||n==L'³')return false;
    }
    return true;
}
}
