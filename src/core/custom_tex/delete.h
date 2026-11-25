// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// Deleting texture packs, the ones the app downloaded and the player's own: the only code that
// deletes a pack's data besides Optimize::RemoveOriginal. No Qt here. The VR menu's Delete
// (citra_qt/frame_mods.cpp, after its dialog) and Download::Downloader::Delete use it.
//
// What may go (Check): a pack the scan lists now (ListPacks, by its id), which is
//   Library  a folder right inside <load>/texture-packs/<TITLEID>
//   Load     the folder <load>/textures/<TITLEID> itself, or one named <TITLEID>-... beside it,
//            when that is a real folder
// Never a root, a folder that holds one, or a folder that holds another pack of the game. Paths
// are compared after every link on the way is resolved, so nothing is reached through a link that
// leads out of the load folder. An entry of the library that is a link itself is unlinked; what it
// points at stays.
//
// The pack goes off in the game's list first (layers.h), so the link moves to the next pack. A
// real folder at <load>/textures/<TITLEID> is moved into the library by that and deleted there.
//
// Threading: blocking; one call at a time for a title, and none beside Activate / Layers calls.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
#include "core/custom_tex/packs.h"

namespace CustomTex::Deletion {

enum class Place {
    None,    ///< may not be deleted
    Library, ///< <load>/texture-packs/<TITLEID>/<folder>
    Load,    ///< <load>/textures/<TITLEID>
};

struct Target {
    std::string id;    ///< Pack::id
    std::string path;  ///< what goes: the folder, or the link
    bool link = false; ///< a link in the library: unlinked, its target stays
    Place place = Place::None;
    std::string error; ///< why not, for the log ("" it may go)
    bool Ok() const {
        return place != Place::None && error.empty();
    }
};

/// Whether the pack `id` of `packs` (ListPacks of this title, now) may be deleted. Changes nothing.
Target Check(const Paths& paths, u64 title_id, const std::vector<Pack>& packs,
             const std::string& id);

/// The other games that have the pack (or a folder in it) on now.
std::vector<u64> OtherGames(const Paths& paths, u64 title_id, const std::string& id);

struct Options {
    bool shared = false; ///< the player was told that another game uses it (else it is refused)
};

struct Result {
    bool ok = false;
    std::string error;    ///< set when !ok, for the log
    bool changed = false; ///< the game's list or files changed (also when !ok)
};

/// Deletes the packs `ids` of the title (an Optimize for Frame pair: both): lists and checks them
/// all first, and nothing changes when one may not go or is being converted. Then all are turned
/// off (in other games that use them too), then deleted in this order; it stops at the first that
/// cannot be deleted, which stays in the list, off.
Result Delete(const Paths& paths, u64 title_id, const std::vector<std::string>& ids,
              const Options& options = {});

} // namespace CustomTex::Deletion
