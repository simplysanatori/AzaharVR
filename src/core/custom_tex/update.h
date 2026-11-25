// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// Updates of downloaded texture packs: whether the catalog has a newer version than the installed
// one, and putting the new one in the old one's place. No Qt here, and no network: the catalog is
// the one in hand (download.h), the download is Downloader::Start's, which the player starts.
//
// The new version is unpacked beside the old one, as a hidden folder (StagedPath) that is no pack
// yet; its download marker, written last, says it is complete. A failed or cancelled download
// leaves the old pack as it was. Apply, called while no game reads the pack (at once, when the game
// stops, or before the next one starts), puts it in place:
//   <folder> -> .<folder>.azaharvr-old, .<folder>.azaharvr-update -> <folder>
// The folder keeps its name, so the pack keeps its place in the game's list and its on and off; its
// Customize choices go over. If either rename fails, the old pack is back (or never went) and the
// new one waits. The old one is deleted by Finish, the update's last step, after the new one was
// checked.
// The same two functions settle an update that a crash cut off. Where it is not certain which
// folder is the good one, nothing is renamed or deleted.
//
// An optimized copy (optimize.h) of the old version is then stale (Optimize::IsStale): the game's
// list gets the new original in its place, and Optimize for Frame makes the copy again, when the
// player asks for it.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
#include "core/custom_tex/packs.h"

namespace CustomTex::Download {
struct Entry;
struct Marker;
} // namespace CustomTex::Download

namespace CustomTex::Update {

/// The catalog's entry is a newer version than the one the marker is of. Only when that is
/// certain:
/// - both know the file's SHA-256, and it differs, and the entry is not dated before the marker's
///   entry; or
/// - the pack's date is the catalog's (Google Drive, https), and the entry's is later; or
/// - the date is the source's own (GameBanana, GitHub): the entry's date is later than the one the
///   catalog had at the download, and its size differs.
/// A date without a day is neither before nor after one in the same month, and is taken as before
/// when the SHA-256 differs. Never for Install from Zip, or when the files' SHA-256 (or the size
/// and the Drive file) are the same.
bool Newer(const Download::Marker& installed, const Download::Entry& entry);
/// Newer than every installed copy (none: false).
bool NewerThanAll(const std::vector<Download::Marker>& installed, const Download::Entry& entry);

/// "<library>/.<folder>.azaharvr-update" and "...-old" for a pack's folder.
std::string StagedPath(const std::string& pack);
std::string OldPath(const std::string& pack);
/// The name (".<folder>.azaharvr-update") is a staged update's.
bool IsStagedName(const std::string& name);

/// The folder in the title's library (its name) that an update of the catalog's pack replaces: the
/// downloaded original (the one that is on, when there are more), or, when only its optimized copy
/// is there, the name the original had. "" none: the library or the folder is reached through a
/// link, or is not the app's download.
std::string TargetFolder(const Paths& paths, u64 title_id, const std::string& catalog_id);

struct Applied {
    std::vector<std::string> packs; ///< ids of the packs that were replaced
    std::vector<std::string> stale; ///< ids of the optimized copies that are stale now
    std::string error;              ///< the last thing that failed, for the log
};
/// Puts the title's waiting updates (newer than what is there: Newer) in place (renames only; the old ones stay, hidden, until
/// Finish), puts back a pack whose update was cut off before the new one was in place. Not for a
/// pack that is being converted. Call when no game reads the packs; `only`: this folder's.
Applied Apply(const Paths& paths, u64 title_id, const std::string& only = {});
/// Deletes the old version of each update that is in place, when both are packs of the same download
/// and the one in place is certainly the newer one (Newer).
void Finish(const Paths& paths, u64 title_id);
/// The titles with an update to Apply or to Finish.
std::vector<u64> Waiting(const Paths& paths);
/// Removes a deleted pack's own hidden folders (delete.h): a waiting update of it, an old version.
void RemoveLeftovers(const std::string& pack);

} // namespace CustomTex::Update
