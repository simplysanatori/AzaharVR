// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

/**
 * Where what Layer Depth moves ends in the eyes' pictures over 1x: melonDS's hi-res renderer has
 * this in its shaders, whose text is changed for the eyes' renderers as they make them. melonDS
 * itself is as it is. nds_resolution_tests.cpp fails if its text becomes another one.
 *
 * As NdsDepth::IsBeyond and PixelShift have it, and the software renderer's eyes.
 */
namespace FrameCores::NdsGlEdge {

/// What an eye has beyond the edge of a layer that nothing else is in front of.
inline constexpr std::string_view kNothing = "vec4(0, 0, 0, 1)";

/// The 2D's compositor: `uEdge` is every priority's shift, in the DS's pixels, as an int each.
inline bool PatchCompositor(std::string& source) {
    const auto put = [&source](std::string_view before, std::string_view text) {
        const std::size_t place = source.find(before);
        if (place == std::string::npos ||
            source.find(before, place + before.size()) != std::string::npos) {
            return false;
        }
        source.insert(place, text);
        return true;
    };
    return put("int MosaicX = 0;", R"(uniform uvec4 uEdge;

bool BeyondBy(int shift, int x)
{
    return shift >= 0 ? x < shift : x >= 256 + shift;
}

bool Beyond(int mask, int x)
{
    int shift = 0;
    if (mask == 0x20)
        shift = int(uEdge[3]);
    for (int bg = 0; bg < 4; bg++)
    {
        // the 3D's is in its own pictures, a capture is not moved
        if (mask == (1 << bg) && !(bg == 0 && uEnable3D) && uBGConfig[bg].Type < 7)
            shift = int(uEdge[uBGPrio[bg]]);
    }
    return BeyondBy(shift, x);
}

)") &&
           put("    int winmask = uScanline[line].WinMask;",
               R"(    if (layercol[4].a > 0 && BeyondBy(int(uEdge[objflags.a & 3]), xpos))
        layercol[4] = vec4(0);

)") &&
           put("    int effect = 0;", std::string{R"(    if (Beyond(mask1, xpos))
        return )"}
                                              .append(kNothing) +
                                          R"(;
    if (Beyond(mask2 >> 8, xpos))
        col2 = ivec4(0);

)");
}

/// What the 3D has where no polygon is: `uEdge` is the columns of its picture that have it, from
/// the first to before the second (0: to the end).
inline bool PatchClearPlane(std::string& source) {
    const std::size_t main = source.find("void main()");
    const std::size_t end = source.rfind('}');
    if (main == std::string::npos || end == std::string::npos || end < main ||
        source.find("out vec4 oColor;") == std::string::npos ||
        source.find("out vec4 oAttr;") == std::string::npos) {
        return false;
    }
    source.insert(
        end,
        R"(    if (gl_FragCoord.x < float(uEdge.x) || (uEdge.y != 0u && gl_FragCoord.x >= float(uEdge.y)))
    {
        oColor.a = 0;
        oAttr.b = 0; // no fog
    }
)");
    source.insert(main, "uniform uvec4 uEdge;\n\n");
    return true;
}

/// Of all the shaders, those it is in. False: one of them is not what is known of it.
inline bool Patch(std::string& source) {
    std::string patched = source;
    bool known = true;
    if (source.find("vec4 CompositeLayers()") != std::string::npos) {
        known = PatchCompositor(patched);
    } else if (source.find("uniform uint uOpaquePolyID;") != std::string::npos) {
        known = PatchClearPlane(patched);
    }
    if (known) {
        source = std::move(patched);
    }
    return known;
}

} // namespace FrameCores::NdsGlEdge
