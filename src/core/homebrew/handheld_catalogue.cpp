// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR homebrew: see handheld_catalogue.h.

#include "core/homebrew/handheld_catalogue.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <json.hpp>
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/homebrew/net.h"

namespace Homebrew::Handheld {

namespace {

// The paths here are UTF-8. On Windows std::filesystem takes a std::string for the ANSI code page,
// so they go through these on the way in and out (as frame_cores/utf8_path.h); elsewhere nothing.
#ifdef _WIN32 // AzaharVR
std::filesystem::path FsPath(const std::string& utf8) {
    return std::filesystem::path(Common::UTF8ToUTF16W(utf8));
}
std::string FsUtf8(const std::filesystem::path& path) {
    return Common::UTF16ToUTF8(path.generic_wstring());
}
#else
std::filesystem::path FsPath(const std::string& utf8) {
    return std::filesystem::path(utf8);
}
std::string FsUtf8(const std::filesystem::path& path) {
    return path.string();
}
#endif

bool IsLowerHex(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

/// Well-formed UTF-8 (no overlong forms, no surrogates, nothing past U+10FFFF).
bool ValidUtf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        u32 cp = 0;
        if (c < 0x80) {
            i++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            n = 1;
            cp = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            n = 2;
            cp = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 3;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + n >= s.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= n; k++) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += n + 1;
    }
    return true;
}

/// Text a row shows (title, author, licence): valid UTF-8, no control characters, no line or
/// paragraph separators, no bidirectional controls (they could make a row read otherwise than it is),
/// not empty, not only spaces, no space at either end.
bool ValidText(std::string_view s) {
    if (s.empty() || s.size() > kMaxTextLength || !ValidUtf8(s) || s.front() == ' ' ||
        s.back() == ' ') {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); i++) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) < 0xA0) {
            return false; // C1 controls
        }
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80) {
            const auto d = static_cast<unsigned char>(s[i + 2]);
            // U+200B..U+200F (zero width, direction marks), U+2028..U+202E (separators, embeddings)
            if ((d >= 0x8B && d <= 0x8F) || (d >= 0xA8 && d <= 0xAE)) {
                return false;
            }
        }
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x81) {
            const auto d = static_cast<unsigned char>(s[i + 2]);
            if (d >= 0xA6 && d <= 0xA9) { // U+2066..U+2069 isolates
                return false;
            }
        }
    }
    return std::any_of(s.begin(), s.end(), [](char c) { return c != ' '; });
}

std::size_t JsonDepth(std::string_view text) {
    std::size_t depth = 0, deepest = 0;
    bool in_string = false, escape = false;
    for (const char c : text) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if ((c == ']' || c == '}') && depth > 0) {
            depth--;
        }
    }
    return deepest;
}

bool HasExtension(std::string_view name, std::string_view ext) {
    return name.size() > ext.size() + 1 && name[name.size() - ext.size() - 1] == '.' &&
           std::equal(ext.begin(), ext.end(), name.end() - static_cast<std::ptrdiff_t>(ext.size()),
                      [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
}

std::string LastPathPart(const std::string& url) {
    std::string path = url.substr(url.find('/', std::string_view("https://").size()));
    path = path.substr(0, path.find_first_of("?#"));
    return path.substr(path.rfind('/') + 1);
}

/// A string member, or "" when it is missing or not a string.
bool GetString(const nlohmann::json& obj, const char* key, std::string* out) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return false;
    }
    *out = it->get<std::string>();
    return true;
}

std::string ParseEntry(const nlohmann::json& j, Entry* e) {
    if (!j.is_object()) {
        return "not an object";
    }
    std::string system;
    if (!GetString(j, "slug", &e->slug) || !ValidSlug(e->slug)) {
        return "bad slug";
    }
    const auto bad = [&](const char* what) { return fmt::format("{}: bad {}", e->slug, what); };
    if (!GetString(j, "title", &e->title) || !ValidText(e->title)) {
        return bad("title");
    }
    if (!GetString(j, "author", &e->author) || !ValidText(e->author)) {
        return bad("author");
    }
    if (!GetString(j, "licence", &e->licence) || !ValidText(e->licence)) {
        return bad("licence");
    }
    if (!GetString(j, "system", &system) || !ParseSystem(system, &e->system)) {
        return bad("system");
    }
    const auto size = j.find("size");
    if (size == j.end() || !size->is_number_unsigned() || size->get<u64>() == 0 ||
        size->get<u64>() > kMaxFileSize) {
        return bad("size");
    }
    e->size = size->get<u64>();
    if (!GetString(j, "sha256", &e->sha256)) {
        return bad("sha256");
    }
    std::transform(e->sha256.begin(), e->sha256.end(), e->sha256.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    if (e->sha256.size() != 64 || !IsLowerHex(e->sha256)) {
        return bad("sha256");
    }
    if (!GetString(j, "page", &e->page) || !ParseHttpsUrl(e->page)) {
        return bad("page");
    }
    std::string host;
    if (!GetString(j, "url", &e->url) || !ParseHttpsUrl(e->url, &host)) {
        return bad("url");
    }
    if (!HostAllowed(host)) {
        return bad("url (host not allowed)");
    }
    if (const auto it = j.find("filename"); it != j.end()) {
        if (!it->is_string()) {
            return bad("filename");
        }
        e->filename = it->get<std::string>();
    } else {
        e->filename = LastPathPart(e->url);
    }
    if (!ValidFileName(e->filename, e->system)) {
        return bad("filename");
    }
    return {};
}

} // namespace

bool HostAllowed(std::string_view host) {
    // The author's or the index's own host the owner has chosen; a list file cannot add to this.
    static constexpr std::array<std::string_view, 5> allowed{
        "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com",
        "raw.githubusercontent.com", "hh3.gbdev.io"};
    return std::find(allowed.begin(), allowed.end(), host) != allowed.end();
}

bool ValidSlug(std::string_view slug) {
    if (slug.empty() || slug.size() > 64 || slug.front() == '-' || slug.front() == '_') {
        return false;
    }
    return std::all_of(slug.begin(), slug.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string_view SystemName(System system) {
    switch (system) {
    case System::DS:
        return "ds";
    case System::GBA:
        return "gba";
    case System::GB:
        return "gb";
    case System::GBC:
        return "gbc";
    }
    return "";
}

bool ParseSystem(std::string_view name, System* system) {
    for (const System s : {System::DS, System::GBA, System::GB, System::GBC}) {
        if (name == SystemName(s)) {
            *system = s;
            return true;
        }
    }
    return false;
}

bool ValidFileName(std::string_view name, System system) {
    if (name.empty() || name.size() > 100 || name.front() == '.' || name.front() == '-' ||
        name.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '-' || c == '_' || c == '+' || c == '(' || c == ')' ||
                        c == ' ';
        if (!ok) { // also refuses / \ : NUL, controls, non-ASCII, % and the like
            return false;
        }
    }
    if (name.back() == ' ' || name.back() == '.') {
        return false;
    }
    switch (system) {
    case System::DS:
        return HasExtension(name, "nds") || HasExtension(name, "srl");
    case System::GBA:
        return HasExtension(name, "gba");
    case System::GB:
    case System::GBC:
        return HasExtension(name, "gb") || HasExtension(name, "gbc");
    }
    return false;
}

bool ParseHttpsUrl(std::string_view url, std::string* host_out) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || url.size() > kMaxUrlLength ||
        url.substr(0, scheme.size()) != scheme) {
        return false;
    }
    for (const char c : url) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F || c == '\\' || c == '"' || c == '<' || c == '>' || c == '`' ||
            c == '{' || c == '}' || c == '|' || c == '^' || c == '#') {
            return false; // controls, space, non-ASCII, and what a URL may not carry
        }
    }
    const std::string_view rest = url.substr(scheme.size());
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0) {
        return false; // a path is required: a direct file
    }
    const std::string_view host = rest.substr(0, slash);
    // a plain host name: lower-case letters, digits, '-' and '.', no user, no port, no dot-less name
    if (host.front() == '.' || host.back() == '.' || host.find("..") != std::string_view::npos ||
        host.find('.') == std::string_view::npos) {
        return false;
    }
    for (const char c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return false;
        }
    }
    // the path: no "." or ".." segment, in any case or percent-encoded, no encoded slash or backslash
    std::string path(rest.substr(slash));
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    for (const char* bad : {"%2e", "%2f", "%5c", "%00"}) {
        if (lower.find(bad) != std::string::npos) {
            return false;
        }
    }
    const std::string query_cut = path.substr(0, path.find('?'));
    std::size_t at = 1;
    while (at <= query_cut.size()) {
        auto end = query_cut.find('/', at);
        if (end == std::string::npos) {
            end = query_cut.size();
        }
        const std::string seg = query_cut.substr(at, end - at);
        if (seg == ".." || seg == ".") {
            return false;
        }
        at = end + 1;
    }
    if (host_out != nullptr) {
        *host_out = std::string(host);
    }
    return true;
}

Catalogue Parse(std::string_view text) {
    Catalogue out;
    if (text.size() > kMaxCatalogueBytes) {
        out.error = "The list is too large";
        return out;
    }
    if (JsonDepth(text) > kMaxJsonDepth) {
        out.error = "The list is nested too deeply";
        return out;
    }
    nlohmann::json root = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        out.error = "The list is not valid JSON";
        return out;
    }
    const auto schema = root.find("schema");
    if (schema == root.end() || !schema->is_number_integer()) {
        out.error = "The list has no schema number";
        return out;
    }
    if (schema->get<long long>() != kSupportedSchema) {
        out.error = fmt::format("The list's schema {} is not supported (this version reads {})",
                                schema->get<long long>(), kSupportedSchema);
        return out;
    }
    const auto list = root.find("homebrew");
    if (list == root.end() || !list->is_array()) {
        out.error = "The list has no homebrew array";
        return out;
    }
    if (list->size() > kMaxEntries) {
        out.error = "The list has too many entries";
        return out;
    }
    for (const nlohmann::json& j : *list) {
        Entry e;
        const std::string why = ParseEntry(j, &e);
        if (!why.empty()) {
            out.rejected.push_back(why);
        } else if (std::any_of(out.entries.begin(), out.entries.end(),
                               [&](const Entry& x) { return x.slug == e.slug; })) {
            out.rejected.push_back(e.slug + ": duplicate slug");
        } else {
            out.entries.push_back(std::move(e));
        }
    }
    return out;
}

std::string EntryDir(const std::string& root, const Entry& entry) {
    if (root.empty() || !ValidSlug(entry.slug)) {
        return {};
    }
    return FsUtf8(FsPath(root) / entry.slug);
}

std::string EntryPath(const std::string& root, const Entry& entry) {
    const std::string dir = EntryDir(root, entry);
    if (dir.empty() || !ValidFileName(entry.filename, entry.system)) {
        return {};
    }
    return FsUtf8(FsPath(dir) / entry.filename);
}

std::string VerifyFile(const std::string& path, const Entry& entry) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(FsPath(path), ec)) {
        return "The file is missing";
    }
    const auto size = std::filesystem::file_size(FsPath(path), ec);
    if (ec || size != entry.size) {
        return "The file's size is not the one listed";
    }
    if (Homebrew::FileSha256(path) != entry.sha256) {
        return "The file's checksum is not the one listed";
    }
    return {};
}

std::string Install(const std::string& root, const Entry& entry,
                    const std::function<bool(u64, u64)>& progress) {
    const std::string dir = EntryDir(root, entry);
    const std::string dest = EntryPath(root, entry);
    std::string host;
    if (dest.empty() || entry.size == 0 || entry.size > kMaxFileSize ||
        !ParseHttpsUrl(entry.url, &host) || !HostAllowed(host)) {
        return "Download refused: not an allowed address";
    }
    std::error_code ec;
    std::filesystem::create_directories(FsPath(dir), ec);
    if (ec) {
        return "The folder could not be made";
    }
    // Under another name until it is checked: a file at `dest` is always a whole, matching one.
    const std::string temp = dest + ".check";
    FileUtil::Delete(temp);
    std::string error = Homebrew::Download(entry.url, temp, progress, entry.size, &HostAllowed);
    if (error.empty()) {
        error = VerifyFile(temp, entry);
    }
    if (!error.empty()) {
        FileUtil::Delete(temp);
        return error;
    }
    FileUtil::Delete(dest);
    if (!FileUtil::Rename(temp, dest)) {
        FileUtil::Delete(temp);
        return "The download could not be saved";
    }
    return {};
}

std::string Remove(const std::string& root, const Entry& entry) {
    const std::string dir = EntryDir(root, entry);
    if (dir.empty()) {
        return "Nothing to remove";
    }
    std::error_code ec;
    if (std::filesystem::is_symlink(FsPath(dir), ec)) {
        return "Nothing to remove"; // never follow a link out of the folder
    }
    std::filesystem::remove_all(FsPath(dir), ec);
    return ec ? "The files could not be removed" : std::string{};
}

} // namespace Homebrew::Handheld
