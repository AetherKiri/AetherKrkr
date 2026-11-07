#pragma once

#include <algorithm>
#include <cstdint>
#include "ComplexRect.h"

// Presentation and pointer input share the same aspect-fit rectangle, even
// when the host requests the unscaled source texture instead of a surface.
inline tTVPRect TVPGetHostViewport(tjs_int source_w, tjs_int source_h,
                                  tjs_int surface_w, tjs_int surface_h) {
    if(source_w <= 0 || source_h <= 0 || surface_w <= 0 || surface_h <= 0)
        return tTVPRect(0, 0, 0, 0);
    tjs_int width = surface_w;
    tjs_int height = surface_h;
    if(int64_t(source_w) * surface_h > int64_t(surface_w) * source_h)
        height = std::max<tjs_int>(1, int64_t(surface_w) * source_h / source_w);
    else
        width = std::max<tjs_int>(1, int64_t(surface_h) * source_w / source_h);
    const tjs_int left = (surface_w - width) / 2;
    const tjs_int top = (surface_h - height) / 2;
    return tTVPRect(left, top, left + width, top + height);
}
