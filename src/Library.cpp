#define SW_LIBRARY_BUILD
#include "LibraryApi.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace fs = std::filesystem;
namespace {
HMODULE module = nullptr;
DWORD owner = 0;
bool opened = false;
fs::path root, directory;
std::wstring lastError;
using FnOpen = int32_t (__stdcall*)(const char*,int32_t);
using FnVoid = int32_t (__stdcall*)();
using FnCount = int32_t (__stdcall*)(int32_t);
using FnDate = int32_t (__stdcall*)(int32_t,int32_t,char*,int32_t);
using FnField = int32_t (__stdcall*)(const char*,int32_t,char*,int32_t);
using FnImage = int32_t (__stdcall*)(const char*,int32_t,void*,int32_t);
using FnDateOnly = int32_t (__stdcall*)(const char*);
using FnFlag = int32_t (__stdcall*)(const char*,int32_t);
using FnExport = int32_t (__stdcall*)(const char*,int32_t,const char*);
using FnImport = int32_t (__stdcall*)(const char*,const char*);
FnOpen apiOpen; FnVoid apiClose; FnCount apiCount; FnDate apiDate;
FnField apiField; FnImage apiImage; FnDateOnly apiFavorite,apiDelete,apiCleanup;
FnFlag apiSetFavorite,apiCheck; FnExport apiExport; FnImport apiImport;
struct Failure { std::wstring text; int32_t code; };
[[noreturn]] void Fail(const std::wstring& s,int32_t n=-100) { throw Failure{s,n}; }
std::string Ansi(const std::wstring& s) {
    if(s.empty()) return {};
    BOOL bad=FALSE;
    int n=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,s.c_str(),-1,nullptr,0,nullptr,&bad);
    if(!n||bad) Fail(L"图库接口无法无损表示该路径或文本（系统 ANSI 代码页）。");
    std::string r(static_cast<size_t>(n),'\0'); bad=FALSE;
    WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,s.c_str(),-1,r.data(),n,nullptr,&bad);
    if(bad) Fail(L"图库接口无法无损表示该路径或文本（系统 ANSI 代码页）。");
    r.pop_back(); return r;
}
std::wstring Wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_ACP,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring r(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_ACP,0,s.data(),static_cast<int>(s.size()),r.data(),n); return r;
}
void Thread(bool require=true) {
    if(owner && owner!=GetCurrentThreadId()) Fail(L"图库调用必须位于同一专用工作线程。");
    if(require && !opened) Fail(L"壁纸库尚未打开。",-2);
}
void Check(int32_t rc,const wchar_t* op) {if(rc<0) Fail(std::wstring(op)+L"（gallery.dll 返回 "+std::to_wstring(rc)+L"）",rc);}
template<class T> void Load(T& fn,const char* name) {
    fn=reinterpret_cast<T>(GetProcAddress(module,name));
    if(!fn) Fail(L"gallery.dll 缺少接口。");
}
void ResetApi() noexcept {
    apiOpen=nullptr; apiClose=nullptr; apiCount=nullptr; apiDate=nullptr;
    apiField=nullptr; apiImage=nullptr; apiFavorite=nullptr; apiDelete=nullptr;
    apiCleanup=nullptr; apiSetFavorite=nullptr; apiCheck=nullptr;
    apiExport=nullptr; apiImport=nullptr;
}
void Bind(const fs::path& runtime) {
    if(module) return;
    module=LoadLibraryExW((runtime/L"gallery.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!module) Fail(L"无法加载 x86 gallery.dll："+std::to_wstring(GetLastError()));
    try {
        Load(apiOpen,"Gallery_Open"); Load(apiClose,"Gallery_Close");
        Load(apiCount,"Gallery_Count"); Load(apiDate,"Gallery_GetDate");
        Load(apiField,"Gallery_GetField"); Load(apiFavorite,"Gallery_GetFavorite");
        Load(apiImage,"Gallery_GetImage");
        Load(apiSetFavorite,"Gallery_SetFavorite"); Load(apiExport,"Gallery_ExportImage");
        Load(apiImport,"Gallery_Import"); Load(apiDelete,"Gallery_Delete");
        Load(apiCheck,"Gallery_Check"); Load(apiCleanup,"Gallery_CleanupOrphan");
    } catch(...) {
        HMODULE failed=module;
        module=nullptr;
        ResetApi();
        FreeLibrary(failed);
        throw;
    }
}
std::string Date(int ordinal,int fav) {
    char b[32]{}; Check(apiDate(ordinal,fav,b,32),L"枚举壁纸"); return b;
}
std::wstring Field(const std::string& d,int field) {
    int32_t n=apiField(d.c_str(),field,nullptr,0); Check(n,L"读取壁纸信息");
    if(n>1024*1024) Fail(L"图库字段长度异常。");
    std::vector<char> buf(static_cast<size_t>(n)+1);
    Check(apiField(d.c_str(),field,buf.data(),static_cast<int32_t>(buf.size())),L"读取壁纸信息");
    return Wide(buf.data());
}
template<size_t N> void Assign(wchar_t(&out)[N],const std::wstring& s) {
    if(s.size()>=N) Fail(L"壁纸字段超出接口容量；未截断原文。");
    wcscpy_s(out,s.c_str());
}
void ReadRecord(const std::string& d,SWL_Record* r) {
    if(!r) Fail(L"输出为空。",-1); *r={};
    Assign(r->date,Wide(d)); Assign(r->urlbase,Field(d,2)); Assign(r->title,Field(d,3));
    Assign(r->description,Field(d,4)); Assign(r->author,Field(d,5)); Assign(r->link,Field(d,6));
    int n=apiFavorite(d.c_str()); Check(n,L"读取收藏状态"); r->favorite=n;
}
std::wstring Quote(const fs::path& p) { return L"\""+p.wstring()+L"\""; }
void Initialize(const fs::path& dest) {
    if(fs::exists(dest/L"Stout_WallGallery_Index.DDT")) return;
    fs::create_directories(dest);
    const auto archive=root/L"GalleryBase.EDT", exe=root/L"7za.exe";
    if(!fs::exists(archive)||!fs::exists(exe)) Fail(L"缺少 GalleryBase.EDT 或 7za.exe，无法释放原版基础壁纸库。");
    std::wstring cmd=Quote(exe)+L" x "+Quote(archive)+L" -pJcRQSCsF,RwFBMhbd,JCRswCSf,QTHL1 -o"+Quote(dest)+L" -aos -y";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{}; si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&pi)) Fail(L"启动基础包解压失败。");
    CloseHandle(pi.hThread); DWORD wait=WaitForSingleObject(pi.hProcess,120000),code=~0u;
    if(wait==WAIT_TIMEOUT) {TerminateProcess(pi.hProcess,124);WaitForSingleObject(pi.hProcess,5000);}
    GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
    if(wait!=WAIT_OBJECT_0||code!=0||!fs::exists(dest/L"Stout_WallGallery_Index.DDT")) Fail(L"原版基础壁纸库解压未成功。");
}
template<class F> int32_t Guard(F f) {
    try {lastError.clear();return f();}
    catch(const Failure& e) {lastError=e.text;return e.code;}
    catch(const std::exception& e) {lastError=Wide(e.what());return -100;}
    catch(...) {lastError=L"图库工作线程遇到未知错误。";return -100;}
}
}
SWL_API int32_t __stdcall Library_OpenW(const wchar_t* runtime,const wchar_t* path,int32_t init) {return Guard([&]()->int32_t{
    Thread(false); if(!runtime||!path||!*path) Fail(L"未指定图库位置。",-1);
    if(opened) Fail(L"请先关闭当前图库。",-8);
    owner=GetCurrentThreadId();root=fs::absolute(runtime);directory=fs::absolute(path);Bind(root);
    if(init) Initialize(directory);
    auto p=Ansi(directory.wstring());int rc=apiOpen(p.c_str(),0);Check(rc,L"打开壁纸库");opened=true;return 0;
});}
SWL_API int32_t __stdcall Library_Close(){return Guard([]()->int32_t{Thread(false);if(opened) Check(apiClose(),L"关闭壁纸库");opened=false;return 0;});}
SWL_API int32_t __stdcall Library_Count(int32_t f){return Guard([&]()->int32_t{Thread();int n=apiCount(f);Check(n,L"统计壁纸");return n;});}
SWL_API int32_t __stdcall Library_GetRecord(int32_t i,int32_t f,SWL_Record* r){return Guard([&]()->int32_t{Thread();ReadRecord(Date(i,f),r);return 0;});}
SWL_API int32_t __stdcall Library_Find(const wchar_t* d,SWL_Record* r){return Guard([&]()->int32_t{Thread();ReadRecord(Ansi(d?d:L""),r);return 0;});}
SWL_API int32_t __stdcall Library_GetImage(const wchar_t* d,int32_t size,void* b,int32_t cap){return Guard([&]()->int32_t{Thread();auto a=Ansi(d?d:L"");int n=apiImage(a.c_str(),size,b,cap);Check(n,L"读取壁纸图片");return n;});}
SWL_API int32_t __stdcall Library_SetFavorite(const wchar_t* d,int32_t f){return Guard([&]()->int32_t{Thread();auto a=Ansi(d?d:L"");int rc=apiSetFavorite(a.c_str(),f);Check(rc,L"修改收藏");return rc;});}
SWL_API int32_t __stdcall Library_ClearFavorites(){return Guard([]()->int32_t{Thread();int n=apiCount(0);Check(n,L"统计壁纸");for(int i=1;i<=n;++i){auto d=Date(i,0);Check(apiSetFavorite(d.c_str(),0),L"清空收藏");}return 0;});}
SWL_API int32_t __stdcall Library_ExportW(const wchar_t* d,int32_t size,const wchar_t* dest,int32_t overwrite){return Guard([&]()->int32_t{
    Thread();if(!dest||!*dest) Fail(L"未指定导出文件。",-1);fs::path p=fs::absolute(dest);
    if(p.parent_path().empty()) Fail(L"导出路径无效。",-1);fs::create_directories(p.parent_path());
    if(fs::exists(p)&&!overwrite) Fail(L"目标文件已存在。",-8);
    auto temp=p;temp+=L".stoutwall-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    if(fs::exists(temp)) Fail(L"目标位置存在待处理的导出暂存文件。",-8);
    auto a=Ansi(d?d:L""),b=Ansi(temp.wstring());int rc=apiExport(a.c_str(),size,b.c_str());
    if(rc<0){std::error_code ignored;fs::remove(temp,ignored);Check(rc,L"导出壁纸");}
    if(!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0))) {fs::remove(temp);Fail(L"写入导出目标失败。",-7);}return 0;
});}
SWL_API int32_t __stdcall Library_ImportW(const SWL_Record* r,const wchar_t* const* images){return Guard([&]()->int32_t{
    Thread();if(!r||!images) Fail(L"入库参数为空。",-1);
    for (int i = 0; i < 5; ++i) {
        if (!images[i] || !*images[i]) Fail(L"入库图片缺失。",-1);
    }
    fs::path pending=directory/L"work";std::error_code workError;
    fs::create_directories(pending,workError);
    if(workError) Fail(L"无法创建图库导入暂存目录："+pending.wstring()+L"（系统错误 "+std::to_wstring(workError.value())+L"）",-7);
    workError.clear();
    if(!fs::is_directory(pending,workError)) {
        const int error=workError?workError.value():ERROR_DIRECTORY;
        Fail(L"图库导入暂存路径不是可用目录："+pending.wstring()+L"（系统错误 "+std::to_wstring(error)+L"）",-7);
    }
    fs::path manifest=pending/(std::wstring(L"import-")+r->date+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".ini");
    auto path=Ansi(manifest.wstring()),date=Ansi(r->date);
    struct RemoveManifest {
        const fs::path& path;
        ~RemoveManifest() { DeleteFileW(path.c_str()); }
    } removeManifest{manifest};
    auto put=[&](const char* group,const char* key,const std::wstring& s){auto v=Ansi(s);if(!WritePrivateProfileStringA(group,key,v.c_str(),path.c_str())) Fail(L"写入图库清单失败。",-7);};
    put("Wallpaper","urlbase",r->urlbase);put("Wallpaper","title",r->title);put("Wallpaper","description",r->description);put("Wallpaper","author",r->author);put("Wallpaper","copyrightlink",r->link);
    static const char* sizeNames[]={"UHD","1920x1200","1920x1080","1024x768","1080x1920"};
    for(int i=0;i<5;++i) put("Images",sizeNames[i],images[i]);
    int rc=apiImport(date.c_str(),path.c_str());
    Check(rc,L"五尺寸壁纸入库（定长字段不会截断）");return rc;
});}
SWL_API int32_t __stdcall Library_Delete(const wchar_t* d){return Guard([&]()->int32_t{Thread();auto a=Ansi(d?d:L"");int rc=apiDelete(a.c_str());Check(rc,L"删除壁纸");return rc;});}
SWL_API int32_t __stdcall Library_Check(const wchar_t* d,int32_t md5){return Guard([&]()->int32_t{Thread();auto a=Ansi(d?d:L"");int rc=apiCheck(a.c_str(),md5);Check(rc,L"壁纸完整性校验");return rc;});}
SWL_API int32_t __stdcall Library_RelocateW(const wchar_t* destination){return Guard([&]()->int32_t{
    Thread();if(!destination||!*destination) Fail(L"请指定壁纸库目录。",-1);
    fs::path dest=fs::absolute(destination).lexically_normal(),old=directory.lexically_normal();
    if(_wcsicmp(dest.c_str(),old.c_str())==0) Fail(L"已选的是当前的壁纸库目录。",-1);
    auto oldText=old.wstring()+L"\\",newText=dest.wstring()+L"\\";
    if(newText.size()>oldText.size()&&_wcsnicmp(newText.c_str(),oldText.c_str(),oldText.size())==0) Fail(L"不能将图库迁移到自身的子目录。",-1);
    for (auto ancestor = dest; !ancestor.empty();) {
        if (fs::exists(ancestor) && fs::equivalent(ancestor, old))
            Fail(L"不能将图库迁移到自身或其子目录。",-1);
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    fs::path staging;
    std::vector<fs::path> published;
    Check(apiClose(),L"关闭原壁纸库");opened=false;
    try {
        bool useExisting=false;auto target=Ansi(dest.wstring());
        if(fs::exists(dest/L"Stout_WallGallery_Index.DDT")&&apiOpen(target.c_str(),1)==0){
            opened=true;
            useExisting=true;int n=apiCount(0);if(n<0) useExisting=false;
            for(int i=1;useExisting&&i<=n;++i){auto d=Date(i,0);if(apiCheck(d.c_str(),0)<0) useExisting=false;}
            Check(apiClose(),L"关闭待检查壁纸库");opened=false;
        }
        if(!useExisting){
            fs::create_directories(dest);
            // An incomplete destination is preserved; never overwrite arbitrary existing data.
            std::vector<fs::path> files;
            const fs::path index = L"Stout_WallGallery_Index.DDT";
            for (const auto& e : fs::directory_iterator(old)) {
                if (!e.is_regular_file()) continue;
                const auto name = e.path().filename();
                if (fs::exists(fs::symlink_status(dest / name)))
                    Fail(L"目标含不完整图库或同名文件，请先选择空目录。",-8);
                if (_wcsicmp(name.c_str(), index.c_str()) != 0) files.push_back(name);
            }
            files.push_back(index);
            published.reserve(files.size());
            auto candidate = dest / (L".stoutwall-migrate-" + std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(GetTickCount64()));
            if (!fs::create_directory(candidate)) Fail(L"目标已有同名迁移暂存目录。",-8);
            staging = std::move(candidate);
            for (const auto& name : files) fs::copy_file(old / name, staging / name);
            for (const auto& name : files) {
                auto path = dest / name;
                if (!MoveFileExW((staging / name).c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
                    Fail(L"无法将暂存文件移入目标图库（系统错误 " + std::to_wstring(GetLastError()) + L"）。",-7);
                published.push_back(std::move(path));
            }
            fs::remove(staging);
            staging.clear();
        }
        Check(apiOpen(target.c_str(),0),L"打开新壁纸库");opened=true;
        int n=apiCount(0);Check(n,L"检查新图库");for(int i=1;i<=n;++i){auto d=Date(i,0);Check(apiCheck(d.c_str(),0),L"检查迁移图片");}
        directory=dest;return 0;
    } catch (...) {
        if (opened) apiClose();
        std::error_code cleanupError, ignored;
        for (const auto& file : published) {
            fs::remove(file, ignored);
            if (ignored) cleanupError = ignored;
        }
        if (!staging.empty()) {
            fs::remove_all(staging, ignored);
            if (ignored) cleanupError = ignored;
        }
        auto p = Ansi(old.wstring());
        opened = apiOpen(p.c_str(), 0) == 0;
        if (!opened) Fail(L"迁移失败，且无法重新打开原壁纸库。",-7);
        if (cleanupError) Fail(L"迁移失败，且无法清理目标位置的复制文件。",-7);
        throw;
    }
});}
SWL_API uint64_t __stdcall Library_Bytes(){uint64_t total=0;Guard([&]()->int32_t{Thread();for(const auto& e:fs::directory_iterator(directory))if(e.is_regular_file())total+=e.file_size();return 0;});return total;}
SWL_API int32_t __stdcall Library_LastErrorW(wchar_t* b,int32_t n){int32_t need=static_cast<int32_t>(lastError.size()+1);if(b&&n>=need)memcpy(b,lastError.c_str(),static_cast<size_t>(need)*sizeof(wchar_t));return need;}
