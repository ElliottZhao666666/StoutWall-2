#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ProductBackend.hpp"
#include "ThemeName.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
namespace {
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::vector<char> Read(const fs::path& path){
    std::ifstream f(path,std::ios::binary);Require(bool(f),"Cannot read test output");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
std::wstring Ini(const fs::path& file,const wchar_t* section,const wchar_t* key){
    wchar_t value[2048]{};GetPrivateProfileStringW(section,key,L"<absent>",value,2048,file.c_str());return value;
}
void CleanStaging(const fs::path& output){
    for(const auto& e:fs::directory_iterator(output))
        Require(e.path().filename().wstring().find(L"StoutWall-Theme-")!=0,"Theme staging directory leaked");
}
}
int wmain(int argc,wchar_t**argv){
    try{
        _setmode(_fileno(stdout),_O_U8TEXT);
        std::wcout<<std::unitbuf;Require(argc==4,"usage: theme-integration-test RUNTIME FIXTURE NEW_ROOT");
        const fs::path runtime=fs::absolute(argv[1]),fixture=fs::absolute(argv[2]),root=fs::absolute(argv[3]);
        Require(!fs::exists(root),"Test root must be new");
        fs::create_directories(root/L"Gallery");fs::create_directories(root/L"images");fs::create_directories(root/L"output");
        for(const auto& e:fs::directory_iterator(fixture))if(e.is_regular_file())
            fs::copy_file(e.path(),root/L"Gallery"/e.path().filename());
        stoutwall::ProductBackend backend;
        const auto initial=backend.Initialize(runtime.wstring(),root.wstring(),(root/L"StoutWall.cfg").wstring(),true,nullptr,L"");
        Require(initial.favorites>=2,"Fixture must contain multiple favorites");
        std::vector<SWP_Path> allUhd;std::vector<SWP_Path> favorites[5];
        for(const auto& record:initial.records){
            for(DWORD size=0;size<5;++size){
                if(size&&!record.favorite)continue;
                const fs::path path=root/L"images"/(std::wstring(record.date)+L"-"+std::to_wstring(size)+L".jpg");
                Require(Library_ExportW(record.date,size+1,path.c_str(),0)>=0,"Cannot export fixture image");
                SWP_Path image{};wcscpy_s(image.path,path.c_str());
                if(size==0)allUhd.push_back(image);if(record.favorite)favorites[size].push_back(image);
            }
        }
        SWP_ThemeOptions theme{};theme.cbSize=sizeof(theme);
        wcscpy_s(theme.outputDir,(root/L"output").c_str());
        wcscpy_s(theme.templateArchive,(runtime/L"ForTheme.EDT").c_str());
        wcscpy_s(theme.sevenZipExe,(runtime/L"7za.exe").c_str());
        wcscpy_s(theme.cabarcExe,(runtime/L"cabarc.exe").c_str());
        SWP_Options options{};options.cbSize=sizeof(options);options.timeoutMs=60000;
        const auto run=[&](const std::vector<SWP_Path>& paths,DWORD expected=ERROR_SUCCESS){
            wchar_t path[SWP_PATH_CAP]{};DWORD toolExit=0;
            const DWORD error=SWP_ExportTheme(&theme,paths.data(),static_cast<DWORD>(paths.size()),&options,path,_countof(path),&toolExit);
            if(error!=expected)std::wcout<<L"Unexpected error "<<error<<L" tool "<<toolExit<<L" in "<<theme.name<<L"\n";
            Require(error==expected,"Unexpected theme export status");CleanStaging(root/L"output");return fs::path(path);
        };
        const auto inspect=[&](const fs::path& package,const std::vector<SWP_Path>& paths,const std::wstring& id,bool slideshow){
            const auto unpacked=root/(L"inspect-"+id);DWORD toolExit=0;
            Require(SWP_ExtractArchive(theme.sevenZipExe,package.c_str(),unpacked.c_str(),nullptr,&options,&toolExit)==0,"Cannot inspect theme package");
            fs::path ini;for(const auto& e:fs::recursive_directory_iterator(unpacked))
                if(e.path().filename()==L"main.theme"){Require(ini.empty(),"Multiple main.theme entries");ini=e.path();}
            Require(!ini.empty(),"Missing main.theme");const auto iniBytes=Read(ini);
            Require(iniBytes.size()>=2&&static_cast<unsigned char>(iniBytes[0])==255&&
                static_cast<unsigned char>(iniBytes[1])==254,"Theme INI must preserve Unicode with a UTF-16LE BOM");
            const auto background=ini.parent_path()/L"DesktopBackground";
            size_t count=0;for(const auto& e:fs::directory_iterator(background))if(e.is_regular_file())++count;
            Require(count==paths.size(),"Wrong number of archived wallpapers");
            for(size_t i=0;i<paths.size();++i)Require(Read(background/(L"img"+std::to_wstring(i)+L".jpg"))==Read(paths[i].path),"Theme altered or reordered a wallpaper");
            Require(Ini(ini,L"Theme",L"DisplayName")==theme.name,"Theme display name mismatch");
            Require(Ini(ini,L"Slideshow",L"Interval")== (slideshow?std::vector<std::wstring>{L"60000",L"600000",L"1800000",L"3600000",L"21600000",L"86400000"}[theme.intervalIndex]:L"<absent>"),"Slideshow interval/single favorite mismatch");
            if(slideshow)Require(Ini(ini,L"Slideshow",L"Shuffle")== (theme.shuffle?L"1":L"0"),"Shuffle mismatch");
            Require(Ini(ini,L"Control Panel\\Desktop",L"TileWallpaper")== (theme.style==SWP_THEME_TILE?L"1":L"0"),"Tile setting mismatch");
            if(theme.style!=SWP_THEME_TILE)Require(Ini(ini,L"Control Panel\\Desktop",L"WallpaperStyle")==
                std::vector<std::wstring>{L"0",L"2",L"6",L"10"}[theme.style],"Wallpaper style mismatch");
            const wchar_t* colors[]={L"0XC40078D4",L"0XC40078D7",L"0X6B74B8FC",L"0XA80046AD",L"0X8032CDCD",L"0X6614A600"};
            Require(theme.colorIndex<_countof(colors)&&Ini(ini,L"VisualStyles",L"ColorizationColor")==colors[theme.colorIndex],"Theme color mismatch");
            if(theme.kind==SWP_THEME_MODERN){
                Require(Ini(ini,L"VisualStyles",L"AutoColorization")== (theme.autoColor?L"1":L"<absent>"),"Automatic color mismatch");
                Require(Ini(ini,L"VisualStyles",L"AppMode")== (theme.dark?L"Dark":L"<absent>"),"Dark mode mismatch");
            }else Require(Ini(ini,L"VisualStyles",L"Transparency")== (theme.transparency?L"1":L"0"),"Transparency mismatch");
        };
        for(DWORD kind=0;kind<2;++kind)for(DWORD size=0;size<5;++size){
            const std::wstring id=L"favorites-"+std::to_wstring(kind)+L"-"+std::to_wstring(size);
            theme.kind=kind;theme.selection=SWP_THEME_FAVORITES;theme.sizeIndex=size;theme.style=size;theme.intervalIndex=size;
            theme.colorIndex=size+1;theme.shuffle=size%2;theme.transparency=size%2;theme.autoColor=size%2;theme.dark=size%2;wcscpy_s(theme.name,id.c_str());
            const auto package=run(favorites[size]);inspect(package,favorites[size],id,true);
            const auto bytes=Read(package);const auto modified=fs::last_write_time(package);run(favorites[size],ERROR_ALREADY_EXISTS);
            Require(Read(package)==bytes&&fs::last_write_time(package)==modified,"Same-name export modified a package");
            std::wcout<<L"PASS: "<<id<<L" image bytes, parameters, and same-name protection.\n";
        }
        theme.kind=SWP_THEME_MODERN;theme.sizeIndex=0;theme.selection=SWP_THEME_ALL;theme.intervalIndex=5;
        wcscpy_s(theme.name,L"all-uhd");inspect(run(allUhd),allUhd,L"all-uhd",true);
        theme.selection=SWP_THEME_FAVORITES;wcscpy_s(theme.name,L"single-favorite");
        std::vector<SWP_Path> single{favorites[0].back()};inspect(run(single),single,L"single-favorite",false);
        wcscpy_s(theme.name,L"我的收藏-\u0112\u0416\U0001F304");
        inspect(run(single),single,L"unicode-name",false);
        wcscpy_s(theme.name,L"failure-retry");
        {std::ofstream f(root/L"broken.EDT",std::ios::binary);f<<"not an archive";}
        wcscpy_s(theme.templateArchive,(root/L"broken.EDT").c_str());run(single,ERROR_INSTALL_FAILURE);
        Require(!fs::exists(root/L"output"/L"failure-retry.deskthemepack"),"Failed archive published output");
        wcscpy_s(theme.templateArchive,(runtime/L"ForTheme.EDT").c_str());inspect(run(single),single,L"failure-retry",false);
        wcscpy_s(theme.name,L"wrong-slot");run(favorites[2],ERROR_BAD_FORMAT);
        const auto truncated=root/L"truncated.jpg";auto bytes=Read(single[0].path);bytes.resize(bytes.size()-2);
        {std::ofstream f(truncated,std::ios::binary);f.write(bytes.data(),bytes.size());}
        std::vector<SWP_Path> invalid(1);wcscpy_s(invalid[0].path,truncated.c_str());run(invalid,ERROR_BAD_FORMAT);
        for(const auto* name:{L"",L"CON",L"nul.theme",L"COM1",L"LPT²",L"bad/name",L"bad\nname",L"bad.",L"bad "}){
            Require(!stoutwall::ValidThemeName(name),"Invalid theme name accepted");wcscpy_s(theme.name,name);run(single,ERROR_INVALID_PARAMETER);
        }
        Require(stoutwall::ValidThemeName(L"我的壁纸合集 2026"),"Valid Chinese name rejected");
        std::wcout<<L"PASS: all UHD, one favorite, Unicode name, bad archive and retry, invalid JPEG/slot/name.\nRoot: "<<root.wstring()<<L"\n";
        stoutwall::ProductRequest shutdown;shutdown.action=stoutwall::ProductAction::Shutdown;backend.Execute(shutdown);return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
