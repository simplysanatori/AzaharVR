// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <tuple>
#include <json.hpp>
#include "citra_qt/update_version.h"

namespace UpdateChecker {

namespace {

// A decimal number of up to 9 digits with no leading zero (except "0" itself); advances `s`.
std::optional<std::uint32_t> Number(std::string_view& s) {
    std::size_t n = 0;
    while (n < s.size() && s[n] >= '0' && s[n] <= '9') {
        ++n;
    }
    if (n == 0 || n > 9 || (n > 1 && s[0] == '0')) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < n; ++i) {
        value = value * 10 + static_cast<std::uint32_t>(s[i] - '0');
    }
    s.remove_prefix(n);
    return value;
}

bool Eat(std::string_view& s, char c) {
    if (s.empty() || s.front() != c) {
        return false;
    }
    s.remove_prefix(1);
    return true;
}

// `[v]X.Y.Z`; `s` is left at what follows.
std::optional<Version> Core(std::string_view& s) {
    Version v;
    Eat(s, 'v');
    const auto major = Number(s);
    if (!major || !Eat(s, '.')) {
        return std::nullopt;
    }
    const auto minor = Number(s);
    if (!minor || !Eat(s, '.')) {
        return std::nullopt;
    }
    const auto patch = Number(s);
    if (!patch) {
        return std::nullopt;
    }
    v.major = *major;
    v.minor = *minor;
    v.patch = *patch;
    return v;
}

} // namespace

std::optional<Version> ParseTag(std::string_view tag) {
    auto v = Core(tag);
    if (!v || !tag.empty()) {
        return std::nullopt;
    }
    return v;
}

std::optional<Version> ParseBuild(std::string_view build) {
    auto v = Core(build);
    if (!v) {
        return std::nullopt;
    }
    if (build.empty()) {
        return v;
    }
    // -N-g<hex>
    if (!Eat(build, '-')) {
        return std::nullopt;
    }
    const auto ahead = Number(build);
    if (!ahead || !Eat(build, '-') || !Eat(build, 'g') || build.empty()) {
        return std::nullopt;
    }
    for (const char c : build) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return std::nullopt; // "-dirty" and anything else
        }
    }
    v->ahead = *ahead;
    return v;
}

std::optional<std::string> PickBuildVersion(std::string_view fullname, std::string_view describe) {
    if (ParseBuild(fullname)) {
        return std::string(fullname);
    }
    if (ParseBuild(describe)) {
        return std::string(describe);
    }
    return std::nullopt;
}

bool IsUpdateAvailable(std::string_view build, std::string_view release_tag) {
    const auto current = ParseBuild(build);
    const auto latest = ParseTag(release_tag);
    if (!current || !latest) {
        return false;
    }
    return std::tie(latest->major, latest->minor, latest->patch) >
           std::tie(current->major, current->minor, current->patch);
}

std::optional<std::string> TagFromReleaseJson(std::string_view body) {
    try {
        const auto json = nlohmann::json::parse(body.begin(), body.end());
        if (!json.is_object()) {
            return std::nullopt;
        }
        for (const char* flag : {"draft", "prerelease"}) {
            if (json.contains(flag) && !(json[flag].is_boolean() && !json[flag].get<bool>())) {
                return std::nullopt;
            }
        }
        if (!json.contains("tag_name") || !json["tag_name"].is_string()) {
            return std::nullopt;
        }
        std::string tag = json["tag_name"].get<std::string>();
        if (!ParseTag(tag)) {
            return std::nullopt;
        }
        return tag;
    } catch (...) {
        return std::nullopt;
    }
}

std::string ReleasePageUrl(std::string_view tag) {
    // The tag parsed, so it holds only digits, dots and a leading "v".
    if (!ParseTag(tag)) {
        return "https://github.com/simplysanatori/AzaharVR/releases/latest";
    }
    return "https://github.com/simplysanatori/AzaharVR/releases/tag/" + std::string(tag);
}

bool IsReleasePageUrl(std::string_view url) {
    constexpr std::string_view base = "https://github.com/simplysanatori/AzaharVR/releases/";
    if (url.substr(0, base.size()) != base) {
        return false;
    }
    url.remove_prefix(base.size());
    if (url == "latest") {
        return true;
    }
    constexpr std::string_view tag_part = "tag/";
    return url.substr(0, tag_part.size()) == tag_part &&
           ParseTag(url.substr(tag_part.size())).has_value();
}

} // namespace UpdateChecker
