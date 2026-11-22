// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu133: where the player's model node is in his actor (KIU_HOOKS.md, "Pit's model
// node, found"). Pure: memory through KiuTurn::Reader; tests in tests/frame_xr/kiu_model.cpp.

#pragma once

#include <cstdint>
#include "frame_xr/input/kiu_turn.h"

namespace FrameXr::Input::KiuModel {

using KiuTurn::Reader;

/// The actor holds a small model holder inline: {class word, the actor, a flag word, the model
/// node}. Where it sits differs between battles (V on the desktop: actor + 0x340 in Chapter 1's
/// air battle and the owner's Chapter 2 land state, + 0x338 in the owner's air state (kiu133:
/// over a city), + 0x3C0 in the Practice Range and Chapter 1's land section), so the model
/// node is found by that shape, not one offset: the word `kHolderModel` after a word holding
/// the actor itself, pointing at an object of the model class.
inline constexpr uint32_t kHolderActor = 4;
inline constexpr uint32_t kHolderModel = 0xC;
/// The actor's own link to it: [actor + kActorLink] is the holder's start minus kLinkToHolder
/// (its +0x1C the actor, its +0x24 the model). Inline in every European state read (it then
/// equals the shape below), on the heap in one USA Rev 1 state (the kiuusa lane's reading);
/// tried first.
inline constexpr uint32_t kActorLink = 0x1E4;
inline constexpr uint32_t kLinkActor = 0x1C;
inline constexpr uint32_t kLinkModel = 0x24;
/// The actor offsets of the model pointer looked at, the first that fits is taken.
inline constexpr uint32_t kFirst = 0x300;
inline constexpr uint32_t kLast = 0x480;
/// kiu131's single offset, tried first.
inline constexpr uint32_t kUsual = 0x34C;

/// The model node at actor + `off` if the holder's shape fits there, else 0.
inline uint32_t ModelAt(const Reader& m, uint32_t actor, uint32_t off, uint32_t model_vtable) {
    uint32_t back = 0, model = 0, vt = 0;
    if (off < kHolderModel - kHolderActor ||
        !m.Read32(actor + off - (kHolderModel - kHolderActor), back) || back != actor ||
        !m.Read32(actor + off, model) || model == 0 || (model & 3u) != 0 ||
        !m.Read32(model, vt) || vt != model_vtable) {
        return 0;
    }
    return model;
}

struct Found {
    uint32_t off = 0;   ///< actor offset of the model pointer (0: none)
    uint32_t model = 0; ///< the model node
};

/// The model node through the actor's link (kActorLink), or 0.
inline uint32_t ModelByLink(const Reader& m, uint32_t actor, uint32_t model_vtable) {
    uint32_t link = 0, back = 0, model = 0, vt = 0;
    if (!m.Read32(actor + kActorLink, link) || link == 0 || (link & 3u) != 0 ||
        !m.Read32(link + kLinkActor, back) || back != actor ||
        !m.Read32(link + kLinkModel, model) || model == 0 || (model & 3u) != 0 ||
        !m.Read32(model, vt) || vt != model_vtable) {
        return 0;
    }
    return model;
}

/// The offset FindModel reports for a model found through the link (not an actor offset).
inline constexpr uint32_t kByLink = 0xFFFFFFFFu;

/// The player's model node: through the actor's link first; then `hint` (the offset found for
/// this actor before), the usual one, and every word from kFirst to kLast.
inline Found FindModel(const Reader& m, uint32_t actor, uint32_t model_vtable, uint32_t hint) {
    if (const uint32_t model = ModelByLink(m, actor, model_vtable)) {
        return {kByLink, model};
    }
    for (const uint32_t off : {hint, kUsual}) {
        if (off != 0 && off != kByLink) {
            if (const uint32_t model = ModelAt(m, actor, off, model_vtable)) {
                return {off, model};
            }
        }
    }
    for (uint32_t off = kFirst; off <= kLast; off += 4) {
        if (const uint32_t model = ModelAt(m, actor, off, model_vtable)) {
            return {off, model};
        }
    }
    return {};
}

} // namespace FrameXr::Input::KiuModel
