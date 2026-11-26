#pragma once
#include "LibraryApi.h"
#include <vector>

namespace stoutwall {
// The legacy DLL reads/decompresses even for a length query. Its API returns
// required bytes without writing when capacity is insufficient. Avoid a second
// read for common JPEGs while retaining a bounded fallback for larger images.
inline int32_t ReadGalleryImage(const wchar_t* date, int32_t size,
                               std::vector<unsigned char>& bytes) {
    constexpr int32_t initialCapacity = 4 * 1024 * 1024;
    constexpr int32_t imageLimit = 64 * 1024 * 1024;
    bytes.resize(initialCapacity);
    int32_t length = Library_GetImage(date, size, bytes.data(), initialCapacity);
    if (length <= 0 || length > imageLimit) { bytes.clear(); return length > imageLimit ? -1 : length; }
    if (length > initialCapacity) {
        bytes.resize(static_cast<size_t>(length));
        const int32_t actual = Library_GetImage(date, size, bytes.data(), length);
        if (actual != length) { bytes.clear(); return actual < 0 ? actual : -1; }
    }
    bytes.resize(static_cast<size_t>(length));
    return length;
}
}
