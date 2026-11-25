// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR updatecheck: the pure parts of the update check. Versions are semver without a
// pre-release part, `X.Y.Z` with an optional leading `v`; a build may also be a `git describe
// --long` of one (`v1.3.0-118-g204b92480`, 118 commits after the tag). Anything else (a dirty tree,
// a pre-release tag, a bare commit id) does not parse, and what does not parse never alerts.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace UpdateChecker {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::uint32_t ahead = 0; ///< commits after the tag (describe form); 0 for a release
};

/// A release tag: `X.Y.Z` or `vX.Y.Z`, nothing after it.
std::optional<Version> ParseTag(std::string_view tag);

/// A build's version: a release tag, or `git describe --long` of one (`vX.Y.Z-N-g<hex>`). Not
/// `-dirty`, not a pre-release, not a bare commit id.
std::optional<Version> ParseBuild(std::string_view build);

/// The first of `fullname` (the build's tag) and `describe` (git describe) that parses, as it was
/// written; nullopt when neither does.
std::optional<std::string> PickBuildVersion(std::string_view fullname, std::string_view describe);

/// True only when `release_tag` parses, `build` parses, and the tag is higher than the build's base
/// version (a build 118 commits after 1.3.0 is not behind release 1.3.0).
bool IsUpdateAvailable(std::string_view build, std::string_view release_tag);

/// The `tag_name` of a GitHub "latest release" reply, when it is a published (not draft, not
/// pre-release) release with a tag that parses; else nullopt.
std::optional<std::string> TagFromReleaseJson(std::string_view body);

/// The release's page in this project's repository, for a tag that parses.
std::string ReleasePageUrl(std::string_view tag);

} // namespace UpdateChecker
