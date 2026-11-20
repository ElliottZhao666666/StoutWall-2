#pragma once
#include <windows.h>
#include <stdint.h>
#ifdef SW_LIBRARY_BUILD
#define SWL_API extern "C" __declspec(dllexport)
#else
#define SWL_API extern "C" __declspec(dllimport)
#endif
// Caller owns every buffer. All functions run on one dedicated library thread.
struct SWL_Record {
    wchar_t date[9];
    wchar_t urlbase[2048];
    wchar_t title[512];
    wchar_t description[2048];
    wchar_t author[1024];
    wchar_t link[2048];
    int32_t favorite;
};
SWL_API int32_t __stdcall Library_OpenW(const wchar_t* runtime, const wchar_t* directory, int32_t initialize);
SWL_API int32_t __stdcall Library_Close();
SWL_API int32_t __stdcall Library_Count(int32_t favoritesOnly);
SWL_API int32_t __stdcall Library_GetRecord(int32_t ordinal, int32_t favoritesOnly, SWL_Record* record);
SWL_API int32_t __stdcall Library_Find(const wchar_t* date, SWL_Record* record);
SWL_API int32_t __stdcall Library_GetImage(const wchar_t* date, int32_t size, void* buffer, int32_t capacity);
SWL_API int32_t __stdcall Library_SetFavorite(const wchar_t* date, int32_t favorite);
SWL_API int32_t __stdcall Library_ClearFavorites();
SWL_API int32_t __stdcall Library_ExportW(const wchar_t* date, int32_t size, const wchar_t* destination, int32_t overwrite);
SWL_API int32_t __stdcall Library_ImportW(const SWL_Record* record, const wchar_t* const* fiveImages);
SWL_API int32_t __stdcall Library_Delete(const wchar_t* date);
SWL_API int32_t __stdcall Library_Check(const wchar_t* date, int32_t verifyMd5);
// Switch to a complete existing library, otherwise copy and verify the current one.
// Source data is retained until a later explicit cleanup, never recursively erased.
SWL_API int32_t __stdcall Library_RelocateW(const wchar_t* destination);
SWL_API uint64_t __stdcall Library_Bytes();
SWL_API int32_t __stdcall Library_LastErrorW(wchar_t* buffer, int32_t capacity);
