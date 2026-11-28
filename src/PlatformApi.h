#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(STOUTWALL_STATIC_LINK)
#define SWP_API
#elif defined(SWP_BUILD)
#define SWP_API __declspec(dllexport)
#else
#define SWP_API __declspec(dllimport)
#endif
#define SWP_CALL __stdcall
#define SWP_PATH_CAP 1024
#define SWP_TEXT_CAP 512
#define SWP_URL_CAP 2048

enum SWP_Mode { SWP_DAILY = 1, SWP_GALLERY = 2, SWP_FAVORITES = 3 };
enum SWP_ThemeKind { SWP_THEME_WIN7 = 0, SWP_THEME_MODERN = 1 };
enum SWP_ThemeStyle { SWP_THEME_CENTER, SWP_THEME_STRETCH, SWP_THEME_FIT,
                      SWP_THEME_FILL, SWP_THEME_TILE };
enum SWP_ThemeSelection { SWP_THEME_ALL, SWP_THEME_FAVORITES, SWP_THEME_CUSTOM };
enum SWP_ArchiveFormat { SWP_ARCHIVE_7Z, SWP_ARCHIVE_ZIP, SWP_ARCHIVE_CAB };

#pragma pack(push, 4)
typedef struct SWP_Options {
    DWORD cbSize;
    DWORD timeoutMs; // Zero = 120 s for archive work, 30 s for copying.
    HANDLE cancelEvent; // Optional, manual-reset; caller owns and retains it.
} SWP_Options;

typedef struct SWP_CurrentWall {
    wchar_t enddate[9];
    wchar_t title[SWP_TEXT_CAP];
    wchar_t description[SWP_TEXT_CAP];
    wchar_t author[SWP_TEXT_CAP];
    wchar_t copyrightlink[SWP_URL_CAP];
    DWORD favorite;
    DWORD size; // 0..4, the original combobox order.
} SWP_CurrentWall;

typedef struct SWP_Config {
    DWORD cbSize;
    DWORD mode, changeFreq, changeRandom, autoSave, first, source;
    wchar_t galleryDir[SWP_PATH_CAP];
    wchar_t galleryCurrent[9];
    DWORD paused, renderPoint;
    SWP_CurrentWall currentWall;
} SWP_Config;

typedef struct SWP_Path { wchar_t path[SWP_PATH_CAP]; } SWP_Path;

typedef struct SWP_ThemeOptions {
    DWORD cbSize;
    DWORD kind, selection, sizeIndex, style, colorIndex, intervalIndex;
    DWORD shuffle, transparency, autoColor, dark;
    wchar_t name[128];
    wchar_t outputDir[SWP_PATH_CAP];
    wchar_t templateArchive[SWP_PATH_CAP]; // Original ForTheme.EDT or compatible trusted template.
    wchar_t sevenZipExe[SWP_PATH_CAP];
    wchar_t cabarcExe[SWP_PATH_CAP];
    wchar_t templatePassword[128]; // Empty uses the original EDT password.
} SWP_ThemeOptions;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif
// ERROR_SUCCESS == 0. Set cbSize on each input/output struct that has one.
// Absolute, caller-selected paths. No implicit access to the DLL/EXE directory.
SWP_API DWORD SWP_CALL SWP_LoadConfig(const wchar_t* iniPath, SWP_Config* config);
SWP_API DWORD SWP_CALL SWP_SaveConfig(const wchar_t* iniPath, const SWP_Config* config);
// wallpaperCount is the count for the active mode. Daily timeout retries after 60 s.
SWP_API DWORD SWP_CALL SWP_GetChangeInterval(const SWP_Config* config, DWORD wallpaperCount,
                                   BOOL dailyFetchFailed, DWORD* intervalMs);
// Uses the current Windows wallpaper style. Keep the applied file available.
SWP_API DWORD SWP_CALL SWP_SetWallpaper(const wchar_t* imagePath);
SWP_API DWORD SWP_CALL SWP_CopyFile(const wchar_t* source, const wchar_t* destination,
                           BOOL replace, const SWP_Options* options);
SWP_API DWORD SWP_CALL SWP_RevealFile(const wchar_t* path);
// Only pass trusted tools and archives. Extraction requires an absent destination.
SWP_API DWORD SWP_CALL SWP_ExtractArchive(const wchar_t* sevenZipExe, const wchar_t* archive,
                                 const wchar_t* destinationDir, const wchar_t* password,
                                 const SWP_Options* options, DWORD* toolExitCode);
// Creates an archive of the contents of sourceDir. CAB uses cabarc; ZIP/7Z use 7za.
SWP_API DWORD SWP_CALL SWP_CreateArchive(DWORD format, const wchar_t* toolExe, const wchar_t* sourceDir,
                                const wchar_t* destination, const SWP_Options* options,
                                DWORD* toolExitCode);
// images are ordered JPEG files already exported at sizeIndex by the caller.
SWP_API DWORD SWP_CALL SWP_ExportTheme(const SWP_ThemeOptions* theme, const SWP_Path* images,
                              DWORD imageCount, const SWP_Options* options,
                              wchar_t* outputPath, DWORD outputChars, DWORD* toolExitCode);
#ifdef __cplusplus
}
#endif
