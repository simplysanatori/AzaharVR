// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/rom_info.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>

#include <fmt/format.h>

#include "common/string_util.h"

extern "C" {
#include <mgba-util/vfs.h>
}

namespace FrameCores {
namespace {

std::string ToLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

Common::AltCore::CoreKind KindFromExtension(std::string_view filename) {
    const auto dot = filename.rfind('.');
    if (dot == std::string_view::npos) {
        return Common::AltCore::CoreKind::N3DS;
    }
    const std::string ext = ToLowerAscii(filename.substr(dot));
    if (ext == ".nds") {
        return Common::AltCore::CoreKind::NDS;
    }
    if (ext == ".dsi") {
        return Common::AltCore::CoreKind::DSi;
    }
    if (ext == ".gba") {
        return Common::AltCore::CoreKind::GBA;
    }
    if (ext == ".gb" || ext == ".gbc") {
        return Common::AltCore::CoreKind::GB;
    }
    return Common::AltCore::CoreKind::N3DS;
}

bool IsZipExtension(std::string_view path) {
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    return ToLowerAscii(path.substr(dot)) == ".zip";
}

uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string CleanAsciiField(const uint8_t* data, size_t max_len) {
    std::string out;
    out.reserve(max_len);
    for (size_t i = 0; i < max_len; ++i) {
        const uint8_t c = data[i];
        if (c == 0) {
            break;
        }
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') {
        ++start;
    }
    return out.substr(start);
}

std::string DecodeUtf16LETitle(const uint8_t* data, size_t code_units) {
    std::string out;
    out.reserve(code_units);
    bool prev_space = false;
    for (size_t i = 0; i < code_units; ++i) {
        const uint16_t ch = ReadLE16(data + i * 2);
        if (ch == 0) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            if (!out.empty() && !prev_space) {
                out.append(" - ");
                prev_space = true;
            }
            continue;
        }
        prev_space = (ch == ' ');
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '-')) {
        out.pop_back();
    }
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') {
        ++start;
    }
    return out.substr(start);
}

struct ZipEntryInfo {
    std::string name;
    uint16_t compression = 0;
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
    uint32_t local_header_offset = 0;
    Common::AltCore::CoreKind kind = Common::AltCore::CoreKind::N3DS;
};

/// Fast ZIP directory scan: reads End of Central Directory (EOCD) first,
/// falling back to local file headers if needed.
std::optional<ZipEntryInfo> FindRomEntryInZip(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return std::nullopt;
    }
    const auto file_size = static_cast<uint64_t>(f.tellg());
    if (file_size < 22) {
        return std::nullopt;
    }

    // Search last up to 65557 bytes for EOCD signature 0x06054b50
    const size_t tail_len = static_cast<size_t>(std::min<uint64_t>(file_size, 65557));
    std::vector<uint8_t> tail(tail_len);
    f.seekg(static_cast<std::streamoff>(file_size - tail_len), std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail_len))) {
        return std::nullopt;
    }

    size_t eocd_pos = tail_len;
    for (size_t i = tail_len - 22 + 1; i > 0; --i) {
        const size_t idx = i - 1;
        if (tail[idx] == 0x50 && tail[idx + 1] == 0x4B && tail[idx + 2] == 0x05 &&
            tail[idx + 3] == 0x06) {
            eocd_pos = idx;
            break;
        }
    }

    if (eocd_pos < tail_len) {
        const uint16_t total_entries = ReadLE16(tail.data() + eocd_pos + 10);
        const uint32_t cd_size = ReadLE32(tail.data() + eocd_pos + 12);
        const uint32_t cd_offset = ReadLE32(tail.data() + eocd_pos + 16);

        if (static_cast<uint64_t>(cd_offset) + cd_size <= file_size && cd_size <= 16u * 1024u * 1024u) {
            std::vector<uint8_t> cd(cd_size);
            f.seekg(static_cast<std::streamoff>(cd_offset), std::ios::beg);
            if (f.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd_size))) {
                size_t pos = 0;
                for (uint16_t e = 0; e < total_entries && pos + 46 <= cd.size(); ++e) {
                    if (ReadLE32(cd.data() + pos) != 0x02014B50u) {
                        break;
                    }
                    const uint16_t method = ReadLE16(cd.data() + pos + 10);
                    const uint32_t comp_sz = ReadLE32(cd.data() + pos + 20);
                    const uint32_t uncomp_sz = ReadLE32(cd.data() + pos + 24);
                    const uint16_t name_len = ReadLE16(cd.data() + pos + 28);
                    const uint16_t extra_len = ReadLE16(cd.data() + pos + 30);
                    const uint16_t comment_len = ReadLE16(cd.data() + pos + 32);
                    const uint32_t local_off = ReadLE32(cd.data() + pos + 42);
                    if (pos + 46 + name_len > cd.size()) {
                        break;
                    }
                    std::string name(reinterpret_cast<const char*>(cd.data() + pos + 46), name_len);
                    const auto kind = KindFromExtension(name);
                    if (kind != Common::AltCore::CoreKind::N3DS) {
                        return ZipEntryInfo{std::move(name), method, comp_sz, uncomp_sz, local_off, kind};
                    }
                    pos += 46u + name_len + extra_len + comment_len;
                }
            }
        }
    }

    // Fallback: inspect first local file header
    f.seekg(0, std::ios::beg);
    uint8_t lfh[30]{};
    if (!f.read(reinterpret_cast<char*>(lfh), sizeof(lfh)) || ReadLE32(lfh) != 0x04034B50u) {
        return std::nullopt;
    }
    const uint16_t method = ReadLE16(lfh + 8);
    const uint32_t comp_sz = ReadLE32(lfh + 18);
    const uint32_t uncomp_sz = ReadLE32(lfh + 22);
    const uint16_t name_len = ReadLE16(lfh + 26);
    if (name_len == 0 || name_len > 1024) {
        return std::nullopt;
    }
    std::string name(name_len, '\0');
    if (!f.read(name.data(), name_len)) {
        return std::nullopt;
    }
    const auto kind = KindFromExtension(name);
    if (kind == Common::AltCore::CoreKind::N3DS) {
        return std::nullopt;
    }
    return ZipEntryInfo{std::move(name), method, comp_sz, uncomp_sz, 0, kind};
}

/// Decodes a 32x32 4bpp NDS banner icon + 16-colour RGB555 palette into RGBA8888.
std::vector<uint8_t> DecodeNdsBannerIcon(const uint8_t* tile_data, const uint8_t* pal_data) {
    std::array<std::array<uint8_t, 4>, 16> palette{};
    palette[0] = {0, 0, 0, 0}; // Index 0 is transparent on DS
    for (int i = 1; i < 16; ++i) {
        const uint16_t rgb555 = ReadLE16(pal_data + i * 2);
        const uint8_t r5 = static_cast<uint8_t>(rgb555 & 0x1F);
        const uint8_t g5 = static_cast<uint8_t>((rgb555 >> 5) & 0x1F);
        const uint8_t b5 = static_cast<uint8_t>((rgb555 >> 10) & 0x1F);
        palette[i] = {
            static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<uint8_t>((g5 << 3) | (g5 >> 2)),
            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)),
            0xFF,
        };
    }

    std::vector<uint8_t> rgba(32u * 32u * 4u, 0);
    for (int ty = 0; ty < 4; ++ty) {
        for (int tx = 0; tx < 4; ++tx) {
            const uint8_t* tile = tile_data + (ty * 4 + tx) * 32;
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; px += 2) {
                    const uint8_t packed = tile[py * 4 + (px / 2)];
                    const uint8_t lo = packed & 0x0F;
                    const uint8_t hi = (packed >> 4) & 0x0F;

                    const int gx0 = tx * 8 + px;
                    const int gy = ty * 8 + py;
                    const size_t off0 = static_cast<size_t>((gy * 32 + gx0) * 4);
                    const size_t off1 = off0 + 4;
                    std::memcpy(rgba.data() + off0, palette[lo].data(), 4);
                    std::memcpy(rgba.data() + off1, palette[hi].data(), 4);
                }
            }
        }
    }
    return rgba;
}

/// Builds a crisp deterministic 32x32 RGBA8 cartridge badge tile for GBA / GB / GBC / fallback DS.
std::vector<uint8_t> MakeDeterministicTileIcon(Common::AltCore::CoreKind kind, uint32_t seed,
                                               bool is_gbc = false) {
    // Deterministic hue-like accent picked from seed
    static constexpr std::array<std::array<uint8_t, 3>, 8> kAccents = {{
        {0x4E, 0x7A, 0xE8}, // indigo blue
        {0x38, 0xA8, 0x8F}, // teal
        {0xD9, 0x5B, 0x5B}, // coral red
        {0xE0, 0x9B, 0x3D}, // amber
        {0x8E, 0x5C, 0xD6}, // violet
        {0x4C, 0xAF, 0x62}, // emerald
        {0xD6, 0x5A, 0x9A}, // rose
        {0x3D, 0x9C, 0xC9}, // cerulean
    }};
    const auto& accent = kAccents[seed % kAccents.size()];

    // Shell colour per system
    std::array<uint8_t, 3> shell{0x34, 0x38, 0x46};
    std::array<uint8_t, 3> border{0x1C, 0x1F, 0x28};
    if (kind == Common::AltCore::CoreKind::GBA) {
        shell = {0x3A, 0x3F, 0x54};
    } else if (kind == Common::AltCore::CoreKind::GB) {
        shell = is_gbc ? std::array<uint8_t, 3>{0x2E, 0x3A, 0x52}
                       : std::array<uint8_t, 3>{0x7D, 0x87, 0x70};
    } else {
        shell = {0x2E, 0x32, 0x3E};
    }

    std::vector<uint8_t> rgba(32u * 32u * 4u, 0);
    auto put_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        const size_t off = static_cast<size_t>((y * 32 + x) * 4);
        rgba[off + 0] = r;
        rgba[off + 1] = g;
        rgba[off + 2] = b;
        rgba[off + 3] = a;
    };

    const int left = 2;
    const int right = 29;
    const int top = (kind == Common::AltCore::CoreKind::GBA) ? 6 : 2;
    const int bottom = (kind == Common::AltCore::CoreKind::GBA) ? 27 : 29;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            // Rounded corners
            const bool corner = (x == left || x == right) && (y == top || y == bottom);
            if (corner) {
                continue;
            }
            const bool edge = (x == left || x == right || y == top || y == bottom);
            if (edge) {
                put_px(x, y, border[0], border[1], border[2], 0xFF);
                continue;
            }
            // Inner label sticker area
            const int lbl_top = top + 5;
            const int lbl_bot = bottom - 4;
            const int lbl_left = left + 4;
            const int lbl_right = right - 4;
            if (x >= lbl_left && x <= lbl_right && y >= lbl_top && y <= lbl_bot) {
                // Subtle diagonal stripe pattern from seed
                const bool stripe = (((x + y + static_cast<int>(seed & 7u)) / 4) & 1) != 0;
                const uint8_t r = static_cast<uint8_t>(std::min(255, accent[0] + (stripe ? 18 : 0)));
                const uint8_t g = static_cast<uint8_t>(std::min(255, accent[1] + (stripe ? 18 : 0)));
                const uint8_t b = static_cast<uint8_t>(std::min(255, accent[2] + (stripe ? 18 : 0)));
                put_px(x, y, r, g, b, 0xFF);
            } else {
                // Cartridge grip lines near top
                const bool grip = (y == top + 2) && (x >= left + 4 && x <= right - 4);
                const int delta = grip ? -18 : 0;
                put_px(x, y, static_cast<uint8_t>(std::max(0, static_cast<int>(shell[0]) + delta)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(shell[1]) + delta)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(shell[2]) + delta)), 0xFF);
            }
        }
    }
    return rgba;
}

uint32_t PackGameCodeBE(std::string_view code, uint32_t fallback_crc) {
    if (code.size() >= 4) {
        return (static_cast<uint32_t>(static_cast<uint8_t>(code[0])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(code[1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(code[2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(code[3]));
    }
    return fallback_crc;
}

} // namespace

uint32_t ComputeCrc32(const uint8_t* data, size_t size, uint32_t seed) {
    static const std::array<uint32_t, 256> kTable = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = ~seed;
    for (size_t i = 0; i < size; ++i) {
        crc = kTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

Common::AltCore::CoreKind DetectCoreKind(const std::string& path) {
    const auto direct = KindFromExtension(path);
    if (direct != Common::AltCore::CoreKind::N3DS) {
        return direct;
    }
    if (IsZipExtension(path)) {
        if (const auto entry = FindRomEntryInZip(path); entry.has_value()) {
            return entry->kind;
        }
    }
    return Common::AltCore::CoreKind::N3DS;
}

bool ExtractRomFromZip(const std::string& zip_path, std::vector<uint8_t>& out_data,
                       std::string* inner_name, size_t max_bytes) {
    out_data.clear();

    // First try mGBA's VDirOpenArchive (handles both Deflate and Stored ZIP entries via minizip).
    if (VDir* archive = VDirOpenArchive(zip_path.c_str())) {
        bool ok = false;
        while (VDirEntry* ent = archive->listNext(archive)) {
            if (ent->type(ent) != VFS_FILE) {
                continue;
            }
            const char* name = ent->name(ent);
            if (!name || KindFromExtension(name) == Common::AltCore::CoreKind::N3DS) {
                continue;
            }
            if (VFile* vf = archive->openFile(archive, name, 0)) {
                const ssize_t full_sz = vf->size(vf);
                size_t to_read = full_sz > 0 ? static_cast<size_t>(full_sz) : 0;
                if (max_bytes > 0 && (to_read == 0 || to_read > max_bytes)) {
                    to_read = max_bytes;
                }
                if (to_read > 0) {
                    out_data.resize(to_read);
                    const ssize_t got = vf->read(vf, out_data.data(), to_read);
                    if (got > 0) {
                        out_data.resize(static_cast<size_t>(got));
                        if (inner_name) {
                            *inner_name = name;
                        }
                        ok = true;
                    } else {
                        out_data.clear();
                    }
                }
                vf->close(vf);
            }
            if (ok) {
                break;
            }
        }
        archive->close(archive);
        if (ok) {
            return true;
        }
    }

    // Fallback: direct read for Stored (uncompressed) ZIP entries.
    const auto entry = FindRomEntryInZip(zip_path);
    if (!entry.has_value() || entry->compression != 0 || entry->uncompressed_size == 0) {
        return false;
    }
    std::ifstream f(zip_path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.seekg(static_cast<std::streamoff>(entry->local_header_offset), std::ios::beg);
    uint8_t lfh[30]{};
    if (!f.read(reinterpret_cast<char*>(lfh), sizeof(lfh)) || ReadLE32(lfh) != 0x04034B50u) {
        return false;
    }
    const uint16_t name_len = ReadLE16(lfh + 26);
    const uint16_t extra_len = ReadLE16(lfh + 28);
    f.seekg(static_cast<std::streamoff>(name_len + extra_len), std::ios::cur);
    const size_t to_read =
        (max_bytes > 0 && max_bytes < entry->uncompressed_size) ? max_bytes : entry->uncompressed_size;
    out_data.resize(to_read);
    if (!f.read(reinterpret_cast<char*>(out_data.data()), static_cast<std::streamsize>(to_read))) {
        out_data.clear();
        return false;
    }
    if (inner_name) {
        *inner_name = entry->name;
    }
    return true;
}

namespace {
std::optional<RomInfo> ProbeRomUncached(const std::string& path);
}

std::optional<RomInfo> ProbeRom(const std::string& path) {
    if (!IsZipExtension(path)) {
        return ProbeRomUncached(path);
    }
    // A zip is read once per file version: scans and starts share the result.
    struct Key {
        std::uintmax_t size;
        std::filesystem::file_time_type mtime;
        bool operator==(const Key& o) const {
            return size == o.size && mtime == o.mtime;
        }
    };
    static std::mutex mutex;
    static std::map<std::string, std::pair<Key, std::optional<RomInfo>>> cache;

    std::error_code ec;
    const Key key{std::filesystem::file_size(path, ec), std::filesystem::last_write_time(path, ec)};
    if (ec) {
        return std::nullopt;
    }
    {
        std::lock_guard lock(mutex);
        const auto it = cache.find(path);
        if (it != cache.end() && it->second.first == key) {
            return it->second.second;
        }
    }
    auto info = ProbeRomUncached(path);
    {
        std::lock_guard lock(mutex);
        if (cache.size() >= 256) {
            cache.clear();
        }
        cache[path] = {key, info};
    }
    return info;
}

namespace {
std::optional<RomInfo> ProbeRomUncached(const std::string& path) {
    const auto detected = DetectCoreKind(path);
    if (detected == Common::AltCore::CoreKind::N3DS) {
        return std::nullopt;
    }

    const bool is_zip = IsZipExtension(path);
    std::vector<uint8_t> head;
    std::vector<uint8_t> banner_buf;
    std::string fallback_stem = std::filesystem::path(path).stem().string();

    constexpr size_t kProbePrefixBytes = 64u * 1024u;
    if (is_zip) {
        std::string inner;
        const bool is_ds = (detected == Common::AltCore::CoreKind::NDS ||
                            detected == Common::AltCore::CoreKind::DSi);
        if (!ExtractRomFromZip(path, head, &inner, kProbePrefixBytes)) {
            return std::nullopt;
        }
        // A DS banner can sit past the first 64 KB: read on only as far as it goes.
        if (is_ds && head.size() >= kProbePrefixBytes) {
            const std::uint64_t banner_end = std::uint64_t{ReadLE32(head.data() + 0x68)} + 0x840;
            if (banner_end > head.size() && banner_end <= 512ull * 1024 * 1024) {
                std::vector<uint8_t> more;
                if (ExtractRomFromZip(path, more, nullptr, static_cast<size_t>(banner_end))) {
                    head = std::move(more);
                }
            }
        }
        if (!inner.empty()) {
            fallback_stem = std::filesystem::path(inner).stem().string();
        }
    } else {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            return std::nullopt;
        }
        head.resize(kProbePrefixBytes);
        f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        const auto got = static_cast<size_t>(f.gcount());
        head.resize(got);

        if ((detected == Common::AltCore::CoreKind::NDS ||
             detected == Common::AltCore::CoreKind::DSi) &&
            head.size() >= 0x200) {
            const uint32_t banner_offset = ReadLE32(head.data() + 0x68);
            if (banner_offset >= 0x200) {
                f.clear();
                f.seekg(static_cast<std::streamoff>(banner_offset), std::ios::beg);
                banner_buf.resize(0x840);
                f.read(reinterpret_cast<char*>(banner_buf.data()),
                       static_cast<std::streamsize>(banner_buf.size()));
                banner_buf.resize(static_cast<size_t>(f.gcount()));
            }
        }
    }

    RomInfo info{};
    info.kind = detected;
    info.icon_w = 32;
    info.icon_h = 32;

    const size_t crc_len = std::min(head.size(), kProbePrefixBytes);
    info.crc32 = ComputeCrc32(head.data(), crc_len);

    if (detected == Common::AltCore::CoreKind::NDS || detected == Common::AltCore::CoreKind::DSi) {
        if (head.size() < 0x200) {
            return std::nullopt;
        }
        const std::string hdr_title = CleanAsciiField(head.data() + 0x00, 12);
        info.game_code = CleanAsciiField(head.data() + 0x0C, 4);
        const uint8_t unit_code = head[0x12];
        info.dsi_enhanced = (unit_code & 0x02) != 0;

        uint32_t dsi_tid_hi = 0;
        if (head.size() >= 0x238) {
            dsi_tid_hi = ReadLE32(head.data() + 0x234);
        }
        info.dsi_ware = (detected == Common::AltCore::CoreKind::DSi) || (unit_code == 0x03) ||
                        (info.dsi_enhanced && dsi_tid_hi == 0x00030004u);
        if (info.dsi_ware) {
            info.kind = Common::AltCore::CoreKind::DSi;
        }

        // Locate banner data (either already read into banner_buf or inside full zip buffer `head`)
        const uint8_t* banner_ptr = nullptr;
        size_t banner_len = 0;
        if (banner_buf.size() >= 0x440) {
            banner_ptr = banner_buf.data();
            banner_len = banner_buf.size();
        } else {
            const uint32_t banner_offset = ReadLE32(head.data() + 0x68);
            if (banner_offset >= 0x200 && static_cast<size_t>(banner_offset) + 0x440 <= head.size()) {
                banner_ptr = head.data() + banner_offset;
                banner_len = head.size() - banner_offset;
            }
        }

        if (banner_ptr && banner_len >= 0x440) {
            info.icon_rgba = DecodeNdsBannerIcon(banner_ptr + 0x20, banner_ptr + 0x220);
            // English title is at banner + 0x340 (128 UTF-16LE chars); Japanese at + 0x240
            info.title = DecodeUtf16LETitle(banner_ptr + 0x340, 128);
            if (info.title.empty()) {
                info.title = DecodeUtf16LETitle(banner_ptr + 0x240, 128);
            }
        }
        if (info.title.empty()) {
            info.title = !hdr_title.empty() ? hdr_title : fallback_stem;
        }
        if (info.icon_rgba.empty()) {
            info.icon_rgba = MakeDeterministicTileIcon(info.kind, info.crc32);
        }

        const uint64_t tag =
            (info.kind == Common::AltCore::CoreKind::DSi) ? Common::AltCore::kTagDSi
                                                          : Common::AltCore::kTagNDS;
        info.title_id =
            Common::AltCore::MakeKey(tag, PackGameCodeBE(info.game_code, info.crc32), info.crc32);
        return info;
    }

    if (detected == Common::AltCore::CoreKind::GBA) {
        if (head.size() < 0xC0) {
            return std::nullopt;
        }
        const std::string hdr_title = CleanAsciiField(head.data() + 0xA0, 12);
        info.game_code = CleanAsciiField(head.data() + 0xAC, 4);
        info.title = !hdr_title.empty() ? hdr_title : fallback_stem;
        info.icon_rgba = MakeDeterministicTileIcon(Common::AltCore::CoreKind::GBA, info.crc32);
        info.title_id = Common::AltCore::MakeKey(
            Common::AltCore::kTagGBA, PackGameCodeBE(info.game_code, info.crc32), info.crc32);
        return info;
    }

    if (detected == Common::AltCore::CoreKind::GB) {
        if (head.size() < 0x150) {
            return std::nullopt;
        }
        const uint8_t cgb_flag = head[0x143];
        const bool is_gbc = (cgb_flag == 0x80 || cgb_flag == 0xC0);
        info.is_gbc = is_gbc;
        const size_t title_len = is_gbc ? 15 : 16;
        const std::string hdr_title = CleanAsciiField(head.data() + 0x134, title_len);
        info.title = !hdr_title.empty() ? hdr_title : fallback_stem;
        info.icon_rgba =
            MakeDeterministicTileIcon(Common::AltCore::CoreKind::GB, info.crc32, is_gbc);
        const uint32_t title_hash =
            ComputeCrc32(reinterpret_cast<const uint8_t*>(info.title.data()), info.title.size());
        info.title_id = Common::AltCore::MakeKey(Common::AltCore::kTagGB, title_hash, info.crc32);
        return info;
    }

    return std::nullopt;
}
} // namespace

GameSystem SystemOf(const RomInfo& info) {
    switch (info.kind) {
    case Common::AltCore::CoreKind::GBA:
        return GameSystem::GBA;
    case Common::AltCore::CoreKind::GB:
        return info.is_gbc ? GameSystem::GBC : GameSystem::GB;
    case Common::AltCore::CoreKind::NDS:
    case Common::AltCore::CoreKind::DSi:
        return GameSystem::DS;
    default:
        return GameSystem::N3DS;
    }
}

namespace {

/// A region tag's word as the list's region names have it ("" none).
const char* RegionWord(std::string_view word) {
    static constexpr std::array<std::pair<std::string_view, const char*>, 20> kWords{{
        {"USA", "North America"},  {"Canada", "North America"}, {"U", "North America"},
        {"Japan", "Japan"},        {"J", "Japan"},              {"Europe", "Europe"},
        {"E", "Europe"},           {"Germany", "Europe"},       {"France", "Europe"},
        {"Spain", "Europe"},       {"Italy", "Europe"},         {"Netherlands", "Europe"},
        {"Sweden", "Europe"},      {"UK", "Europe"},            {"Scandinavia", "Europe"},
        {"Australia", "Australia"}, {"Korea", "Korea"},         {"China", "China"},
        {"Taiwan", "Taiwan"},      {"World", "Region free"},
    }};
    for (const auto& [tag, region] : kWords) {
        if (word == tag) {
            return region;
        }
    }
    return "";
}

} // namespace

const char* RegionOf(const RomInfo& info) {
    if (!Common::AltCore::IsDs(info.kind) || info.game_code.size() < 4) {
        return "";
    }
    switch (info.game_code[3]) {
    case 'J':
        return "Japan";
    case 'E':
        return "North America";
    case 'P': // English and more
    case 'D': // German
    case 'F': // French
    case 'H': // Dutch
    case 'I': // Italian
    case 'S': // Spanish
    case 'X':
    case 'Y':
    case 'Z':
        return "Europe";
    case 'U':
        return "Australia";
    case 'K':
        return "Korea";
    case 'C':
        return "China";
    default:
        return "";
    }
}

std::vector<const char*> RegionsOfName(const std::string& name) {
    for (std::size_t open = name.find('('); open != std::string::npos;
         open = name.find('(', open + 1)) {
        const std::size_t close = name.find(')', open);
        if (close == std::string::npos) {
            break;
        }
        const std::string_view tag = std::string_view(name).substr(open + 1, close - open - 1);
        std::vector<const char*> regions;
        bool all = !tag.empty();
        const auto add = [&regions](const char* region) {
            if (std::find(regions.begin(), regions.end(), std::string_view(region)) ==
                regions.end()) {
                regions.push_back(region);
            }
        };
        if (tag.size() <= 3 && tag.find_first_not_of("UEJ") == std::string_view::npos) {
            for (const char c : tag) { // GoodTools: (U), (E), (J), (UE), (JU)...
                add(RegionWord(std::string_view(&c, 1)));
            }
        } else {
            for (std::size_t at = 0; at <= tag.size();) {
                std::size_t end = std::min(tag.find(',', at), tag.size());
                std::string_view word = tag.substr(at, end - at);
                while (!word.empty() && word.front() == ' ') {
                    word.remove_prefix(1);
                }
                const char* region = word.size() > 1 ? RegionWord(word) : "";
                all = all && *region != 0;
                if (*region) {
                    add(region);
                }
                at = end + 1;
            }
        }
        if (all && !regions.empty()) {
            return regions;
        }
    }
    return {};
}

std::string CleanName(std::string name) {
    // the tags
    std::string out;
    int depth = 0;
    for (const char c : name) {
        if (c == '(' || c == '[') {
            depth++;
        } else if ((c == ')' || c == ']') && depth > 0) {
            depth--;
        } else if (depth == 0 && !(c == ' ' && (out.empty() || out.back() == ' '))) {
            out.push_back(c);
        }
    }
    // "Name, The - More" as "The Name - More"
    const std::size_t dash = out.find(" - ");
    std::string first = out.substr(0, dash);
    const auto trim = [](std::string& s) {
        while (!s.empty() && s.back() == ' ') {
            s.pop_back();
        }
        s.erase(0, std::min(s.find_first_not_of(' '), s.size()));
    };
    trim(first);
    for (const std::string_view article : {"The", "A", "An"}) {
        const std::string tail = ", " + std::string(article);
        if (first.size() > tail.size() && first.ends_with(tail)) {
            first = std::string(article) + " " + first.substr(0, first.size() - tail.size());
            break;
        }
    }
    out = dash == std::string::npos ? first : first + out.substr(dash);
    trim(out);
    return out;
}

std::string NameOfFile(const std::string& path) {
    std::filesystem::path file = std::filesystem::path(path).stem();
    static constexpr std::array<std::string_view, 6> kRoms{".gba", ".gb", ".gbc", ".nds", ".dsi",
                                                           ".srl"};
    const std::string inner = Common::ToLower(file.extension().string()); // "Game.gba.zip"
    if (std::find(kRoms.begin(), kRoms.end(), inner) != kRoms.end()) {
        file = file.stem();
    }
    return CleanName(file.string());
}

std::string DisplayTitle(const RomInfo& info, const std::string& path) {
    if (info.kind != Common::AltCore::CoreKind::GBA && info.kind != Common::AltCore::CoreKind::GB) {
        return info.title; // a DS game's banner names it well
    }
    const std::string name = NameOfFile(path);
    return name.empty() ? info.title : name;
}

const char* CoreFolder(Common::AltCore::CoreKind kind) {
    switch (kind) {
    case Common::AltCore::CoreKind::GBA:
        return "gba";
    case Common::AltCore::CoreKind::GB:
        return "gb";
    default:
        return "ds";
    }
}

std::string GameFolder(const std::string& data_dir, const RomInfo& info) {
    return (std::filesystem::path(data_dir) / CoreFolder(info.kind) / "games" /
            fmt::format("{:016X}", info.title_id))
        .string();
}

} // namespace FrameCores
