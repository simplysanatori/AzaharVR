// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharFrame cpphelpers: the command-line modes of the azahar binary that replace the Python
// helpers the package used to ship (no window, no emulation, exit when done):
//   --frame-emerald-build ...        Emerald 3DS's data pack from the player's ROM
//   --frame-cef 'JS' | --art APPID DIR [--dry-run] | --screen ...   the command line steam-cef.py had (removed)
//   --frame-steam-art APPID DIR [--dry-run]                          Steam library art and icon
//   --frame-steam-shortcut find|add|update|remove|remove-named ...   the library entry
//   --frame-weapon-models GAME [OUTDIR]                              AzaharFrame kiuguns: Kid Icarus:
//                                    Uprising's weapon models from the player's own game

#pragma once

#include <optional>
#include <string>

namespace FrameTools {

/// The exit code when argv holds one of the modes (it was run), else nullopt.
std::optional<int> Run(int argc, char* argv[]);

#ifdef AZAHARVR_PC // AzaharVR
/// Makes OUT_DIR's frame_controller_{left,right}.model from the player's own SteamVR install when
/// either is missing (on a PC no installer does). Logs what it did; a failure only leaves the beams.
void EnsureControllerModels(const std::string& out_dir);
#endif

} // namespace FrameTools
