// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: making a hand file (pit_model.h) from a model the player already has: a
// zip holding a COLLADA model (pit.dae) and its PNG images. The player supplies the zip; nothing
// is fetched from anywhere, and nothing of such a model is in this app. The left arm's hand,
// sleeve and gold are taken by the names such a model gives them (hand_import.cpp lists them),
// put in arm space at the size drawn in the player's hand, their images packed into one pair.
//
// The zip is not trusted. It is read from memory only (nothing is unpacked to disk): the
// members' count, sizes, total and packing ratio are capped, names that are absolute or step
// out with ".." and names that appear twice are refused, only stored and deflated members are
// read, and every checksum is checked. The model file is read by a small strict reader that
// refuses document types, entities and anything else it does not know, with limits on depth
// and count; every index in it is checked against what it points into; images are decoded
// with size limits. No size taken from the file is used before it is checked, garbage is
// refused in time proportional to its length, and what comes out is checked once more by
// pit_model.h's own reader before it is handed back. No OpenXR, Vulkan or Qt: it can run off
// the main thread, and asks `progress` now and then whether to go on.
//
// What it says (each a whole sentence for a menu to show; ImportHand returns "" when done):
//   progress  "Reading the zip." "Reading the model." "Building the hand."
//             "Reading the pictures." "Saving the hand."
//   errors    "Stopped."
//             "This is not a zip file."
//             "The zip is too big."
//             "The zip is damaged."
//             "The zip has too many files."
//             "The zip has a file with an unsafe name."
//             "The zip has the same file twice."
//             "The zip has no model in it (pit.dae)."
//             "The zip has more than one model in it (pit.dae)."
//             "The zip is missing a picture: <name>."
//             "A file in the zip is too big: <name>."
//             "A file in the zip is packed in a way this cannot read: <name>."
//             "A file in the zip is damaged: <name>."
//             "The model file is not well formed."
//             "The model file uses something this refuses (a document type or an entity)."
//             "The model file is too big or too deep."
//             "The model has no left hand."
//             "The model is missing a part of the left arm: <name>."
//             "The model is missing a joint: <name>."
//             "The model's numbers do not add up."
//             "A picture could not be read: <name>."
//             "A picture is too big: <name>."
//             "The hand that came out was not right."
//             "The zip could not be read."            (ImportHandFile)
//             "The hand could not be saved."          (ImportHandFile)

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FrameXr::Vis {

/// The most a zip may be and hold.
inline constexpr uint64_t kHandZipMaxBytes = 512ull << 20;
inline constexpr uint32_t kHandZipMaxMembers = 4096;
inline constexpr uint64_t kHandZipMaxMemberBytes = 64ull << 20; ///< one member unpacked
inline constexpr uint64_t kHandZipMaxUnpacked = 256ull << 20;   ///< all that is unpacked
inline constexpr uint32_t kHandZipMaxRatio = 400;               ///< unpacked over packed
inline constexpr uint32_t kHandImageMaxSide = 4096;
/// The hand's length as drawn (the wrist to the middle finger's tip), metres: a hand of this
/// build closes round the Steam Frame controller's handle with all four fingers on it.
inline constexpr float kHandImportLength = 0.155f;

/// Told what is being done and how far along the whole it is (0 to 1); returns false to stop.
using HandImportProgress = std::function<bool(std::string_view doing, float done)>;

/// The hand file's bytes from a zip's. Returns "" and fills `out`, or a sentence saying what
/// was wrong (and `out` empty).
std::string ImportHand(std::span<const uint8_t> zip, std::vector<uint8_t>& out,
                       const HandImportProgress& progress = {});

/// The same from a zip's path, saved as `<user_directory>/kiu_hand/pit_hand.bin` (written
/// beside it first, then renamed over it, so a hand already there is never left half
/// written). Returns "" or a sentence.
std::string ImportHandFile(const std::string& zip_path, const std::string& user_directory,
                           const HandImportProgress& progress = {});

/// The zips waiting in `<user_directory>/kiu_hand/` (full paths, by name), for a menu to list.
std::vector<std::string> ListHandZips(const std::string& user_directory);

} // namespace FrameXr::Vis
