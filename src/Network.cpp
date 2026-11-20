#define WIN32_LEAN_AND_MEAN
#include "NetworkApi.h"
#include <winhttp.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <string>
#include <vector>
#include <new>
#pragma comment(lib, "winhttp.lib")

#pragma comment(linker, "/EXPORT:SWN_FetchLatest=_SWN_FetchLatest@16")
#pragma comment(linker, "/EXPORT:SWN_ParseMetadata=_SWN_ParseMetadata@16")
#pragma comment(linker, "/EXPORT:SWN_BuildImageUrl=_SWN_BuildImageUrl@16")
#pragma comment(linker, "/EXPORT:SWN_DownloadImage=_SWN_DownloadImage@24")
#pragma comment(linker, "/EXPORT:SWN_ValidateImage=_SWN_ValidateImage@12")
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

namespace {
DWORD LastError() { DWORD e=GetLastError(); return e?e:ERROR_GEN_FAILURE; }
bool IsCancelled(HANDLE e) { return e && WaitForSingleObject(e,0)==WAIT_OBJECT_0; }
DWORD Timeout(const SWN_Options* o) { return !o||!o->timeoutMs?45000:std::min<DWORD>(o->timeoutMs,600000); }
struct Handles { HINTERNET s=nullptr,c=nullptr,r=nullptr; ~Handles(){if(r)WinHttpCloseHandle(r);if(c)WinHttpCloseHandle(c);if(s)WinHttpCloseHandle(s);} };
struct Reply { std::vector<unsigned char> bytes; DWORD status=0; };

DWORD Get(const std::wstring& url,const SWN_Options* o,Reply& reply) {
    HANDLE cancel=o?o->cancelEvent:nullptr;if(IsCancelled(cancel))return ERROR_CANCELLED;
    const ULONGLONG start=GetTickCount64();const DWORD total=Timeout(o);
    URL_COMPONENTS u{};u.dwStructSize=sizeof(u);wchar_t host[512]{},path[4096]{},extra[2048]{};
    u.lpszHostName=host;u.dwHostNameLength=_countof(host);u.lpszUrlPath=path;u.dwUrlPathLength=_countof(path);u.lpszExtraInfo=extra;u.dwExtraInfoLength=_countof(extra);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&u))return LastError();
    if(u.nScheme!=INTERNET_SCHEME_HTTPS||!u.dwHostNameLength)return ERROR_INVALID_NAME;
    std::wstring resource(path,u.dwUrlPathLength);resource.append(extra,u.dwExtraInfoLength);if(resource.empty())resource=L"/";
    Handles h;std::wstring proxy=o?o->proxy:L"";
    h.s=WinHttpOpen(L"StoutWall/2.0",proxy.empty()?WINHTTP_ACCESS_TYPE_DEFAULT_PROXY:WINHTTP_ACCESS_TYPE_NAMED_PROXY,
        proxy.empty()?WINHTTP_NO_PROXY_NAME:proxy.c_str(),WINHTTP_NO_PROXY_BYPASS,0);if(!h.s)return LastError();
    int timeout=static_cast<int>(std::min<DWORD>(total,5000));WinHttpSetTimeouts(h.s,timeout,timeout,timeout,timeout);
    DWORD tls=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;if(!WinHttpSetOption(h.s,WINHTTP_OPTION_SECURE_PROTOCOLS,&tls,sizeof(tls)))return LastError();
    std::wstring hostname(host,u.dwHostNameLength);h.c=WinHttpConnect(h.s,hostname.c_str(),u.nPort,0);if(!h.c)return LastError();
    h.r=WinHttpOpenRequest(h.c,L"GET",resource.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(!h.r)return LastError();
    const wchar_t hdr[]=L"Accept: application/json, image/jpeg, */*\r\n";
    if(!WinHttpSendRequest(h.r,hdr,static_cast<DWORD>(-1L),WINHTTP_NO_REQUEST_DATA,0,0,0))return LastError();
    if(IsCancelled(cancel))return ERROR_CANCELLED;if(GetTickCount64()-start>=total)return ERROR_TIMEOUT;
    if(!WinHttpReceiveResponse(h.r,nullptr))return LastError();DWORD n=sizeof(reply.status);
    if(!WinHttpQueryHeaders(h.r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&reply.status,&n,WINHTTP_NO_HEADER_INDEX))return LastError();
    if(reply.status<200||reply.status>=300)return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    for(;;){if(IsCancelled(cancel))return ERROR_CANCELLED;if(GetTickCount64()-start>=total)return ERROR_TIMEOUT;DWORD avail=0;
        if(!WinHttpQueryDataAvailable(h.r,&avail))return LastError();if(!avail)break;if(avail>64u*1024u*1024u-reply.bytes.size())return ERROR_FILE_TOO_LARGE;
        size_t old=reply.bytes.size();reply.bytes.resize(old+avail);DWORD got=0;if(!WinHttpReadData(h.r,reply.bytes.data()+old,avail,&got))return LastError();reply.bytes.resize(old+got);if(!got)break;}
    return ERROR_SUCCESS;
}

bool Utf8(const void* p,DWORD n,std::wstring& out){if(!p||!n||n>4u*1024u*1024u)return false;int z=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,static_cast<LPCCH>(p),static_cast<int>(n),nullptr,0);if(!z)return false;out.resize(z);return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,static_cast<LPCCH>(p),static_cast<int>(n),out.data(),z)==z;}
struct J{enum K{nil,str,obj,arr,atom}k=nil;std::wstring s;std::map<std::wstring,J> o;std::vector<J>a;const J*get(const wchar_t*x)const{auto i=o.find(x);return i==o.end()?nullptr:&i->second;}};
class Parser{
 const std::wstring&s;size_t p=0;unsigned depth=0;
 void ws(){while(p<s.size()&&iswspace(s[p]))++p;}
 bool hex(unsigned&v){if(p+4>s.size())return false;v=0;for(int i=0;i<4;++i){wchar_t c=s[p++];v<<=4;if(c>=L'0'&&c<=L'9')v|=c-L'0';else if(c>=L'a'&&c<=L'f')v|=c-L'a'+10;else if(c>=L'A'&&c<=L'F')v|=c-L'A'+10;else return false;}return true;}
 bool string(std::wstring&v){if(p>=s.size()||s[p++]!=L'"')return false;while(p<s.size()){wchar_t c=s[p++];if(c==L'"')return true;if(c<32)return false;if(c!=L'\\'){v+=c;continue;}if(p==s.size())return false;c=s[p++];switch(c){case L'"':case L'\\':case L'/':v+=c;break;case L'b':v+=L'\b';break;case L'f':v+=L'\f';break;case L'n':v+=L'\n';break;case L'r':v+=L'\r';break;case L't':v+=L'\t';break;case L'u':{unsigned cp;if(!hex(cp))return false;if(cp>=0xd800&&cp<=0xdbff){unsigned lo;if(p+2>s.size()||s[p++]!=L'\\'||s[p++]!=L'u'||!hex(lo)||lo<0xdc00||lo>0xdfff)return false;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)return false;if(cp<=0xffff)v+=static_cast<wchar_t>(cp);else{cp-=0x10000;v+=static_cast<wchar_t>(0xd800+(cp>>10));v+=static_cast<wchar_t>(0xdc00+(cp&1023));}break;}default:return false;}}return false;}
 bool value(J&v){ws();if(p==s.size()||depth>=64)return false;if(s[p]==L'"'){v.k=J::str;return string(v.s);}if(s[p]==L'{'){++p;++depth;v.k=J::obj;ws();if(p<s.size()&&s[p]==L'}'){++p;--depth;return true;}for(;;){ws();std::wstring key;if(!string(key))return false;ws();if(p==s.size()||s[p++]!=L':')return false;J x;if(!value(x)||!v.o.emplace(key,std::move(x)).second)return false;ws();if(p==s.size())return false;if(s[p]==L'}'){++p;--depth;return true;}if(s[p++]!=L',')return false;}}
 if(s[p]==L'['){++p;++depth;v.k=J::arr;ws();if(p<s.size()&&s[p]==L']'){++p;--depth;return true;}for(;;){J x;if(!value(x))return false;v.a.push_back(std::move(x));ws();if(p==s.size())return false;if(s[p]==L']'){++p;--depth;return true;}if(s[p++]!=L',')return false;}}
 size_t b=p;while(p<s.size()&&s[p]!=L','&&s[p]!=L']'&&s[p]!=L'}'&&!iswspace(s[p]))++p;if(b==p)return false;v.k=J::atom;v.s=s.substr(b,p-b);return v.s==L"true"||v.s==L"false"||v.s==L"null"||v.s[0]==L'-'||iswdigit(v.s[0]);}
public:explicit Parser(const std::wstring&x):s(x){}bool parse(J&v){if(!value(v))return false;ws();return p==s.size();}};
std::wstring field(const J*j,const wchar_t*k){const J*v=j?j->get(k):nullptr;return v&&(v->k==J::str||v->k==J::atom)?v->s:L"";}
std::wstring trim(std::wstring s){size_t a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return L"";return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);}
bool put(wchar_t*d,size_t cap,const std::wstring&s){if(!d||s.size()>=cap)return false;std::copy(s.begin(),s.end(),d);d[s.size()]=0;return true;}
DWORD parseMetadata(DWORD source,const void*bytes,DWORD count,SWN_Metadata*out){if(source<1||source>3||!out||out->cbSize<sizeof(*out))return ERROR_INVALID_PARAMETER;std::wstring text;if(!Utf8(bytes,count,text))return ERROR_INVALID_DATA;J root;Parser parser(text);if(!parser.parse(root)||root.k!=J::obj)return ERROR_INVALID_DATA;const J*item=&root;if(source!=SWN_CUSTOM){const J*images=root.get(L"images");if(!images||images->k!=J::arr||images->a.empty()||images->a[0].k!=J::obj)return ERROR_INVALID_DATA;item=&images->a[0];}
 std::wstring date=field(item,L"enddate"),title=field(item,L"title"),desc=field(item,L"description"),author=field(item,L"author"),link=field(item,L"copyrightlink"),base=field(item,L"urlbase");
 if(source!=SWN_CUSTOM){std::wstring copyright=field(item,L"copyright");size_t mark=copyright.find(L" (");if(mark!=copyright.npos){if(desc.empty())desc=trim(copyright.substr(0,mark));if(author.empty()){author=trim(copyright.substr(mark+2));if(!author.empty()&&author.back()==L')')author.pop_back();author=trim(author);if(!author.empty()&&author.front()==0xa9){author.erase(author.begin());author=trim(author);}}}else if(desc.empty())desc=copyright;}
 if(date.size()!=8||!std::all_of(date.begin(),date.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})||title.empty()||base.empty())return ERROR_INVALID_DATA;SWN_Metadata m{};m.cbSize=sizeof(m);
 if(!put(m.enddate,_countof(m.enddate),date)||!put(m.title,_countof(m.title),title)||!put(m.description,_countof(m.description),desc)||!put(m.author,_countof(m.author),author)||!put(m.copyrightlink,_countof(m.copyrightlink),link)||!put(m.urlbase,_countof(m.urlbase),base))return ERROR_INSUFFICIENT_BUFFER;
 std::wstring wp=field(item,L"wp");m.wp=(wp==L"true"||wp==L"1");*out=m;return ERROR_SUCCESS;}
const wchar_t*const sizes[]={L"UHD",L"1920x1200",L"1920x1080",L"1024x768",L"1080x1920"};
bool jpeg(const std::vector<unsigned char>&b,DWORD&w,DWORD&h){if(b.size()<12||b[0]!=255||b[1]!=216)return false;size_t p=2;while(p+1<b.size()){if(b[p++]!=255)return false;while(p<b.size()&&b[p]==255)++p;if(p==b.size())return false;unsigned char m=b[p++];if(m==217)break;if(m==1||(m>=208&&m<=215))continue;if(p+2>b.size())return false;size_t n=(b[p]<<8)|b[p+1];if(n<2||n>b.size()-p)return false;bool sof=(m>=192&&m<=195)||(m>=197&&m<=199)||(m>=201&&m<=203)||(m>=205&&m<=207);if(sof){if(n<7)return false;h=(b[p+3]<<8)|b[p+4];w=(b[p+5]<<8)|b[p+6];return w&&h;}if(m==218)break;p+=n;}return false;}
bool dims(DWORD i,DWORD w,DWORD h){static const DWORD xy[5][2]={{3840,2160},{1920,1200},{1920,1080},{1024,768},{1080,1920}};return i==5||(i<5&&w==xy[i][0]&&h==xy[i][1]);}
DWORD validate(const wchar_t*path,DWORD i,SWN_ImageInfo*info){if(!path||!*path||i>SWN_SIZE_COUNT)return ERROR_INVALID_PARAMETER;HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return LastError();LARGE_INTEGER z{};if(!GetFileSizeEx(f,&z)){DWORD e=LastError();CloseHandle(f);return e;}if(z.QuadPart<12||z.QuadPart>100ll*1024*1024){CloseHandle(f);return ERROR_FILE_TOO_LARGE;}std::vector<unsigned char>b(static_cast<size_t>(z.QuadPart));DWORD n=0;BOOL ok=ReadFile(f,b.data(),static_cast<DWORD>(b.size()),&n,nullptr);DWORD e=ok?0:LastError();CloseHandle(f);if(!ok)return e;if(n!=b.size())return ERROR_HANDLE_EOF;DWORD w=0,h=0;if(!jpeg(b,w,h)||b[b.size()-2]!=255||b.back()!=217)return ERROR_INVALID_DATA;if(!dims(i,w,h))return ERROR_BAD_FORMAT;if(info){if(info->cbSize<sizeof(*info))return ERROR_INVALID_PARAMETER;info->width=w;info->height=h;info->bytes=z.QuadPart;}return 0;}
}

extern "C" DWORD SWN_CALL SWN_ParseMetadata(DWORD s,const void*b,DWORD n,SWN_Metadata*m){try{if(!m||m->cbSize<sizeof(*m))return ERROR_INVALID_PARAMETER;return parseMetadata(s,b,n,m);}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}
extern "C" DWORD SWN_CALL SWN_FetchLatest(DWORD source,const SWN_Options*o,SWN_Metadata*m,DWORD*status){try{if(source<1||source>3||!m||m->cbSize<sizeof(*m)||(o&&o->cbSize<sizeof(*o)))return ERROR_INVALID_PARAMETER;const wchar_t*url=source==SWN_CN?L"https://cn.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN":source==SWN_GLOBAL?L"https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1":L"https://api.bingpics.ybjun.com/api/latest";Reply r;DWORD e=Get(url,o,r);if(status)*status=r.status;return e?e:parseMetadata(source,r.bytes.data(),static_cast<DWORD>(r.bytes.size()),m);}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}
extern "C" DWORD SWN_CALL SWN_BuildImageUrl(const SWN_Metadata*m,DWORD i,wchar_t*out,DWORD cap){if(!m||m->cbSize<sizeof(*m)||i>=SWN_SIZE_COUNT||!out||!cap)return ERROR_INVALID_PARAMETER;try{std::wstring b=m->urlbase;if(b.find_first_of(L"\r\n\\")!=b.npos||b.find(L"://")!=b.npos)return ERROR_INVALID_NAME;while(!b.empty()&&b.front()==L'/')b.erase(b.begin());std::wstring u=L"https://www.bing.com/"+b+L"_"+sizes[i]+L".jpg&rf=LaDigue_"+sizes[i]+L".jpg&pid=hp";if(u.size()+1>cap)return ERROR_INSUFFICIENT_BUFFER;std::copy(u.begin(),u.end(),out);out[u.size()]=0;return 0;}catch(...){return ERROR_NOT_ENOUGH_MEMORY;}}
extern "C" DWORD SWN_CALL SWN_ValidateImage(const wchar_t*p,DWORD i,SWN_ImageInfo*m){try{if(m&&m->cbSize<sizeof(*m))return ERROR_INVALID_PARAMETER;return validate(p,i,m);}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}
extern "C" DWORD SWN_CALL SWN_DownloadImage(const SWN_Metadata*m,DWORD i,const wchar_t*dest,const SWN_Options*o,SWN_ImageInfo*info,DWORD*status){try{if(!m||m->cbSize<sizeof(*m)||!dest||!*dest||i>=SWN_SIZE_COUNT||(o&&o->cbSize<sizeof(*o))||(info&&info->cbSize<sizeof(*info)))return ERROR_INVALID_PARAMETER;wchar_t url[SWN_URL_CAP]{};DWORD e=SWN_BuildImageUrl(m,i,url,_countof(url));if(e)return e;Reply r;e=Get(url,o,r);if(status)*status=r.status;if(e)return e;if(IsCancelled(o?o->cancelEvent:nullptr))return ERROR_CANCELLED;DWORD w=0,h=0;if(!jpeg(r.bytes,w,h)||!dims(i,w,h)||r.bytes.size()<4||r.bytes[r.bytes.size()-2]!=255||r.bytes.back()!=217)return ERROR_BAD_FORMAT;std::wstring tmp=std::wstring(dest)+L".swn-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);if(f==INVALID_HANDLE_VALUE)return LastError();size_t p=0;DWORD err=0;while(p<r.bytes.size()){if(IsCancelled(o?o->cancelEvent:nullptr)){err=ERROR_CANCELLED;break;}DWORD putn=0,chunk=static_cast<DWORD>(std::min<size_t>(r.bytes.size()-p,1u<<20));if(!WriteFile(f,r.bytes.data()+p,chunk,&putn,nullptr)||!putn){err=LastError();break;}p+=putn;}if(!err&&!FlushFileBuffers(f))err=LastError();CloseHandle(f);if(err){DeleteFileW(tmp.c_str());return err;}if(!MoveFileExW(tmp.c_str(),dest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){err=LastError();DeleteFileW(tmp.c_str());return err;}if(info){info->width=w;info->height=h;info->bytes=r.bytes.size();}return 0;}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_GEN_FAILURE;}}
