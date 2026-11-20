#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(SWN_BUILD)
#define SWN_API __declspec(dllexport)
#else
#define SWN_API __declspec(dllimport)
#endif
#define SWN_CALL __stdcall
#define SWN_URL_CAP 2048
#define SWN_TEXT_CAP 512
#define SWN_DATE_CAP 9
#define SWN_SIZE_COUNT 5

enum SWN_Source { SWN_GLOBAL = 1, SWN_CN = 2, SWN_CUSTOM = 3 };
enum SWN_Size { SWN_UHD, SWN_1920x1200, SWN_1920x1080, SWN_1024x768, SWN_1080x1920 };

#pragma pack(push, 4)
typedef struct SWN_Options {
    DWORD cbSize;
    DWORD timeoutMs; // Zero = 45 s. Includes all network phases.
    HANDLE cancelEvent; // Optional, manual-reset; caller owns and retains it.
    wchar_t proxy[SWN_TEXT_CAP]; // Empty = system WinHTTP proxy; otherwise host:port.
} SWN_Options;

typedef struct SWN_Metadata {
    DWORD cbSize;
    wchar_t enddate[SWN_DATE_CAP];
    wchar_t title[SWN_TEXT_CAP];
    wchar_t description[SWN_TEXT_CAP];
    wchar_t author[SWN_TEXT_CAP];
    wchar_t copyrightlink[SWN_URL_CAP];
    wchar_t urlbase[SWN_URL_CAP];
    DWORD wp;
} SWN_Metadata;

typedef struct SWN_ImageInfo {
    DWORD cbSize;
    DWORD width;
    DWORD height;
    ULONGLONG bytes;
} SWN_ImageInfo;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif
// Return ERROR_SUCCESS or a Win32/WinHTTP error. Initialize all cbSize fields.
// Only read outputs on success. httpStatus, info and options may be NULL.
SWN_API DWORD SWN_CALL SWN_FetchLatest(DWORD source, const SWN_Options* options,
                             SWN_Metadata* metadata, DWORD* httpStatus);
SWN_API DWORD SWN_CALL SWN_ParseMetadata(DWORD source, const void* utf8, DWORD bytes,
                                SWN_Metadata* metadata);
SWN_API DWORD SWN_CALL SWN_BuildImageUrl(const SWN_Metadata* metadata, DWORD sizeIndex,
                                wchar_t* url, DWORD urlChars);
// Downloads and verifies a JPEG of the requested size before replacing destination.
SWN_API DWORD SWN_CALL SWN_DownloadImage(const SWN_Metadata* metadata, DWORD sizeIndex,
                                const wchar_t* destination, const SWN_Options* options,
                                SWN_ImageInfo* info, DWORD* httpStatus);
// Pass sizeIndex == SWN_SIZE_COUNT to validate any supported JPEG dimensions.
SWN_API DWORD SWN_CALL SWN_ValidateImage(const wchar_t* path, DWORD sizeIndex,
                                SWN_ImageInfo* info);
#ifdef __cplusplus
}
#endif
