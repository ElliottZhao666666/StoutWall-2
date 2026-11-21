#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "PlatformApi.h"
#include "ThemeName.hpp"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <algorithm>
#include <map>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <new>

#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"user32.lib")

#pragma comment(linker, "/EXPORT:SWP_LoadConfig=_SWP_LoadConfig@8")
#pragma comment(linker, "/EXPORT:SWP_SaveConfig=_SWP_SaveConfig@8")
#pragma comment(linker, "/EXPORT:SWP_GetChangeInterval=_SWP_GetChangeInterval@16")
#pragma comment(linker, "/EXPORT:SWP_SetWallpaper=_SWP_SetWallpaper@4")
#pragma comment(linker, "/EXPORT:SWP_CopyFile=_SWP_CopyFile@16")
#pragma comment(linker, "/EXPORT:SWP_RevealFile=_SWP_RevealFile@4")
#pragma comment(linker, "/EXPORT:SWP_ExtractArchive=_SWP_ExtractArchive@24")
#pragma comment(linker, "/EXPORT:SWP_CreateArchive=_SWP_CreateArchive@24")
#pragma comment(linker, "/EXPORT:SWP_ExportTheme=_SWP_ExportTheme@28")
namespace fs=std::filesystem;
namespace {
DWORD LastError(){DWORD e=GetLastError();return e?e:ERROR_GEN_FAILURE;}
std::wstring Lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s;}
std::wstring Trim(std::wstring s){size_t a=s.find_first_not_of(L" \t\r\n");if(a==s.npos)return L"";return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);}
bool Put(wchar_t*d,size_t cap,const std::wstring&s){if(!d||s.size()>=cap)return false;std::copy(s.begin(),s.end(),d);d[s.size()]=0;return true;}
bool ValidStruct(DWORD n,DWORD size){return n>=size;}
bool Cancel(HANDLE h){return h&&WaitForSingleObject(h,0)==WAIT_OBJECT_0;}
DWORD Timeout(const SWP_Options*o,DWORD fallback){return !o||!o->timeoutMs?fallback:std::min<DWORD>(o->timeoutMs,3600000);}
bool Absolute(const wchar_t*p){return p&&*p&&fs::path(p).is_absolute();}
std::wstring Full(const wchar_t*p){return fs::absolute(fs::path(p)).lexically_normal().wstring();}
bool ReadWide(const std::wstring&p,std::wstring&out){std::ifstream f(fs::path(p),std::ios::binary);if(!f)return false;std::string b((std::istreambuf_iterator<char>(f)),{});if(b.size()>16*1024*1024)return false;UINT cp=CP_UTF8;size_t off=0;if(b.size()>=2&&(unsigned char)b[0]==0xff&&(unsigned char)b[1]==0xfe){out.resize((b.size()-2)/2);memcpy(out.data(),b.data()+2,out.size()*2);return true;}if(b.size()>=3&&(unsigned char)b[0]==0xef&&(unsigned char)b[1]==0xbb&&(unsigned char)b[2]==0xbf)off=3;int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,b.data()+off,static_cast<int>(b.size()-off),nullptr,0);if(!n&&b.size()>off)n=MultiByteToWideChar(CP_ACP,0,b.data()+off,static_cast<int>(b.size()-off),nullptr,0),cp=CP_ACP;if(!n&&b.size()>off)return false;out.resize(n);return !n||MultiByteToWideChar(cp,cp==CP_UTF8?MB_ERR_INVALID_CHARS:0,b.data()+off,static_cast<int>(b.size()-off),out.data(),n)==n;}
bool WriteWideAtomic(const std::wstring&p,const std::wstring&text){
 int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
 if(n<0)return false;
 std::string bytes(static_cast<size_t>(n),0);
 if(n&&WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),bytes.data(),n,nullptr,nullptr)!=n)return false;
 std::wstring temp=p+L".swp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
 HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return false;
 const unsigned char bom[]={0xef,0xbb,0xbf};
 DWORD wrote=0;
 bool ok=WriteFile(f,bom,3,&wrote,nullptr)&&wrote==3;
 size_t off=0;
 while(ok&&off<bytes.size()){
  DWORD chunk=static_cast<DWORD>(std::min<size_t>(bytes.size()-off,1u<<20));
  if(!WriteFile(f,bytes.data()+off,chunk,&wrote,nullptr)||!wrote){ok=false;break;}
  off+=wrote;
 }
 if(ok)ok=FlushFileBuffers(f)!=FALSE;
 DWORD e=ok?0:LastError();
 CloseHandle(f);
 if(ok){
  for(int attempt=0;;++attempt){
   if(MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))break;
   e=LastError();
   if(attempt>=5&&(e!=ERROR_ACCESS_DENIED&&e!=ERROR_SHARING_VIOLATION&&e!=ERROR_LOCK_VIOLATION)){ok=false;break;}
   if(attempt>=5){ok=false;break;}
   Sleep(25*(attempt+1));
  }
 }
 if(!ok){DeleteFileW(temp.c_str());SetLastError(e?e:ERROR_GEN_FAILURE);}
 return ok;
}
struct Ini{std::vector<std::wstring>lines;std::wstring section;
 bool load(const std::wstring&p){std::wstring text;if(!ReadWide(p,text))return false;size_t b=0;while(b<text.size()){size_t e=text.find_first_of(L"\r\n",b);lines.push_back(text.substr(b,e==text.npos?text.size()-b:e-b));if(e==text.npos)break;b=e+1;if(text[e]==L'\r'&&b<text.size()&&text[b]==L'\n')++b;}return true;}
 void set(const wchar_t*sec,const wchar_t*key,const std::wstring&value){std::wstring target=Lower(sec);size_t begin=lines.size(),end=lines.size();std::wstring current;for(size_t i=0;i<lines.size();++i){std::wstring t=Trim(lines[i]);if(t.size()>2&&t.front()==L'['&&t.back()==L']'){current=Lower(Trim(t.substr(1,t.size()-2)));if(current==target){if(begin==lines.size())begin=i+1;}else if(begin!=lines.size()&&end==lines.size()){end=i;break;}}}if(begin==lines.size()){if(!lines.empty()&&!lines.back().empty())lines.emplace_back();lines.push_back(L"["+std::wstring(sec)+L"]");begin=lines.size();end=begin;}else if(end==lines.size())end=lines.size();for(size_t i=begin;i<end;++i){std::wstring t=Trim(lines[i]);size_t eq=t.find(L'=');if(eq!=t.npos&&Lower(Trim(t.substr(0,eq)))==Lower(key)){lines[i]=std::wstring(key)+L"="+value;return;}}lines.insert(lines.begin()+static_cast<std::ptrdiff_t>(end),std::wstring(key)+L"="+value);}
 std::wstring get(const wchar_t*sec,const wchar_t*key,const wchar_t*def)const{std::wstring target=Lower(sec),current;for(const auto&line:lines){std::wstring t=Trim(line);if(t.size()>2&&t.front()==L'['&&t.back()==L']'){current=Lower(Trim(t.substr(1,t.size()-2)));continue;}if(current!=target)continue;size_t eq=t.find(L'=');if(eq!=t.npos&&Lower(Trim(t.substr(0,eq)))==Lower(key))return Trim(t.substr(eq+1));}return def;}
 std::wstring serialize()const{std::wstring s;for(auto&l:lines)s+=l+L"\r\n";return s;}};
DWORD Num(const Ini&i,const wchar_t*s,const wchar_t*k,DWORD def){std::wstring v=i.get(s,k,L"");if(v.empty())return def;try{size_t n=0;unsigned long x=std::stoul(v,&n);return n==v.size()&&x<=0xfffffffful?static_cast<DWORD>(x):def;}catch(...){return def;}}
void ApplyDefaults(SWP_Config&c){c.mode=SWP_DAILY;c.changeFreq=3;c.changeRandom=0;c.autoSave=1;c.first=1;c.source=2;c.paused=0;c.renderPoint=1;SYSTEMTIME t{};GetLocalTime(&t);swprintf_s(c.galleryCurrent,L"%04u%02u%02u",t.wYear,t.wMonth,t.wDay);}
void LoadFields(const Ini&i,SWP_Config&c){c.mode=Num(i,L"General",L"Mode",1);c.changeFreq=Num(i,L"General",L"ChangeFreq",3);c.changeRandom=Num(i,L"General",L"ChangeRandom",0);c.autoSave=Num(i,L"General",L"AutoSave",1);c.first=Num(i,L"General",L"First",1);c.source=Num(i,L"General",L"source",2);Put(c.galleryDir,_countof(c.galleryDir),i.get(L"Gallery",L"dir",L""));Put(c.galleryCurrent,_countof(c.galleryCurrent),i.get(L"Gallery",L"current",L""));c.paused=Num(i,L"Gallery",L"pause",0);c.renderPoint=Num(i,L"Gallery",L"renderpoint",1);Put(c.currentWall.enddate,_countof(c.currentWall.enddate),i.get(L"CurrentWall",L"enddate",L""));Put(c.currentWall.title,_countof(c.currentWall.title),i.get(L"CurrentWall",L"title",L""));Put(c.currentWall.description,_countof(c.currentWall.description),i.get(L"CurrentWall",L"description",L""));Put(c.currentWall.author,_countof(c.currentWall.author),i.get(L"CurrentWall",L"author",L""));Put(c.currentWall.copyrightlink,_countof(c.currentWall.copyrightlink),i.get(L"CurrentWall",L"copyrightlink",L""));c.currentWall.favorite=Num(i,L"CurrentWall",L"favorite",0);c.currentWall.size=Num(i,L"CurrentWall",L"size",0);}
void SaveFields(Ini&i,const SWP_Config&c){auto set=[&](const wchar_t*s,const wchar_t*k,DWORD n){i.set(s,k,std::to_wstring(n));};set(L"General",L"Mode",c.mode);set(L"General",L"ChangeFreq",c.changeFreq);set(L"General",L"ChangeRandom",c.changeRandom);set(L"General",L"AutoSave",c.autoSave);set(L"General",L"First",c.first);set(L"General",L"source",c.source);i.set(L"Gallery",L"dir",c.galleryDir);i.set(L"Gallery",L"current",c.galleryCurrent);set(L"Gallery",L"pause",c.paused);set(L"Gallery",L"renderpoint",c.renderPoint);i.set(L"CurrentWall",L"enddate",c.currentWall.enddate);i.set(L"CurrentWall",L"title",c.currentWall.title);i.set(L"CurrentWall",L"description",c.currentWall.description);i.set(L"CurrentWall",L"author",c.currentWall.author);set(L"CurrentWall",L"favorite",c.currentWall.favorite);i.set(L"CurrentWall",L"copyrightlink",c.currentWall.copyrightlink);set(L"CurrentWall",L"size",c.currentWall.size);}
std::wstring Quote(const std::wstring&s){std::wstring q=L"\"";size_t slashes=0;for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'"'){q.append(slashes*2+1,L'\\');q+=c;slashes=0;continue;}q.append(slashes,L'\\');slashes=0;q+=c;}q.append(slashes*2,L'\\');q+=L'"';return q;}
DWORD Run(const std::wstring&exe,const std::vector<std::wstring>&args,const std::wstring&cwd,const SWP_Options*o,DWORD fallback,DWORD*exitCode){if(!Absolute(exe.c_str()))return ERROR_INVALID_NAME;if(Cancel(o?o->cancelEvent:nullptr))return ERROR_CANCELLED;std::wstring cmd=Quote(exe);for(const auto&a:args)cmd+=L" "+Quote(a);std::vector<wchar_t>buf(cmd.begin(),cmd.end());buf.push_back(0);STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,cwd.empty()?nullptr:cwd.c_str(),&si,&pi))return LastError();CloseHandle(pi.hThread);ULONGLONG start=GetTickCount64();DWORD timeout=Timeout(o,fallback),result=ERROR_SUCCESS,code=0;for(;;){if(Cancel(o?o->cancelEvent:nullptr)){TerminateProcess(pi.hProcess,ERROR_CANCELLED);WaitForSingleObject(pi.hProcess,5000);result=ERROR_CANCELLED;break;}ULONGLONG elapsed=GetTickCount64()-start;if(elapsed>=timeout){TerminateProcess(pi.hProcess,ERROR_TIMEOUT);WaitForSingleObject(pi.hProcess,5000);result=ERROR_TIMEOUT;break;}DWORD wait=WaitForSingleObject(pi.hProcess,static_cast<DWORD>(std::min<ULONGLONG>(100,timeout-elapsed)));if(wait==WAIT_OBJECT_0){if(!GetExitCodeProcess(pi.hProcess,&code))result=LastError();else if(code!=0)result=ERROR_INSTALL_FAILURE;break;}if(wait==WAIT_FAILED){result=LastError();break;}}CloseHandle(pi.hProcess);if(exitCode)*exitCode=code;return result;}
std::wstring Guid(){GUID g{};if(FAILED(CoCreateGuid(&g)))return L"";wchar_t b[40]{};if(!StringFromGUID2(g,b,_countof(b)))return L"";std::wstring s=b;std::transform(s.begin(),s.end(),s.begin(),towupper);return s;}
bool SafeName(const wchar_t*n){return n&&stoutwall::ValidThemeName(n);}
struct CopyContext { HANDLE cancel; ULONGLONG start; DWORD timeout; };

static DWORD CALLBACK CopyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
                                   LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID data) {
    const auto& ctx = *static_cast<const CopyContext*>(data);
    return Cancel(ctx.cancel) || GetTickCount64() - ctx.start >= ctx.timeout
        ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

DWORD CopyFileChecked(const wchar_t* src, const wchar_t* dst, BOOL replace, const SWP_Options* options) {
    if (!src || !dst || !*src || !*dst) return ERROR_INVALID_PARAMETER;
    CopyContext ctx{options ? options->cancelEvent : nullptr, GetTickCount64(), Timeout(options, 30000)};
    if (Cancel(ctx.cancel)) return ERROR_CANCELLED;
    const DWORD flags = replace ? 0 : COPY_FILE_FAIL_IF_EXISTS;
    if (CopyFileExW(src, dst, CopyProgress, &ctx, nullptr, flags)) return ERROR_SUCCESS;
    const DWORD error = LastError();
    return error == ERROR_REQUEST_ABORTED
        ? (Cancel(ctx.cancel) ? ERROR_CANCELLED : ERROR_TIMEOUT) : error;
}
struct TempCleanup{std::wstring path;bool active=false;~TempCleanup()noexcept{if(active&&!path.empty()){try{std::error_code ec;for(fs::recursive_directory_iterator i(path,ec),end;i!=end&&!ec;i.increment(ec)){DWORD attributes=GetFileAttributesW(i->path().c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_READONLY))SetFileAttributesW(i->path().c_str(),attributes&~FILE_ATTRIBUTE_READONLY);}DWORD attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_READONLY))SetFileAttributesW(path.c_str(),attributes&~FILE_ATTRIBUTE_READONLY);fs::remove_all(path,ec);}catch(...){}}}};
DWORD CreateThemeTempDirectory(const wchar_t*outputDir,TempCleanup&cleanup){
 fs::path base(outputDir);
 for(int i=0;i<8;++i){
  std::wstring guid=Guid();if(guid.empty())return ERROR_GEN_FAILURE;
  cleanup.path=(base/(L"StoutWall-Theme-"+guid)).lexically_normal().wstring();
  if(!CreateDirectoryW(cleanup.path.c_str(),nullptr)){DWORD error=GetLastError();if(error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS)continue;return error;}
  cleanup.active=true;
  std::wstring probe=cleanup.path+L"\\.write-check";
  HANDLE file=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
  if(file==INVALID_HANDLE_VALUE)return LastError();
  CloseHandle(file);
  if(!DeleteFileW(probe.c_str()))return LastError();
  return ERROR_SUCCESS;
 }
 cleanup.path.clear();return ERROR_ALREADY_EXISTS;
}
bool ThemeImage(const wchar_t*p,DWORD index){
 static const DWORD xy[5][2]={{3840,2160},{1920,1200},{1920,1080},{1024,768},{1080,1920}};
 if(index>=5)return false;
 std::ifstream f(fs::path(p),std::ios::binary|std::ios::ate);if(!f)return false;
 const auto length=f.tellg();if(length<12||length>64*1024*1024)return false;
 f.seekg(-2,std::ios::end);unsigned char tail[2]{};
 if(!f.read(reinterpret_cast<char*>(tail),2)||tail[0]!=255||tail[1]!=217)return false;
 f.seekg(0);std::vector<unsigned char>b(static_cast<size_t>((std::min)(length,std::streampos(1024*1024))));
 if(!f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size())))return false;
 if(b[0]!=255||b[1]!=216)return false;
 size_t q=2;
 while(q+1<b.size()){
  if(b[q++]!=255)return false;while(q<b.size()&&b[q]==255)++q;if(q>=b.size())return false;
  unsigned char m=b[q++];if(m==217||m==218)break;if(m==1||(m>=208&&m<=215))continue;
  if(q+2>b.size())return false;size_t n=(b[q]<<8)|b[q+1];if(n<2||n>b.size()-q)return false;
  bool sof=(m>=192&&m<=195)||(m>=197&&m<=199)||(m>=201&&m<=203)||(m>=205&&m<=207);
  if(sof&&n>=7){
   DWORD h=(b[q+3]<<8)|b[q+4],w=(b[q+5]<<8)|b[q+6];
   // The original UHD slot also contains 4500x2531 and 4596x2583 JPEGs.
   // Preserve these originals; the other four named slots keep exact dimensions.
   if(index==0)return w>=3840&&h>=2160&&w<=12000&&h<=12000;
   return w==xy[index][0]&&h==xy[index][1];
  }
  q+=n;
 }
 return false;
}
DWORD PrepareThemeIni(const std::wstring&file){
 // Profile APIs preserve Unicode only when the existing INI is UTF-16LE.
 // Convert the isolated template before adding user-provided display names.
 std::wstring text;if(!ReadWide(file,text))return ERROR_BAD_FORMAT;
 HANDLE f=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return LastError();
 const wchar_t bom=0xfeff;DWORD wrote=0,error=ERROR_SUCCESS;
 const auto write=[&](const void*bytes,DWORD size){
  if(!WriteFile(f,bytes,size,&wrote,nullptr)){error=LastError();return false;}
  if(wrote!=size){error=ERROR_WRITE_FAULT;return false;}
  return true;
 };
 if(write(&bom,sizeof(bom))&&!text.empty())
  write(text.data(),static_cast<DWORD>(text.size()*sizeof(wchar_t)));
 if(!CloseHandle(f)&&!error)error=LastError();
 return error;
}
bool WriteThemeValue(const std::wstring&file,const wchar_t*section,const wchar_t*key,const std::wstring&value){return WritePrivateProfileStringW(section,key,value.c_str(),file.c_str())!=FALSE;}
}

extern "C" DWORD SWP_CALL SWP_LoadConfig(const wchar_t*path,SWP_Config*config){try{if(!path||!Absolute(path)||!config||!ValidStruct(config->cbSize,sizeof(*config)))return ERROR_INVALID_PARAMETER;SWP_Config c{};c.cbSize=sizeof(c);Ini ini;std::error_code ec;bool exists=fs::exists(path,ec);if(ec)return ERROR_ACCESS_DENIED;if(exists){if(!ini.load(Full(path)))return ERROR_INVALID_DATA;LoadFields(ini,c);}else{ApplyDefaults(c);if(!WriteWideAtomic(Full(path),L"[General]\r\nMode=1\r\nChangeFreq=3\r\nChangeRandom=0\r\nAutoSave=1\r\nFirst=1\r\nsource=2\r\n[Gallery]\r\ncurrent="+std::wstring(c.galleryCurrent)+L"\r\npause=0\r\nrenderpoint=1\r\n"))return LastError();}*config=c;return ERROR_SUCCESS;}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}

extern "C" DWORD SWP_CALL SWP_SaveConfig(const wchar_t*path,const SWP_Config*config){try{if(!path||!Absolute(path)||!config||!ValidStruct(config->cbSize,sizeof(*config))||config->mode<1||config->mode>3||config->changeFreq>9||config->source<1||config->source>3||config->currentWall.size>4)return ERROR_INVALID_PARAMETER;Ini ini;std::error_code ec;if(fs::exists(path,ec)){if(ec||!ini.load(Full(path)))return ERROR_INVALID_DATA;}SaveFields(ini,*config);return WriteWideAtomic(Full(path),ini.serialize())?ERROR_SUCCESS:LastError();}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}

extern "C" DWORD SWP_CALL SWP_GetChangeInterval(const SWP_Config*c,DWORD count,BOOL failed,DWORD*out){if(!c||!ValidStruct(c->cbSize,sizeof(*c))||!out||c->mode<1||c->mode>3||c->changeFreq>9)return ERROR_INVALID_PARAMETER;if(c->mode==SWP_DAILY){*out=failed?60000:1200000;return ERROR_SUCCESS;}if(count<=1){*out=0;return ERROR_SUCCESS;}static const DWORD freq[]={300000,600000,1200000,1800000,2700000,3600000,7200000,10800000,18000000,28800000};*out=freq[c->changeFreq];return ERROR_SUCCESS;}

extern "C" DWORD SWP_CALL SWP_SetWallpaper(const wchar_t* imagePath) {
    if (!imagePath || !*imagePath) return ERROR_INVALID_PARAMETER;
    const DWORD attributes = GetFileAttributesW(imagePath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_FILE_NOT_FOUND;
    SetLastError(ERROR_SUCCESS);
    if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(imagePath),
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) return ERROR_SUCCESS;
    return LastError();
}

extern "C" DWORD SWP_CALL SWP_CopyFile(const wchar_t*s,const wchar_t*d,BOOL replace,const SWP_Options*o){try{if(o&&!ValidStruct(o->cbSize,sizeof(*o)))return ERROR_INVALID_PARAMETER;return CopyFileChecked(s,d,replace,o);}catch(...){return ERROR_GEN_FAILURE;}}
extern "C" DWORD SWP_CALL SWP_RevealFile(const wchar_t*path){if(!path||!*path)return ERROR_INVALID_PARAMETER;std::wstring p=Full(path),args=L"/select,\""+p+L"\"";HINSTANCE r=ShellExecuteW(nullptr,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL);return reinterpret_cast<INT_PTR>(r)>32?ERROR_SUCCESS:ERROR_FILE_NOT_FOUND;}

extern "C" DWORD SWP_CALL SWP_ExtractArchive(const wchar_t*sevenZip,const wchar_t*archive,const wchar_t*dest,const wchar_t*password,const SWP_Options*o,DWORD*exitCode){try{if(o&&!ValidStruct(o->cbSize,sizeof(*o)))return ERROR_INVALID_PARAMETER;if(!Absolute(sevenZip)||!Absolute(archive)||!Absolute(dest)||!fs::is_regular_file(sevenZip)||!fs::is_regular_file(archive))return ERROR_INVALID_NAME;std::error_code ec;if(fs::exists(dest,ec)||ec)return ERROR_ALREADY_EXISTS;if(!fs::create_directory(dest,ec)||ec)return ERROR_PATH_NOT_FOUND;std::vector<std::wstring>a={L"x",Full(archive),L"-o"+Full(dest),L"-aoa",L"-y"};if(password&&*password)a.push_back(std::wstring(L"-p")+password);DWORD e=Run(Full(sevenZip),a,L"",o,120000,exitCode);if(e){std::error_code ignored;fs::remove_all(dest,ignored);}return e;}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}

extern "C" DWORD SWP_CALL SWP_CreateArchive(DWORD format,const wchar_t*tool,const wchar_t*source,const wchar_t*dest,const SWP_Options*o,DWORD*exitCode){try{if(format>SWP_ARCHIVE_CAB||!Absolute(tool)||!source||!dest||!fs::is_directory(source)||!fs::is_regular_file(tool)||(o&&!ValidStruct(o->cbSize,sizeof(*o))))return ERROR_INVALID_PARAMETER;std::wstring src=Full(source),out=Full(dest),exe=Full(tool);if(fs::exists(out))return ERROR_ALREADY_EXISTS;std::vector<std::wstring>a;std::wstring cwd;if(format==SWP_ARCHIVE_CAB){fs::path d(src);cwd=d.parent_path().wstring();std::wstring prefix=cwd;if(!prefix.empty()&&prefix.back()!=L'\\')prefix+=L'\\';a={L"-r",L"-p",L"-m",L"LZX:16",L"-P",prefix,L"N",out,d.filename().wstring()+L"\\*.*"};}else{a={L"a",L"-r",L"-y",format==SWP_ARCHIVE_ZIP?L"-tzip":L"-t7z",out,L"*"};cwd=src;}return Run(exe,a,cwd,o,120000,exitCode);}catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_PARAMETER;}}

extern "C" DWORD SWP_CALL SWP_ExportTheme(const SWP_ThemeOptions*t,const SWP_Path*images,DWORD count,const SWP_Options*o,wchar_t*out,DWORD outChars,DWORD*exitCode){try{
 if(!t||!ValidStruct(t->cbSize,sizeof(*t))||!SafeName(t->name)||t->kind>SWP_THEME_MODERN||t->selection>SWP_THEME_CUSTOM||t->sizeIndex>4||t->style>SWP_THEME_TILE||t->colorIndex>=18||t->intervalIndex>=6||!images||!count||count>10000||!Absolute(t->outputDir)||!Absolute(t->templateArchive)||!Absolute(t->sevenZipExe)||!Absolute(t->cabarcExe)||(o&&!ValidStruct(o->cbSize,sizeof(*o)))||!out||!outChars)return ERROR_INVALID_PARAMETER;
 if(!fs::is_directory(t->outputDir)||!fs::is_regular_file(t->templateArchive)||!fs::is_regular_file(t->sevenZipExe)||!fs::is_regular_file(t->cabarcExe))return ERROR_PATH_NOT_FOUND;
 for(DWORD i=0;i<count;++i)if(!Absolute(images[i].path)||!ThemeImage(images[i].path,t->sizeIndex))return ERROR_BAD_FORMAT;
 const std::wstring ext=t->kind==SWP_THEME_MODERN?L".deskthemepack":L".themepack";std::wstring output=(fs::path(t->outputDir)/(std::wstring(t->name)+ext)).wstring();if(fs::exists(output))return ERROR_ALREADY_EXISTS;if(output.size()+1>outChars)return ERROR_INSUFFICIENT_BUFFER;
  TempCleanup cleanup;DWORD tempError=CreateThemeTempDirectory(t->outputDir,cleanup);if(tempError)return tempError;const std::wstring root=cleanup.path+L"\\theme";
 const wchar_t* password = t->templatePassword[0] ? t->templatePassword : L"YfsTyyFR,JChSgJCr,R4t,f4GYDbceDH";
 DWORD e=SWP_ExtractArchive(t->sevenZipExe,t->templateArchive,root.c_str(),password,o,exitCode);if(e)return e;
 std::wstring ini=root+L"\\ThemeBase.ini",desktop=root+L"\\DesktopBackground";if(!fs::is_regular_file(ini)||!fs::is_directory(desktop)||!fs::is_regular_file(root+L"\\brand.png"))return ERROR_FILE_NOT_FOUND;
 e=PrepareThemeIni(ini);if(e)return e;
 static const wchar_t*colors[]={L"0XC40078D4",L"0XC40078D7",L"0X6B74B8FC",L"0XA80046AD",L"0X8032CDCD",L"0X6614A600",L"0X6697D937",L"0X54FADC0E",L"0X80FF9C00",L"0XA8CE0F0F",L"0X66FF0099",L"0X70FCC7F8",L"0X856E3BA1",L"0X528D5A94",L"0X6698844C",L"0XA84F1B1B",L"0X80555555",L"0X54FCFCFC"};
 static const wchar_t*styles[]={L"0",L"2",L"6",L"10"};static const wchar_t*intervals[]={L"60000",L"600000",L"1800000",L"3600000",L"21600000",L"86400000"};
 auto write=[&](const wchar_t*s,const wchar_t*k,const std::wstring&v){return WriteThemeValue(ini,s,k,v);};
 if(!write(L"VisualStyles",L"ColorizationColor",colors[t->colorIndex]))return LastError();
 if(t->style==SWP_THEME_TILE){if(!write(L"Control Panel\\Desktop",L"TileWallpaper",L"1"))return LastError();}
 else{if(!write(L"Control Panel\\Desktop",L"TileWallpaper",L"0")||!write(L"Control Panel\\Desktop",L"WallpaperStyle",styles[t->style]))return LastError();}
 if(!write(L"Control Panel\\Desktop",L"Pattern",L""))return LastError();
 if(t->kind==SWP_THEME_MODERN){if(!write(L"VisualStyles",L"Transparency",L"1")||!write(L"VisualStyles",L"Composition",L"1")||!write(L"VisualStyles",L"VisualStyleVersion",L"10"))return LastError();if(t->autoColor&&!write(L"VisualStyles",L"AutoColorization",L"1"))return LastError();if(t->dark&&(!write(L"VisualStyles",L"AppMode",L"Dark")||!write(L"VisualStyles",L"SystemMode",L"Dark")))return LastError();if(!write(L"MasterThemeSelector",L"MTSM",L"RJSPBS")||!write(L"Control Panel\\Desktop",L"MultimonBackgrounds",L"0"))return LastError();std::wstring guid=Guid();if(guid.empty())return ERROR_GEN_FAILURE;if(!write(L"Theme",L"ThemeId",guid))return LastError();}
 else{if(!write(L"VisualStyles",L"Transparency",t->transparency?L"1":L"0")||!write(L"VisualStyles",L"Composition",L"1")||!write(L"VisualStyles",L"VisualStyleVersion",L"10")||!write(L"MasterThemeSelector",L"MTSM",L"DABJDKT"))return LastError();}
 if(!write(L"Theme",L"DisplayName",t->name)||!write(L"Theme",L"BrandImage",L"brand.png"))return LastError();
 const bool slideshow=t->selection!=SWP_THEME_FAVORITES||count!=1;
 if(slideshow){if(!write(L"Slideshow",L"Interval",intervals[t->intervalIndex])||!write(L"Slideshow",L"Shuffle",t->shuffle?L"1":L"0")||!write(L"Slideshow",L"ImagesRootPath",L"DesktopBackground"))return LastError();}
 WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());
 for(DWORD i=0;i<count;++i){if(Cancel(o?o->cancelEvent:nullptr))return ERROR_CANCELLED;std::wstring target=(fs::path(desktop)/(L"img"+std::to_wstring(i)+L".jpg")).wstring();DWORD ce=CopyFileChecked(images[i].path,target.c_str(),FALSE,o);if(ce)return ce;}
 std::wstring main=root+L"\\main.theme";if(!MoveFileExW(ini.c_str(),main.c_str(),MOVEFILE_REPLACE_EXISTING))return LastError();
 const std::wstring stagedPackage=(fs::path(cleanup.path)/L"package.cab").wstring();
 e=SWP_CreateArchive(SWP_ARCHIVE_CAB,t->cabarcExe,root.c_str(),stagedPackage.c_str(),o,exitCode);if(e)return e;
 if(Cancel(o?o->cancelEvent:nullptr))return ERROR_CANCELLED;
 if(!MoveFileExW(stagedPackage.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH))return LastError();
 std::copy(output.begin(),output.end(),out);out[output.size()]=0;return ERROR_SUCCESS;
 }catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}}
