// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ds_gl_decl.h"

namespace DsGl {

/// Puts VRAM's four capture banks into the textures melonDS's hi-res renderer keeps its captures
/// in, `scale` times as big. For a renderer put in place while a game runs: a game that shows or
/// blends the bank it captures into has the capture before from these textures, never from VRAM.
/// `tex256`: a layer a bank, 256x256. `tex128`: four layers a bank, 128x128.
inline void LoadCaptures(const std::array<const uint8_t*, 4>& vram, GLuint tex256, GLuint tex128,
                         int scale) {
    GLuint from = 0;
    std::array<GLuint, 2> fbo{};
    glGenTextures(1, &from);
    glGenFramebuffers(2, fbo.data());
    glActiveTexture(GL_TEXTURE0);
    const auto load = [&](const uint8_t* data, int size, GLuint to, int layer) {
        std::vector<uint32_t> pixels(static_cast<size_t>(size) * size);
        for (size_t i = 0; i < pixels.size(); i++) {
            uint16_t c = 0;
            std::memcpy(&c, data + i * 2, 2);
            // as CaptureFS.glsl writes it
            pixels[i] = (c & 0x1Fu) << 3 | (c >> 5 & 0x1Fu) << 11 | (c >> 10 & 0x1Fu) << 19 |
                        ((c & 0x8000) != 0 ? 0xFF000000u : 0);
        }
        glBindTexture(GL_TEXTURE_2D, from);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE,
                        pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
        glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, from, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, to, 0, layer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, size, size, 0, 0, size * scale, size * scale, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    };
    for (int bank = 0; bank < 4; bank++) {
        load(vram[bank], 256, tex256, bank);
        for (int part = 0; part < 4; part++) {
            load(vram[bank] + part * 0x8000, 128, tex128, bank << 2 | part);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(2, fbo.data());
    glDeleteTextures(1, &from);
}

} // namespace DsGl
