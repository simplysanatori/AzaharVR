// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_xr/vis/hand_import.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <tuple>

#include <lodepng.h>

#include "frame_xr/vis/pit_model.h"

namespace FrameXr::Vis {

namespace {

// ---- what is looked for (the names such a model gives its parts) ----

constexpr std::string_view kModelName = "pit.dae";
/// The left arm's three meshes and the image set each is painted with.
struct MeshName {
    std::string_view geometry;
    std::string_view images;
};
constexpr std::array<MeshName, 3> kMeshes{{{"geom-arm_L_VIS_O_OBJShape_2", "skin_pit_001"},
                                           {"geom-arm_L_VIS_O_OBJShape", "def_pit_001"},
                                           {"geom-arm_L_VIS_O_OBJShape_1", "met_pit_001"}}};
enum Tex { kSkinTex, kSleeveTex, kMetalTex };
constexpr std::array<std::string_view, 4> kMaps{"col", "nor", "ao", "mtl"};

/// Our joints, a parent before its children: the name, its parent's place in this list, the
/// app's role (pit_hand.h PitHandJoint), and whether it only takes the hand's cupping.
struct JointName {
    std::string_view name;
    int parent;
    int role;
    bool relative;
};
constexpr std::array<JointName, 26> kJoints{{
    {"ArmL", -1, kPitForearm, true},
    {"HandL", 0, kPitWrist, true},
    {"FingerL10", 1, kPitIndexMetacarpal, true},
    {"FingerL11", 2, kPitIndexProximal, false},
    {"FingerL12", 3, kPitIndexIntermediate, false},
    {"FingerL13", 4, kPitIndexDistal, false},
    {"FingerL20", 1, kPitMiddleMetacarpal, true},
    {"FingerL21", 6, kPitMiddleProximal, false},
    {"FingerL22", 7, kPitMiddleIntermediate, false},
    {"FingerL23", 8, kPitMiddleDistal, false},
    {"FingerL30", 1, kPitRingMetacarpal, true},
    {"FingerL31", 10, kPitRingProximal, false},
    {"FingerL32", 11, kPitRingIntermediate, false},
    {"FingerL33", 12, kPitRingDistal, false},
    {"FingerL40", 1, kPitLittleMetacarpal, true},
    {"FingerL41", 14, kPitLittleProximal, false},
    {"FingerL42", 15, kPitLittleIntermediate, false},
    {"FingerL43", 16, kPitLittleDistal, false},
    {"FingerL51", 1, kPitThumbMetacarpal, false},
    {"FingerL52", 18, kPitThumbProximal, false},
    {"FingerL53", 19, kPitThumbDistal, false},
    {"FingerL14_eff", 5, kPitIndexTip, false},
    {"FingerL24_eff", 9, kPitMiddleTip, false},
    {"FingerL34_eff", 13, kPitRingTip, false},
    {"FingerL44_eff", 17, kPitLittleTip, false},
    {"FingerL54_eff", 20, kPitThumbTip, false},
}};
constexpr std::size_t kWristJoint = 1, kMiddleKnuckle = 7, kMiddleTip = 22;
/// A connected piece that hangs on any of these is left out: a weapon's light rings, and the
/// cuff of the upper arm (beyond where the forearm ends).
constexpr std::array<std::string_view, 3> kLeftOut{"Ring1", "Ring2", "ShoulderL"};
constexpr std::string_view kPlateJoint = "HandL"; ///< the gold piece on it is the plate

// ---- limits on the model file ----

constexpr std::size_t kXmlMaxNodes = 2'000'000;
constexpr std::size_t kXmlMaxDepth = 64;
constexpr std::size_t kXmlMaxAttributes = 64;
constexpr std::size_t kMaxNumbers = 8'000'000; ///< in one array
constexpr std::size_t kMaxTriangles = 200'000;
constexpr std::size_t kMaxPositions = 200'000;
constexpr std::size_t kMaxSkinJoints = 4096;

// ---- errors ----

struct Stop {
    std::string why;
};
[[noreturn]] void Fail(std::string why) {
    throw Stop{std::move(why)};
}
[[noreturn]] void BadNumbers() {
    Fail("The model's numbers do not add up.");
}

// ---- the zip ----

uint16_t U16(std::span<const uint8_t> d, std::size_t at) {
    return static_cast<uint16_t>(d[at] | (d[at + 1] << 8));
}
uint32_t U32(std::span<const uint8_t> d, std::size_t at) {
    return static_cast<uint32_t>(d[at]) | (static_cast<uint32_t>(d[at + 1]) << 8) |
           (static_cast<uint32_t>(d[at + 2]) << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
}

struct Member {
    std::string name;
    uint16_t flags = 0, method = 0;
    uint32_t crc = 0, packed = 0, unpacked = 0, offset = 0;
};

bool SafeName(std::string_view name) {
    if (name.empty() || name.size() > 1024 || name.front() == '/' || name.front() == '\\') {
        return false;
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i <= name.size(); i++) {
        const char c = i < name.size() ? name[i] : '/';
        if (c == '\0' || c == ':') {
            return false;
        }
        if (c == '/' || c == '\\') {
            if (name.substr(start, i - start) == "..") {
                return false;
            }
            start = i + 1;
        }
    }
    return true;
}

class Zip {
public:
    explicit Zip(std::span<const uint8_t> data) : data_(data) {
        if (data.size() > kHandZipMaxBytes) {
            Fail("The zip is too big.");
        }
        if (data.size() < 22 || U32(data, 0) != 0x04034b50u) {
            Fail("This is not a zip file.");
        }
        // The end record: the last one in the final 64 KiB and a bit.
        const std::size_t least = data.size() > 22 + 65535 ? data.size() - 22 - 65535 : 0;
        std::size_t end = data.size();
        for (std::size_t at = data.size() - 22 + 1; at-- > least;) {
            if (U32(data, at) == 0x06054b50u) {
                end = at;
                break;
            }
        }
        if (end == data.size()) {
            Fail("The zip is damaged.");
        }
        const uint16_t disk = U16(data, end + 4), start_disk = U16(data, end + 6);
        const uint16_t here = U16(data, end + 8), total = U16(data, end + 10);
        const uint32_t size = U32(data, end + 12), offset = U32(data, end + 16);
        if (total == 0xFFFF || size == 0xFFFFFFFFu || offset == 0xFFFFFFFFu) {
            Fail("The zip is too big.");
        }
        if (disk != 0 || start_disk != 0 || here != total || uint64_t{offset} + size > end) {
            Fail("The zip is damaged.");
        }
        if (total > kHandZipMaxMembers) {
            Fail("The zip has too many files.");
        }
        directory_ = offset;
        std::size_t at = offset;
        const std::size_t stop = std::size_t{offset} + size;
        std::set<std::string> seen;
        for (uint16_t i = 0; i < total; i++) {
            if (stop - at < 46 || U32(data, at) != 0x02014b50u) {
                Fail("The zip is damaged.");
            }
            Member m;
            m.flags = U16(data, at + 8);
            m.method = U16(data, at + 10);
            m.crc = U32(data, at + 16);
            m.packed = U32(data, at + 20);
            m.unpacked = U32(data, at + 24);
            const std::size_t name = U16(data, at + 28), extra = U16(data, at + 30), comment = U16(data, at + 32);
            m.offset = U32(data, at + 42);
            if (stop - at - 46 < name + extra + comment) {
                Fail("The zip is damaged.");
            }
            m.name.assign(reinterpret_cast<const char*>(data.data() + at + 46), name);
            at += 46 + name + extra + comment;
            if (!SafeName(m.name)) {
                Fail("The zip has a file with an unsafe name.");
            }
            if (!seen.insert(m.name).second) {
                Fail("The zip has the same file twice.");
            }
            members_.push_back(std::move(m));
        }
    }

    const std::vector<Member>& Members() const {
        return members_;
    }

    /// A member's bytes, unpacked and checked.
    std::vector<uint8_t> Read(const Member& m) {
        if (m.unpacked > kHandZipMaxMemberBytes || m.packed == 0xFFFFFFFFu || m.unpacked == 0xFFFFFFFFu) {
            Fail("A file in the zip is too big: " + m.name + ".");
        }
        if ((m.flags & 1) != 0 || (m.method != 0 && m.method != 8)) {
            Fail("A file in the zip is packed in a way this cannot read: " + m.name + ".");
        }
        if (m.method == 0 ? m.packed != m.unpacked : uint64_t{m.unpacked} > uint64_t{m.packed} * kHandZipMaxRatio) {
            Fail("A file in the zip is damaged: " + m.name + ".");
        }
        unpacked_ += m.unpacked;
        if (unpacked_ > kHandZipMaxUnpacked) {
            Fail("The zip is too big.");
        }
        // The local header, then the data, all before the directory.
        if (uint64_t{m.offset} + 30 > directory_ || U32(data_, m.offset) != 0x04034b50u) {
            Fail("A file in the zip is damaged: " + m.name + ".");
        }
        const uint64_t start = uint64_t{m.offset} + 30 + U16(data_, m.offset + 26) + U16(data_, m.offset + 28);
        if (start + m.packed > directory_) {
            Fail("A file in the zip is damaged: " + m.name + ".");
        }
        const uint8_t* in = data_.data() + start;
        std::vector<uint8_t> out;
        if (m.method == 0) {
            out.assign(in, in + m.packed);
        } else {
            LodePNGDecompressSettings settings;
            lodepng_decompress_settings_init(&settings);
            settings.max_output_size = m.unpacked;
            unsigned char* buffer = nullptr;
            std::size_t size = 0;
            const unsigned error = lodepng_inflate(&buffer, &size, in, m.packed, &settings);
            if (!error && size == m.unpacked) {
                out.assign(buffer, buffer + size);
            }
            std::free(buffer);
            if (error || size != m.unpacked) {
                Fail("A file in the zip is damaged: " + m.name + ".");
            }
        }
        if (lodepng_crc32(out.data(), out.size()) != m.crc) {
            Fail("A file in the zip is damaged: " + m.name + ".");
        }
        return out;
    }

private:
    std::span<const uint8_t> data_;
    std::vector<Member> members_;
    std::size_t directory_ = 0;
    uint64_t unpacked_ = 0;
};

// ---- the model file: a small strict reader of the XML it is written in ----

struct XmlNode {
    std::string_view name;
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
    std::string_view text; ///< the text before its first child (all of it when it has none)
    std::vector<uint32_t> children;

    std::string_view Attribute(std::string_view key) const {
        for (const auto& [k, v] : attributes) {
            if (k == key) {
                return v;
            }
        }
        return {};
    }
};

class Xml {
public:
    explicit Xml(std::string_view text) : s_(text) {
        Parse();
    }
    const XmlNode& Root() const {
        return nodes_[0];
    }
    const XmlNode& At(uint32_t i) const {
        return nodes_[i];
    }
    /// The first child of that name, or null.
    const XmlNode* Child(const XmlNode& n, std::string_view name) const {
        for (const uint32_t c : n.children) {
            if (nodes_[c].name == name) {
                return &nodes_[c];
            }
        }
        return nullptr;
    }
    std::vector<const XmlNode*> Children(const XmlNode& n, std::string_view name) const {
        std::vector<const XmlNode*> out;
        for (const uint32_t c : n.children) {
            if (nodes_[c].name == name) {
                out.push_back(&nodes_[c]);
            }
        }
        return out;
    }

private:
    [[noreturn]] static void Malformed() {
        Fail("The model file is not well formed.");
    }
    [[noreturn]] static void Refused() {
        Fail("The model file uses something this refuses (a document type or an entity).");
    }
    static bool NameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == ':';
    }
    static bool Space(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }
    void SkipSpace() {
        while (at_ < s_.size() && Space(s_[at_])) {
            at_++;
        }
    }
    std::string_view Name() {
        const std::size_t start = at_;
        while (at_ < s_.size() && NameChar(s_[at_])) {
            at_++;
        }
        if (at_ == start) {
            Malformed();
        }
        return s_.substr(start, at_ - start);
    }
    /// Text may hold the five plain escapes and nothing else that starts with an ampersand.
    static void CheckText(std::string_view t) {
        for (std::size_t i = t.find('&'); i != std::string_view::npos; i = t.find('&', i + 1)) {
            const std::string_view rest = t.substr(i, 6);
            if (!(rest.starts_with("&lt;") || rest.starts_with("&gt;") || rest.starts_with("&amp;") ||
                  rest.starts_with("&quot;") || rest.starts_with("&apos;"))) {
                Refused();
            }
        }
    }
    void Parse() {
        if (s_.starts_with("\xEF\xBB\xBF")) {
            at_ = 3;
        }
        SkipSpace();
        if (s_.substr(at_).starts_with("<?xml")) {
            const std::size_t end = s_.find("?>", at_);
            if (end == std::string_view::npos) {
                Malformed();
            }
            at_ = end + 2;
        }
        std::vector<uint32_t> open;
        bool closed_root = false;
        while (true) {
            // text up to the next tag
            const std::size_t lt = s_.find('<', at_);
            const std::string_view text = s_.substr(at_, lt == std::string_view::npos ? std::string_view::npos : lt - at_);
            if (open.empty()) {
                for (const char c : text) {
                    if (!Space(c)) {
                        Malformed();
                    }
                }
            } else {
                CheckText(text);
                XmlNode& top = nodes_[open.back()];
                if (top.children.empty() && top.text.empty()) {
                    top.text = text;
                }
            }
            if (lt == std::string_view::npos) {
                break;
            }
            at_ = lt + 1;
            if (at_ >= s_.size()) {
                Malformed();
            }
            if (s_[at_] == '!') {
                if (!s_.substr(at_).starts_with("!--")) {
                    Refused(); // a document type, an entity, a CDATA section
                }
                const std::size_t end = s_.find("-->", at_ + 3);
                if (end == std::string_view::npos) {
                    Malformed();
                }
                at_ = end + 3;
                continue;
            }
            if (s_[at_] == '?') {
                Refused(); // a processing instruction after the first line
            }
            if (s_[at_] == '/') {
                at_++;
                const std::string_view name = Name();
                SkipSpace();
                if (open.empty() || nodes_[open.back()].name != name || at_ >= s_.size() || s_[at_] != '>') {
                    Malformed();
                }
                at_++;
                open.pop_back();
                if (open.empty()) {
                    closed_root = true;
                }
                continue;
            }
            if (closed_root || nodes_.size() >= kXmlMaxNodes || open.size() >= kXmlMaxDepth) {
                if (closed_root) {
                    Malformed();
                }
                Fail("The model file is too big or too deep.");
            }
            XmlNode node;
            node.name = Name();
            while (true) {
                SkipSpace();
                if (at_ >= s_.size()) {
                    Malformed();
                }
                if (s_[at_] == '>' || s_[at_] == '/') {
                    break;
                }
                const std::string_view key = Name();
                SkipSpace();
                if (at_ >= s_.size() || s_[at_] != '=') {
                    Malformed();
                }
                at_++;
                SkipSpace();
                if (at_ >= s_.size() || (s_[at_] != '"' && s_[at_] != '\'')) {
                    Malformed();
                }
                const char quote = s_[at_++];
                const std::size_t end = s_.find(quote, at_);
                if (end == std::string_view::npos) {
                    Malformed();
                }
                const std::string_view value = s_.substr(at_, end - at_);
                if (value.find('<') != std::string_view::npos) {
                    Malformed();
                }
                CheckText(value);
                at_ = end + 1;
                if (node.attributes.size() >= kXmlMaxAttributes) {
                    Fail("The model file is too big or too deep.");
                }
                node.attributes.emplace_back(key, value);
            }
            const bool empty = s_[at_] == '/';
            if (empty) {
                at_++;
                if (at_ >= s_.size() || s_[at_] != '>') {
                    Malformed();
                }
            }
            at_++;
            const auto index = static_cast<uint32_t>(nodes_.size());
            if (!open.empty()) {
                nodes_[open.back()].children.push_back(index);
            } else if (!nodes_.empty()) {
                Malformed(); // a second root
            }
            nodes_.push_back(std::move(node));
            if (!empty) {
                open.push_back(index);
            } else if (open.empty()) {
                closed_root = true;
            }
        }
        if (!open.empty() || nodes_.empty()) {
            Malformed();
        }
    }

    std::string_view s_;
    std::size_t at_ = 0;
    std::vector<XmlNode> nodes_;
};

/// Numbers separated by spaces: plain decimals with an optional exponent, nothing else.
std::vector<double> Numbers(std::string_view t, std::size_t most = kMaxNumbers) {
    std::vector<double> out;
    std::size_t i = 0;
    const std::size_t n = t.size();
    while (true) {
        while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        if (out.size() >= most) {
            Fail("The model file is too big or too deep.");
        }
        bool negative = false;
        if (t[i] == '-' || t[i] == '+') {
            negative = t[i] == '-';
            i++;
        }
        double value = 0.0;
        int digits = 0, scale = 0;
        while (i < n && t[i] >= '0' && t[i] <= '9') {
            if (digits < 18) {
                value = value * 10.0 + (t[i] - '0');
            } else {
                scale++;
            }
            digits++;
            i++;
        }
        if (i < n && t[i] == '.') {
            i++;
            while (i < n && t[i] >= '0' && t[i] <= '9') {
                if (digits < 18) {
                    value = value * 10.0 + (t[i] - '0');
                    scale--;
                }
                digits++;
                i++;
            }
        }
        if (digits == 0) {
            BadNumbers();
        }
        if (i < n && (t[i] == 'e' || t[i] == 'E')) {
            i++;
            bool minus = false;
            if (i < n && (t[i] == '-' || t[i] == '+')) {
                minus = t[i] == '-';
                i++;
            }
            int e = 0, ed = 0;
            while (i < n && t[i] >= '0' && t[i] <= '9') {
                e = std::min(e * 10 + (t[i] - '0'), 10000);
                ed++;
                i++;
            }
            if (ed == 0) {
                BadNumbers();
            }
            scale += minus ? -e : e;
        }
        if (i < n && !(t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) {
            BadNumbers();
        }
        value *= std::pow(10.0, std::clamp(scale, -400, 400));
        if (!std::isfinite(value)) {
            BadNumbers();
        }
        out.push_back(negative ? -value : value);
    }
    return out;
}

std::vector<uint32_t> Indices(std::string_view t, std::size_t most = kMaxNumbers) {
    std::vector<uint32_t> out;
    for (const double v : Numbers(t, most)) {
        if (v < 0.0 || v > 4.0e9 || v != std::floor(v)) {
            BadNumbers();
        }
        out.push_back(static_cast<uint32_t>(v));
    }
    return out;
}

std::vector<std::string_view> SplitNames(std::string_view t, std::size_t most) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (true) {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) {
            i++;
        }
        if (i >= t.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < t.size() && !(t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) {
            i++;
        }
        if (out.size() >= most) {
            Fail("The model file is too big or too deep.");
        }
        out.push_back(t.substr(start, i - start));
    }
    return out;
}

// ---- small geometry ----

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V3 operator-(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V3 operator*(V3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Length(V3 a) {
    return std::sqrt(Dot(a, a));
}
V3 Unit(V3 a) {
    const double l = Length(a);
    if (!(l > 1e-12)) {
        BadNumbers();
    }
    return a * (1.0 / l);
}

/// A joint's bind: its axes (columns) and its place.
struct Bind {
    std::array<V3, 3> axis{};
    V3 at{};
};

/// The inverse of a placement given row major (rotation, perhaps scaled, and translation).
Bind InvertPlacement(const double* m) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(std::abs(det) > 1e-12) || !std::isfinite(det)) {
        BadNumbers();
    }
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    const V3 t{m[3], m[7], m[11]};
    Bind out;
    out.axis[0] = {r[0], r[3], r[6]};
    out.axis[1] = {r[1], r[4], r[7]};
    out.axis[2] = {r[2], r[5], r[8]};
    out.at = {-(r[0] * t.x + r[1] * t.y + r[2] * t.z), -(r[3] * t.x + r[4] * t.y + r[5] * t.z),
              -(r[6] * t.x + r[7] * t.y + r[8] * t.z)};
    return out;
}

double Median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
        m = 0.5 * (m + *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid)));
    }
    return m;
}

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(std::size_t n) : parent(n) {
        for (std::size_t i = 0; i < n; i++) {
            parent[i] = static_cast<uint32_t>(i);
        }
    }
    uint32_t Find(uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void Join(uint32_t a, uint32_t b) {
        parent[Find(b)] = Find(a);
    }
};

// ---- the model's parts ----

/// One mesh as the file has it.
struct SourceMesh {
    std::vector<V3> positions;
    std::vector<uint32_t> corner_position; ///< 3 a triangle
    std::vector<V3> corner_normal;
    std::vector<std::array<double, 2>> corner_uv;
    /// Per position: (joint number in `joints`, weight).
    std::vector<std::vector<std::pair<uint32_t, double>>> weights;
    std::vector<std::string_view> joints;
    std::vector<Bind> binds; ///< per joint
};

struct Source {
    std::vector<double> values;
    std::size_t stride = 0;
};

const XmlNode& Need(const XmlNode* n) {
    if (!n) {
        BadNumbers();
    }
    return *n;
}

std::string_view Ref(std::string_view r) {
    return r.starts_with("#") ? r.substr(1) : r;
}

Source ReadSource(const Xml& xml, const XmlNode& parent, std::string_view id) {
    for (const XmlNode* s : xml.Children(parent, "source")) {
        if (s->Attribute("id") != id) {
            continue;
        }
        Source out;
        out.values = Numbers(Need(xml.Child(*s, "float_array")).text);
        const XmlNode& accessor = Need(xml.Child(Need(xml.Child(*s, "technique_common")), "accessor"));
        const auto stride = Indices(accessor.Attribute("stride"), 1);
        out.stride = stride.empty() ? 1 : stride[0];
        if (out.stride == 0 || out.stride > 16 || out.values.size() % out.stride != 0) {
            BadNumbers();
        }
        return out;
    }
    BadNumbers();
}

uint32_t Offset(const XmlNode& input) {
    const auto v = Indices(input.Attribute("offset"), 1);
    if (v.size() != 1 || v[0] > 15) {
        BadNumbers();
    }
    return v[0];
}

SourceMesh ReadMesh(const Xml& xml, std::string_view geometry_id, const std::map<std::string_view, std::string_view>& joint_names) {
    const XmlNode& root = xml.Root();
    SourceMesh out;
    const XmlNode* geometry = nullptr;
    if (const XmlNode* library = xml.Child(root, "library_geometries")) {
        for (const XmlNode* g : xml.Children(*library, "geometry")) {
            if (g->Attribute("id") == geometry_id) {
                geometry = g;
            }
        }
    }
    if (!geometry) {
        Fail(geometry_id == kMeshes[0].geometry ? std::string("The model has no left hand.")
                                                : "The model is missing a part of the left arm: " + std::string(geometry_id) + ".");
    }
    const XmlNode& mesh = Need(xml.Child(*geometry, "mesh"));
    const XmlNode& triangles = Need(xml.Child(mesh, "triangles"));
    std::string_view vertex_ref, normal_ref, uv_ref;
    uint32_t vertex_at = 0, normal_at = 0, uv_at = 0, stride = 0;
    for (const XmlNode* input : xml.Children(triangles, "input")) {
        const std::string_view semantic = input->Attribute("semantic");
        const uint32_t offset = Offset(*input);
        stride = std::max(stride, offset + 1);
        if (semantic == "VERTEX") {
            vertex_ref = Ref(input->Attribute("source"));
            vertex_at = offset;
        } else if (semantic == "NORMAL") {
            normal_ref = Ref(input->Attribute("source"));
            normal_at = offset;
        } else if (semantic == "TEXCOORD" && uv_ref.empty()) {
            uv_ref = Ref(input->Attribute("source"));
            uv_at = offset;
        }
    }
    if (vertex_ref.empty() || normal_ref.empty() || uv_ref.empty()) {
        BadNumbers();
    }
    std::string_view position_ref;
    for (const XmlNode* v : xml.Children(mesh, "vertices")) {
        if (v->Attribute("id") != vertex_ref) {
            continue;
        }
        for (const XmlNode* input : xml.Children(*v, "input")) {
            if (input->Attribute("semantic") == "POSITION") {
                position_ref = Ref(input->Attribute("source"));
            }
        }
    }
    const Source positions = ReadSource(xml, mesh, position_ref);
    const Source normals = ReadSource(xml, mesh, normal_ref);
    const Source uvs = ReadSource(xml, mesh, uv_ref);
    if (positions.stride != 3 || normals.stride != 3 || uvs.stride < 2 || positions.values.size() / 3 > kMaxPositions) {
        BadNumbers();
    }
    const std::size_t position_count = positions.values.size() / 3;
    out.positions.resize(position_count);
    for (std::size_t i = 0; i < position_count; i++) {
        out.positions[i] = {positions.values[i * 3], positions.values[i * 3 + 1], positions.values[i * 3 + 2]};
    }
    const std::vector<uint32_t> p = Indices(Need(xml.Child(triangles, "p")).text);
    if (p.empty() || p.size() % (std::size_t{stride} * 3) != 0 || p.size() / (std::size_t{stride} * 3) > kMaxTriangles) {
        BadNumbers();
    }
    const std::size_t corners = p.size() / stride;
    out.corner_position.resize(corners);
    out.corner_normal.resize(corners);
    out.corner_uv.resize(corners);
    for (std::size_t c = 0; c < corners; c++) {
        const uint32_t pi = p[c * stride + vertex_at], ni = p[c * stride + normal_at], ti = p[c * stride + uv_at];
        if (pi >= position_count || ni >= normals.values.size() / 3 || ti >= uvs.values.size() / uvs.stride) {
            BadNumbers();
        }
        out.corner_position[c] = pi;
        out.corner_normal[c] = {normals.values[std::size_t{ni} * 3], normals.values[std::size_t{ni} * 3 + 1],
                                normals.values[std::size_t{ni} * 3 + 2]};
        out.corner_uv[c] = {uvs.values[ti * uvs.stride], uvs.values[ti * uvs.stride + 1]};
    }
    // Its skin.
    const XmlNode* skin = nullptr;
    if (const XmlNode* library = xml.Child(root, "library_controllers")) {
        for (const XmlNode* c : xml.Children(*library, "controller")) {
            const XmlNode* s = xml.Child(*c, "skin");
            if (s && Ref(s->Attribute("source")) == geometry_id) {
                skin = s;
            }
        }
    }
    if (!skin) {
        Fail("The model is missing a part of the left arm: " + std::string(geometry_id) + ".");
    }
    std::string_view joint_ref, bind_ref;
    for (const XmlNode* input : xml.Children(Need(xml.Child(*skin, "joints")), "input")) {
        if (input->Attribute("semantic") == "JOINT") {
            joint_ref = Ref(input->Attribute("source"));
        } else if (input->Attribute("semantic") == "INV_BIND_MATRIX") {
            bind_ref = Ref(input->Attribute("source"));
        }
    }
    for (const XmlNode* s : xml.Children(*skin, "source")) {
        if (s->Attribute("id") == joint_ref) {
            out.joints = SplitNames(Need(xml.Child(*s, "Name_array")).text, kMaxSkinJoints);
        }
    }
    const Source inverse = ReadSource(xml, *skin, bind_ref);
    if (out.joints.empty() || inverse.stride != 16 || inverse.values.size() != out.joints.size() * 16) {
        BadNumbers();
    }
    out.binds.resize(out.joints.size());
    for (std::size_t j = 0; j < out.joints.size(); j++) {
        out.binds[j] = InvertPlacement(&inverse.values[j * 16]);
        // by the scene's own name for it, where it has one
        const auto named = joint_names.find(out.joints[j]);
        if (named != joint_names.end()) {
            out.joints[j] = named->second;
        }
    }
    const XmlNode& vertex_weights = Need(xml.Child(*skin, "vertex_weights"));
    std::string_view weight_ref;
    uint32_t joint_at = 0, weight_at = 0, pair = 0;
    for (const XmlNode* input : xml.Children(vertex_weights, "input")) {
        const uint32_t offset = Offset(*input);
        pair = std::max(pair, offset + 1);
        if (input->Attribute("semantic") == "JOINT") {
            joint_at = offset;
        } else if (input->Attribute("semantic") == "WEIGHT") {
            weight_ref = Ref(input->Attribute("source"));
            weight_at = offset;
        }
    }
    const Source weights = ReadSource(xml, *skin, weight_ref);
    const std::vector<uint32_t> counts = Indices(Need(xml.Child(vertex_weights, "vcount")).text);
    const std::vector<uint32_t> v = Indices(Need(xml.Child(vertex_weights, "v")).text);
    if (pair == 0 || counts.size() != position_count) {
        BadNumbers();
    }
    out.weights.resize(position_count);
    std::size_t at = 0;
    for (std::size_t i = 0; i < position_count; i++) {
        if (counts[i] > 64 || (v.size() - at) / pair < counts[i]) {
            BadNumbers();
        }
        for (uint32_t k = 0; k < counts[i]; k++, at += pair) {
            const uint32_t joint = v[at + joint_at], weight = v[at + weight_at];
            if (joint >= out.joints.size() || weight >= weights.values.size()) {
                BadNumbers();
            }
            out.weights[i].emplace_back(joint, weights.values[weight]);
        }
    }
    return out;
}

/// The scene's nodes: what each joint the skins speak of (by sid) is called.
std::map<std::string_view, std::string_view> JointNames(const Xml& xml) {
    std::map<std::string_view, std::string_view> out;
    const XmlNode* library = xml.Child(xml.Root(), "library_visual_scenes");
    if (!library) {
        return out;
    }
    std::vector<const XmlNode*> todo{library};
    while (!todo.empty()) {
        const XmlNode* n = todo.back();
        todo.pop_back();
        for (const uint32_t c : n->children) {
            const XmlNode& child = xml.At(c);
            if (child.name == "node") {
                if (!child.Attribute("sid").empty() && !child.Attribute("name").empty()) {
                    out.emplace(child.Attribute("sid"), child.Attribute("name"));
                }
                todo.push_back(&child);
            } else if (child.name == "visual_scene") {
                todo.push_back(&child);
            }
        }
    }
    return out;
}

// ---- pictures ----

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    /// A texel, the picture repeating in both directions.
    const uint8_t* At(int64_t x, int64_t y) const {
        const auto w = static_cast<int64_t>(width), h = static_cast<int64_t>(height);
        x = ((x % w) + w) % w;
        y = ((y % h) + h) % h;
        return &rgba[static_cast<std::size_t>(y * w + x) * 4];
    }
};

Image DecodePng(const std::vector<uint8_t>& png, const std::string& name) {
    LodePNGState state;
    lodepng_state_init(&state);
    unsigned w = 0, h = 0;
    Image out;
    std::string error;
    if (lodepng_inspect(&w, &h, &state, png.data(), png.size()) != 0 || w == 0 || h == 0) {
        error = "A picture could not be read: " + name + ".";
    } else if (w > kHandImageMaxSide || h > kHandImageMaxSide) {
        error = "A picture is too big: " + name + ".";
    } else {
        state.info_raw.colortype = LCT_RGBA;
        state.info_raw.bitdepth = 8;
        // The packed rows can be no bigger than 16 bit RGBA ones.
        state.decoder.zlibsettings.max_output_size = std::size_t{h} * (std::size_t{w} * 8 + 1);
        state.decoder.max_text_size = 1 << 16;
        state.decoder.max_icc_size = 1 << 16;
        unsigned char* pixels = nullptr;
        unsigned dw = 0, dh = 0;
        const unsigned failed = lodepng_decode(&pixels, &dw, &dh, &state, png.data(), png.size());
        if (failed || dw != w || dh != h) {
            error = "A picture could not be read: " + name + ".";
        } else {
            out.width = w;
            out.height = h;
            out.rgba.assign(pixels, pixels + std::size_t{w} * h * 4);
        }
        std::free(pixels);
    }
    lodepng_state_cleanup(&state);
    if (!error.empty()) {
        Fail(error);
    }
    return out;
}

// ---- the hand being built ----

struct Vertex {
    V3 position{}, normal{};
    std::array<double, 2> uv{}; ///< in its own picture, v up
    int tex = kSkinTex;
    bool plate = false;
    bool stub = false;
    std::array<double, 2> fade{}; ///< a stub's place in its patch
    std::vector<std::pair<int, double>> weights; ///< (our joint, weight)
};

constexpr uint8_t kGold = 26, kEnamel = 77, kLeather = 128, kSkin = 179, kCloth = 230; // pit_model.h
constexpr int kPad = 4;

// The stub toward the shoulder: a few triangles of our own, so the arm does not end in a hole.
constexpr double kStubStart = -0.116, kStubSolid = -0.134, kStubEnd = -0.166, kStubRadius = 0.0345;
constexpr int kStubSides = 20;

// The controller (the left one; its OpenXR aim space, metres) and how the hand holds it.
constexpr V3 kHandleAxis{0.05500, 0.73870, -0.67180};
constexpr V3 kHandleCentre{-0.013, -0.0444, 0.0871};
constexpr double kHandleRadius = 0.018;
constexpr double kHandleSlide = 0.016;    ///< the hand this far up the handle
constexpr double kGripTiltDegrees = 35.0; ///< the handle's lean across the palm
constexpr double kGripBehindKnuckles = 0.015;

template <typename T>
void Put(std::vector<uint8_t>& out, T value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}
void PutF(std::vector<uint8_t>& out, double value) {
    Put<float>(out, static_cast<float>(value));
}

struct Ask {
    const HandImportProgress& progress;
    void operator()(std::string_view doing, float done) const {
        if (progress && !progress(doing, done)) {
            Fail("Stopped.");
        }
    }
};

std::vector<uint8_t> Build(std::span<const uint8_t> zip_bytes, const HandImportProgress& progress) {
    const Ask ask{progress};
    ask("Reading the zip.", 0.0f);
    Zip zip(zip_bytes);
    // The model, and the folder it is in.
    const Member* model = nullptr;
    for (const Member& m : zip.Members()) {
        const std::string_view name = m.name;
        const std::size_t slash = name.find_last_of("/\\");
        if ((slash == std::string_view::npos ? name : name.substr(slash + 1)) == kModelName) {
            if (model) {
                Fail("The zip has more than one model in it (pit.dae).");
            }
            model = &m;
        }
    }
    if (!model) {
        Fail("The zip has no model in it (pit.dae).");
    }
    const std::string folder = model->name.substr(0, model->name.size() - kModelName.size());
    const auto member = [&](const std::string& name) -> const Member* {
        for (const Member& m : zip.Members()) {
            if (m.name == name) {
                return &m;
            }
        }
        return nullptr;
    };
    const std::vector<uint8_t> text = zip.Read(*model);
    ask("Reading the model.", 0.1f);
    const Xml xml(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
    const auto names = JointNames(xml);
    std::array<SourceMesh, 3> meshes;
    for (std::size_t i = 0; i < 3; i++) {
        meshes[i] = ReadMesh(xml, kMeshes[i].geometry, names);
        ask("Reading the model.", 0.15f + 0.05f * static_cast<float>(i));
    }
    // Our joints' binds: from whichever mesh's skin knows the joint.
    std::array<Bind, kJoints.size()> joint{};
    for (std::size_t j = 0; j < kJoints.size(); j++) {
        bool found = false;
        for (const SourceMesh& m : meshes) {
            for (std::size_t k = 0; k < m.joints.size() && !found; k++) {
                if (m.joints[k] == kJoints[j].name) {
                    joint[j] = m.binds[k];
                    found = true;
                }
            }
        }
        if (!found) {
            Fail("The model is missing a joint: " + std::string(kJoints[j].name) + ".");
        }
    }
    ask("Building the hand.", 0.3f);
    // Arm space: the wrist at the origin, at the size drawn.
    const V3 origin = joint[kWristJoint].at;
    const double reach = Length(joint[kMiddleTip].at - origin);
    if (!(reach > 1e-6)) {
        BadNumbers();
    }
    const double scale = static_cast<double>(kHandImportLength) / reach;
    for (Bind& b : joint) {
        b.at = (b.at - origin) * scale;
        for (V3& a : b.axis) {
            a = Unit(a);
        }
    }
    const auto ours = [](std::string_view name) {
        for (std::size_t j = 0; j < kJoints.size(); j++) {
            if (kJoints[j].name == name) {
                return static_cast<int>(j);
            }
        }
        return -1;
    };

    // One list of vertices and triangles from the three meshes.
    std::vector<Vertex> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    for (int tex = 0; tex < 3; tex++) {
        const SourceMesh& m = meshes[static_cast<std::size_t>(tex)];
        const std::size_t count = m.corner_position.size() / 3;
        // Its connected pieces, welded by place.
        std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> weld;
        std::vector<uint32_t> welded(m.positions.size());
        for (std::size_t i = 0; i < m.positions.size(); i++) {
            const V3& p = m.positions[i];
            const auto key = std::make_tuple(std::llround(p.x * 1e4), std::llround(p.y * 1e4), std::llround(p.z * 1e4));
            welded[i] = weld.try_emplace(key, static_cast<uint32_t>(weld.size())).first->second;
        }
        UnionFind pieces(weld.size());
        for (std::size_t t = 0; t < count; t++) {
            pieces.Join(welded[m.corner_position[t * 3]], welded[m.corner_position[t * 3 + 1]]);
            pieces.Join(welded[m.corner_position[t * 3]], welded[m.corner_position[t * 3 + 2]]);
        }
        // What each piece hangs on.
        std::map<uint32_t, std::pair<bool, bool>> piece; // left out, on the plate's joint
        for (std::size_t i = 0; i < m.positions.size(); i++) {
            auto& [left_out, on_plate] = piece[pieces.Find(welded[i])];
            for (const auto& [j, w] : m.weights[i]) {
                if (w <= 0.0) {
                    continue;
                }
                const std::string_view name = m.joints[j];
                left_out = left_out || std::find(kLeftOut.begin(), kLeftOut.end(), name) != kLeftOut.end();
                on_plate = on_plate || name == kPlateJoint;
            }
        }
        std::map<std::tuple<uint32_t, int64_t, int64_t, int64_t, int64_t, int64_t>, uint32_t> same;
        for (std::size_t t = 0; t < count; t++) {
            const auto& [left_out, on_plate] = piece[pieces.Find(welded[m.corner_position[t * 3]])];
            if (left_out) {
                continue;
            }
            std::array<uint32_t, 3> tri{};
            for (std::size_t k = 0; k < 3; k++) {
                const std::size_t c = t * 3 + k;
                const uint32_t pi = m.corner_position[c];
                const V3 n = m.corner_normal[c];
                const auto key = std::make_tuple(pi, std::llround(n.x * 1e4), std::llround(n.y * 1e4), std::llround(n.z * 1e4),
                                                 std::llround(m.corner_uv[c][0] * 1e6), std::llround(m.corner_uv[c][1] * 1e6));
                const auto [it, added] = same.try_emplace(key, static_cast<uint32_t>(vertices.size()));
                if (added) {
                    Vertex v;
                    v.position = (m.positions[pi] - origin) * scale;
                    v.normal = Unit(n);
                    v.uv = m.corner_uv[c];
                    v.tex = tex;
                    v.plate = tex == kMetalTex && on_plate;
                    for (const auto& [j, w] : m.weights[pi]) {
                        const int mine = ours(m.joints[j]);
                        if (w > 0.0 && mine < 0) {
                            Fail("The model is missing a joint: " + std::string(m.joints[j]) + ".");
                        }
                        if (w > 0.0) {
                            v.weights.emplace_back(mine, w);
                        }
                    }
                    if (v.weights.empty()) {
                        BadNumbers();
                    }
                    vertices.push_back(std::move(v));
                }
                tri[k] = it->second;
            }
            triangles.push_back(tri);
        }
    }
    if (triangles.empty() || vertices.size() > kPitModelMaxVertices) {
        Fail("The model has no left hand.");
    }
    // The stub, on the forearm's line.
    const V3 elbow = joint[0].at;
    {
        const double cy = elbow.y * 0.75, cz = elbow.z * 0.75;
        const std::array<std::pair<double, double>, 6> rows{{{kStubStart, 1.0}, {kStubSolid, 1.0}, {kStubSolid - 0.012, 0.99},
                                                              {kStubEnd + 0.012, 0.93}, {kStubEnd + 0.005, 0.72}, {kStubEnd, 0.38}}};
        const auto first = static_cast<uint32_t>(vertices.size());
        const auto add = [&](V3 p, V3 radial, double u) {
            Vertex v;
            v.position = p;
            const double lean = std::pow(std::clamp((kStubSolid - p.x) / (kStubSolid - kStubEnd), 0.0, 1.0), 2.0);
            v.normal = Unit(radial * (1.0 - 0.8 * lean) + V3{-1, 0, 0} * (0.8 * lean));
            v.stub = true;
            v.fade = {u, std::clamp((p.x - kStubEnd) / (kStubSolid - kStubEnd), 0.0, 1.0)};
            v.weights.emplace_back(0, 1.0);
            vertices.push_back(std::move(v));
        };
        for (const auto& [x, k] : rows) {
            for (int a = 0; a <= kStubSides; a++) {
                const double angle = 2.0 * 3.14159265358979323846 * a / kStubSides;
                const V3 radial{0.0, std::cos(angle), std::sin(angle)};
                add({x, cy + radial.y * kStubRadius * k, cz + radial.z * kStubRadius * k}, radial, static_cast<double>(a) / kStubSides);
            }
        }
        const uint32_t ring = kStubSides + 1;
        for (uint32_t r = 0; r + 1 < rows.size(); r++) {
            for (uint32_t a = 0; a < static_cast<uint32_t>(kStubSides); a++) {
                const uint32_t i0 = first + r * ring + a, i1 = i0 + 1, j0 = i0 + ring, j1 = i1 + ring;
                // x runs toward -x along the rows: outward is (i0, j1, i1)
                triangles.push_back({i0, j1, i1});
                triangles.push_back({i0, j0, j1});
            }
        }
        const auto centre = static_cast<uint32_t>(vertices.size());
        add({kStubEnd - 0.004, cy, cz}, {0, 0, 0}, 0.5);
        const uint32_t last = first + static_cast<uint32_t>(rows.size() - 1) * ring;
        for (uint32_t a = 0; a < static_cast<uint32_t>(kStubSides); a++) {
            triangles.push_back({last + a, centre, last + a + 1});
        }
    }

    // ---- rounder: such a model is drawn small on a screen, this one a hand's breadth from
    // the eyes. Each triangle becomes four: on the hand a new corner is lifted toward the
    // surface its neighbours' normals describe; on the vambrace and its bands (turned shapes)
    // it is put on the circle between its two neighbours, the bands twice over. ----
    {
        const auto key_of = [](const V3& p) {
            return std::make_tuple(std::llround(p.x * 1e6), std::llround(p.y * 1e6), std::llround(p.z * 1e6));
        };
        const auto only_forearm = [](const Vertex& v) { return v.weights.size() == 1 && v.weights[0].first == 0; };
        for (int round = 0; round < 2; round++) {
            // Welded places: the smooth normal there, and which turned piece it belongs to.
            std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> weld;
            std::vector<uint32_t> at(vertices.size());
            std::vector<V3> smooth;
            for (std::size_t i = 0; i < vertices.size(); i++) {
                const auto [it, added] = weld.try_emplace(key_of(vertices[i].position), static_cast<uint32_t>(smooth.size()));
                if (added) {
                    smooth.emplace_back();
                }
                at[i] = it->second;
                smooth[it->second] = smooth[it->second] + vertices[i].normal;
            }
            for (V3& n : smooth) {
                const double l = Length(n);
                n = l > 1e-9 ? n * (1.0 / l) : V3{0, 1, 0};
            }
            UnionFind pieces(smooth.size());
            for (const auto& t : triangles) {
                pieces.Join(at[t[0]], at[t[1]]);
                pieces.Join(at[t[0]], at[t[2]]);
            }
            std::map<uint32_t, std::array<double, 3>> centre; // sum y, sum z, count
            for (std::size_t i = 0; i < smooth.size(); i++) {
                centre[pieces.Find(static_cast<uint32_t>(i))][2] += 1.0;
            }
            {
                std::vector<bool> counted(smooth.size(), false);
                for (std::size_t i = 0; i < vertices.size(); i++) {
                    if (!counted[at[i]]) {
                        counted[at[i]] = true;
                        auto& c = centre[pieces.Find(at[i])];
                        c[0] += vertices[i].position.y;
                        c[1] += vertices[i].position.z;
                    }
                }
            }
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> mids;
            std::vector<std::array<uint32_t, 3>> next;
            next.reserve(triangles.size() * 4);
            for (const auto& t : triangles) {
                const Vertex& a0 = vertices[t[0]];
                const bool turned = !a0.stub && !a0.plate && a0.tex != kSkinTex && only_forearm(a0) && only_forearm(vertices[t[1]]) &&
                                    only_forearm(vertices[t[2]]);
                const bool gold = turned && a0.tex == kMetalTex;
                const bool hand = !a0.stub && a0.tex == kSkinTex;
                if (!((round == 0 && (turned || hand)) || (round == 1 && gold)) || vertices.size() + 3 > kPitModelMaxVertices) {
                    next.push_back(t);
                    continue;
                }
                std::array<uint32_t, 3> m{};
                for (std::size_t k = 0; k < 3; k++) {
                    const uint32_t i = t[k], j = t[(k + 1) % 3];
                    const auto [it, added] = mids.try_emplace({std::min(i, j), std::max(i, j)}, static_cast<uint32_t>(vertices.size()));
                    m[k] = it->second;
                    if (!added) {
                        continue;
                    }
                    const Vertex& a = vertices[i];
                    const Vertex& b = vertices[j];
                    Vertex v;
                    v.tex = a.tex;
                    v.plate = a.plate;
                    v.uv = {0.5 * (a.uv[0] + b.uv[0]), 0.5 * (a.uv[1] + b.uv[1])};
                    std::map<int, double> w;
                    for (const auto& [joint_index, weight] : a.weights) {
                        w[joint_index] += 0.5 * weight;
                    }
                    for (const auto& [joint_index, weight] : b.weights) {
                        w[joint_index] += 0.5 * weight;
                    }
                    v.weights.assign(w.begin(), w.end());
                    const V3 mid = (a.position + b.position) * 0.5;
                    if (turned) {
                        const auto& c = centre[pieces.Find(at[i])];
                        const double cy = c[0] / c[2], cz = c[1] / c[2];
                        const double ra = std::hypot(a.position.y - cy, a.position.z - cz), rb = std::hypot(b.position.y - cy, b.position.z - cz);
                        const double ta = std::atan2(a.position.z - cz, a.position.y - cy), tb = std::atan2(b.position.z - cz, b.position.y - cy);
                        double turn = tb - ta;
                        const double pi = 3.14159265358979323846;
                        turn -= 2.0 * pi * std::floor((turn + pi) / (2.0 * pi));
                        double angle = ta + 0.5 * turn;
                        if (std::min(ra, rb) < 0.15 * std::max(ra, rb)) { // toward a point on the axis
                            angle = ra > rb ? ta : tb;
                            turn = 0.0;
                        }
                        const double r = 0.5 * (ra + rb);
                        v.position = {mid.x, cy + r * std::cos(angle), cz + r * std::sin(angle)};
                        const auto spin = [](V3 n, double by) {
                            const double c2 = std::cos(by), s2 = std::sin(by);
                            return V3{n.x, n.y * c2 - n.z * s2, n.y * s2 + n.z * c2};
                        };
                        const V3 n = spin(a.normal, 0.5 * turn) + spin(b.normal, -0.5 * turn);
                        v.normal = Length(n) > 1e-9 ? Unit(n) : a.normal;
                    } else {
                        const V3 na = smooth[at[i]], nb = smooth[at[j]];
                        const V3 on_a = mid - na * Dot(mid - a.position, na), on_b = mid - nb * Dot(mid - b.position, nb);
                        v.position = mid + ((on_a + on_b) * 0.5 - mid) * 0.75;
                        const V3 n = a.normal + b.normal;
                        v.normal = Length(n) > 1e-9 ? Unit(n) : a.normal;
                    }
                    vertices.push_back(std::move(v));
                }
                next.push_back({t[0], m[0], m[2]});
                next.push_back({m[0], t[1], m[1]});
                next.push_back({m[2], m[1], t[2]});
                next.push_back({m[0], m[1], m[2]});
            }
            triangles = std::move(next);
        }
    }

    // ---- the pictures: every island of each picture cut out and packed into one ----
    ask("Reading the pictures.", 0.4f);
    std::array<std::array<Image, 4>, 3> maps;
    for (std::size_t t = 0; t < 3; t++) {
        for (std::size_t k = 0; k < 4; k++) {
            const std::string name = std::string(kMeshes[t].images) + "_" + std::string(kMaps[k]) + ".png";
            const Member* m = member(folder + "images/" + name);
            if (!m) {
                Fail("The zip is missing a picture: " + name + ".");
            }
            maps[t][k] = DecodePng(zip.Read(*m), name);
            if (maps[t][k].width != maps[t][0].width || maps[t][k].height != maps[t][0].height) {
                Fail("A picture could not be read: " + name + ".");
            }
            ask("Reading the pictures.", 0.4f + 0.03f * static_cast<float>(t * 4 + k + 1));
        }
    }
    ask("Building the hand.", 0.8f);
    struct Rect {
        std::vector<uint32_t> vertices;
        int tex = 0;
        bool stub = false;
        int64_t x0 = 0, y0 = 0, w = 0, h = 0, ax = 0, ay = 0;
        std::size_t order = 0;
    };
    std::vector<Rect> rects;
    {
        UnionFind islands(vertices.size());
        for (const auto& t : triangles) {
            islands.Join(t[0], t[1]);
            islands.Join(t[0], t[2]);
        }
        std::map<uint32_t, std::size_t> of;
        Rect stub;
        stub.stub = true;
        stub.w = 24;
        stub.h = 72;
        for (uint32_t i = 0; i < vertices.size(); i++) {
            if (vertices[i].stub) {
                stub.vertices.push_back(i);
                continue;
            }
            const auto [it, added] = of.try_emplace(islands.Find(i), rects.size());
            if (added) {
                rects.emplace_back();
                rects.back().tex = vertices[i].tex;
            }
            rects[it->second].vertices.push_back(i);
        }
        for (Rect& r : rects) {
            const Image& image = maps[static_cast<std::size_t>(r.tex)][0];
            double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
            for (const uint32_t i : r.vertices) {
                const double px = vertices[i].uv[0] * image.width, py = (1.0 - vertices[i].uv[1]) * image.height;
                lo_x = std::min(lo_x, px);
                hi_x = std::max(hi_x, px);
                lo_y = std::min(lo_y, py);
                hi_y = std::max(hi_y, py);
            }
            // An island wider than a few pictures is not a real one.
            if (!(hi_x - lo_x < 4.0 * image.width) || !(hi_y - lo_y < 4.0 * image.height) || !(std::abs(lo_x) < 1e7) ||
                !(std::abs(lo_y) < 1e7)) {
                BadNumbers();
            }
            r.x0 = static_cast<int64_t>(std::floor(lo_x)) - kPad;
            r.y0 = static_cast<int64_t>(std::floor(lo_y)) - kPad;
            r.w = static_cast<int64_t>(std::ceil(hi_x)) + kPad - r.x0;
            r.h = static_cast<int64_t>(std::ceil(hi_y)) + kPad - r.y0;
        }
        rects.push_back(std::move(stub));
        for (std::size_t i = 0; i < rects.size(); i++) {
            rects[i].order = i;
        }
        std::stable_sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) { return a.h > b.h; });
    }
    int64_t width = 1024;
    for (const Rect& r : rects) {
        width = std::max(width, r.w);
    }
    width = (width + 63) / 64 * 64;
    int64_t x = 0, y = 0, shelf = 0;
    for (Rect& r : rects) {
        if (x + r.w > width) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        r.ax = x;
        r.ay = y;
        x += r.w;
        shelf = std::max(shelf, r.h);
    }
    const int64_t height = (y + shelf + 63) / 64 * 64;
    if (width > kPitModelMaxTexture || height > kPitModelMaxTexture || height <= 0) {
        BadNumbers();
    }
    const std::size_t pixels = static_cast<std::size_t>(width * height);
    std::vector<uint8_t> colour(pixels * 4, 0), surface(pixels * 4, 0);
    for (std::size_t i = 0; i < pixels; i++) {
        colour[i * 4 + 3] = 255;
        surface[i * 4] = 128;
        surface[i * 4 + 1] = 128;
        surface[i * 4 + 2] = 255;
        surface[i * 4 + 3] = kSkin;
    }
    std::vector<std::array<double, 2>> uv(vertices.size());
    std::array<double, 3> tone{240, 205, 175};
    std::size_t tone_count = 0;
    for (const Rect& r : rects) {
        if (r.stub) {
            continue;
        }
        const auto& set = maps[static_cast<std::size_t>(r.tex)];
        std::array<std::vector<double>, 3> skin_texels;
        for (int64_t row = 0; row < r.h; row++) {
            for (int64_t col = 0; col < r.w; col++) {
                const int64_t sx = r.x0 + col, sy = r.y0 + row;
                const uint8_t* c = set[0].At(sx, sy);
                const uint8_t* n = set[1].At(sx, sy);
                const uint8_t ao = set[2].At(sx, sy)[0];
                const double metal = set[3].At(sx, sy)[0] / 255.0;
                double code = kLeather;
                if (r.tex == kSkinTex) {
                    // the glove against the skin, by the map that marks skin
                    code = kCloth + (kSkin - kCloth) * std::clamp((metal - 0.35) / 0.3, 0.0, 1.0);
                    if (metal > 0.8 && ao > 200) {
                        for (std::size_t k = 0; k < 3; k++) {
                            skin_texels[k].push_back(c[k]);
                        }
                    }
                } else if (r.tex == kMetalTex) {
                    // the plate's red inset against the gold round it, by colour
                    const double red = c[0] / (c[1] + 8.0);
                    code = kGold + (kEnamel - kGold) * std::clamp((red - 1.55) / 0.35, 0.0, 1.0);
                }
                const std::size_t at = static_cast<std::size_t>((r.ay + row) * width + r.ax + col) * 4;
                colour[at] = c[0];
                colour[at + 1] = c[1];
                colour[at + 2] = c[2];
                colour[at + 3] = ao;
                surface[at] = n[0];
                surface[at + 1] = n[1];
                surface[at + 2] = 255;
                surface[at + 3] = static_cast<uint8_t>(std::lround(code));
            }
        }
        const Image& image = set[0];
        for (const uint32_t i : r.vertices) {
            const double px = vertices[i].uv[0] * image.width, py = (1.0 - vertices[i].uv[1]) * image.height;
            uv[i] = {(static_cast<double>(r.ax) + px - static_cast<double>(r.x0)) / static_cast<double>(width),
                     (static_cast<double>(r.ay) + py - static_cast<double>(r.y0)) / static_cast<double>(height)};
        }
        if (skin_texels[0].size() > 200 && skin_texels[0].size() > tone_count) {
            tone_count = skin_texels[0].size();
            for (std::size_t k = 0; k < 3; k++) {
                tone[k] = Median(std::move(skin_texels[k]));
            }
        }
    }
    // The skin's picture is pale, and grey where the model's own lighting was meant to warm it:
    // every skin texel takes one warm skin hue at its own (a little evened) brightness; the
    // gold's picture loses most of its wear, so it is a clean yellow for the shader to light.
    for (std::size_t k = 0; k < 3; k++) {
        constexpr std::array<double, 3> warm{1.05, 0.94, 0.82};
        tone[k] = std::clamp(tone[k] * warm[k], 0.0, 255.0);
    }
    {
        const double tone_light = 0.30 * tone[0] + 0.59 * tone[1] + 0.11 * tone[2];
        constexpr std::array<double, 3> clean_gold{238, 198, 72};
        for (const Rect& r : rects) {
            if (r.stub || r.tex == kSleeveTex || !(tone_light > 1.0)) {
                continue;
            }
            for (int64_t row = 0; row < r.h; row++) {
                for (int64_t col = 0; col < r.w; col++) {
                    const std::size_t at = static_cast<std::size_t>((r.ay + row) * width + r.ax + col) * 4;
                    const double light = 0.30 * colour[at] + 0.59 * colour[at + 1] + 0.11 * colour[at + 2];
                    if (r.tex == kMetalTex) {
                        const double share = std::clamp((static_cast<double>(kEnamel) - surface[at + 3]) / (kEnamel - kGold), 0.0, 1.0) * 0.7;
                        const double level = std::clamp(0.75 + 0.25 * light / 150.0, 0.6, 1.1);
                        for (std::size_t k = 0; k < 3; k++) {
                            colour[at + k] = static_cast<uint8_t>(
                                std::lround(std::clamp(colour[at + k] + (clean_gold[k] * level - colour[at + k]) * share, 0.0, 255.0)));
                        }
                        continue;
                    }
                    const double share = std::clamp((static_cast<double>(kCloth) - surface[at + 3]) / (kCloth - kSkin), 0.0, 1.0) * 0.9;
                    if (share <= 0.0) {
                        continue;
                    }
                    const double evened = tone_light + (light - tone_light) * 0.75;
                    for (std::size_t k = 0; k < 3; k++) {
                        const double warm = tone[k] * evened / tone_light;
                        colour[at + k] = static_cast<uint8_t>(std::lround(std::clamp(colour[at + k] + (warm - colour[at + k]) * share, 0.0, 255.0)));
                    }
                }
            }
        }
    }
    // Rosy finger tips and knuckles, painted where each texel is on the hand: every skin
    // triangle is walked over its texels, each texel's place found, and its nearness to a tip
    // (all round) or to a joint (on the back of the finger) reddens it.
    {
        std::vector<std::pair<V3, double>> tips, knuckles; // place, reach
        for (std::size_t j = 0; j < kJoints.size(); j++) {
            const std::string_view name = kJoints[j].name;
            if (name.ends_with("_eff")) {
                tips.emplace_back(joint[j].at, 0.015);
            } else if (!kJoints[j].relative) {
                knuckles.emplace_back(joint[j].at, name.ends_with("1") ? 0.014 : 0.011);
            }
        }
        std::vector<float> rosy(pixels, 0.0f);
        for (const auto& t : triangles) {
            const Vertex& a = vertices[t[0]];
            if (a.tex != kSkinTex || a.stub) {
                continue;
            }
            const Vertex& b = vertices[t[1]];
            const Vertex& c = vertices[t[2]];
            const double ax = uv[t[0]][0] * static_cast<double>(width), ay = uv[t[0]][1] * static_cast<double>(height);
            const double bx = uv[t[1]][0] * static_cast<double>(width), by = uv[t[1]][1] * static_cast<double>(height);
            const double cx = uv[t[2]][0] * static_cast<double>(width), cy = uv[t[2]][1] * static_cast<double>(height);
            const double det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (std::abs(det) < 1e-9) {
                continue;
            }
            const auto x0 = static_cast<int64_t>(std::floor(std::min({ax, bx, cx}))) - 1, x1 = static_cast<int64_t>(std::ceil(std::max({ax, bx, cx}))) + 1;
            const auto y0 = static_cast<int64_t>(std::floor(std::min({ay, by, cy}))) - 1, y1 = static_cast<int64_t>(std::ceil(std::max({ay, by, cy}))) + 1;
            if (x1 - x0 > 2048 || y1 - y0 > 2048) {
                continue;
            }
            for (int64_t py = std::max<int64_t>(y0, 0); py <= std::min<int64_t>(y1, height - 1); py++) {
                for (int64_t px = std::max<int64_t>(x0, 0); px <= std::min<int64_t>(x1, width - 1); px++) {
                    const double qx = static_cast<double>(px) + 0.5, qy = static_cast<double>(py) + 0.5;
                    double w1 = ((qx - ax) * (cy - ay) - (cx - ax) * (qy - ay)) / det;
                    double w2 = ((bx - ax) * (qy - ay) - (qx - ax) * (by - ay)) / det;
                    double w0 = 1.0 - w1 - w2;
                    if (w0 < -0.6 || w1 < -0.6 || w2 < -0.6) {
                        continue; // a texel or so of margin, for the filter
                    }
                    w0 = std::clamp(w0, 0.0, 1.0);
                    w1 = std::clamp(w1, 0.0, 1.0);
                    w2 = std::clamp(w2, 0.0, 1.0);
                    const double sum = std::max(w0 + w1 + w2, 1e-9);
                    const V3 at = (a.position * w0 + b.position * w1 + c.position * w2) * (1.0 / sum);
                    const V3 n = (a.normal * w0 + b.normal * w1 + c.normal * w2) * (1.0 / sum);
                    double amount = 0.0;
                    for (const auto& [place, far] : tips) {
                        const double d = Length(at - place) / far;
                        amount = std::max(amount, 0.85 * std::exp(-d * d));
                    }
                    const double back = std::clamp(n.y * 2.0 + 0.3, 0.0, 1.0); // the back of the hand is +y
                    for (const auto& [place, far] : knuckles) {
                        const double d = Length(at - place) / far;
                        amount = std::max(amount, 0.6 * back * std::exp(-d * d));
                    }
                    float& slot = rosy[static_cast<std::size_t>(py * width + px)];
                    slot = std::max(slot, static_cast<float>(amount));
                }
            }
        }
        for (std::size_t i = 0; i < pixels; i++) {
            if (rosy[i] <= 0.0f) {
                continue;
            }
            const double share = std::clamp((static_cast<double>(kCloth) - surface[i * 4 + 3]) / (kCloth - kSkin), 0.0, 1.0) * rosy[i];
            constexpr std::array<double, 3> pink{1.0, 0.80, 0.78};
            for (std::size_t k = 0; k < 3; k++) {
                colour[i * 4 + k] = static_cast<uint8_t>(std::lround(colour[i * 4 + k] * (1.0 + (pink[k] - 1.0) * share)));
            }
        }
    }
    for (const Rect& r : rects) {
        if (!r.stub) {
            continue;
        }
        // The stub's patch: plain skin, solid at the vambrace, clear at its end.
        for (int64_t row = 0; row < r.h; row++) {
            const double fade = std::clamp(static_cast<double>(row - kPad) / static_cast<double>(r.h - 2 * kPad - 1), 0.0, 1.0);
            for (int64_t col = 0; col < r.w; col++) {
                const std::size_t at = static_cast<std::size_t>((r.ay + row) * width + r.ax + col) * 4;
                for (std::size_t k = 0; k < 3; k++) {
                    colour[at + k] = static_cast<uint8_t>(std::lround(std::clamp(tone[k], 0.0, 255.0)));
                }
                colour[at + 3] = 235;
                surface[at + 2] = static_cast<uint8_t>(std::lround(255.0 * fade));
                surface[at + 3] = kSkin;
            }
        }
        for (const uint32_t i : r.vertices) {
            uv[i] = {(static_cast<double>(r.ax + kPad) + vertices[i].fade[0] * static_cast<double>(r.w - 2 * kPad - 1) + 0.5) /
                         static_cast<double>(width),
                     (static_cast<double>(r.ay + kPad) + vertices[i].fade[1] * static_cast<double>(r.h - 2 * kPad - 1) + 0.5) /
                         static_cast<double>(height)};
        }
    }
    ask("Building the hand.", 0.88f);

    // Tangents: the way u grows, square to the normal, and which side v-up lies.
    std::vector<V3> tangent(vertices.size()), bitangent(vertices.size());
    for (const auto& t : triangles) {
        const V3 e1 = vertices[t[1]].position - vertices[t[0]].position, e2 = vertices[t[2]].position - vertices[t[0]].position;
        const double u1 = uv[t[1]][0] - uv[t[0]][0], v1 = -(uv[t[1]][1] - uv[t[0]][1]);
        const double u2 = uv[t[2]][0] - uv[t[0]][0], v2 = -(uv[t[2]][1] - uv[t[0]][1]);
        const double det = u1 * v2 - u2 * v1;
        if (std::abs(det) < 1e-14) {
            continue;
        }
        const V3 tan = (e1 * v2 - e2 * v1) * (1.0 / det), bit = (e2 * u1 - e1 * u2) * (1.0 / det);
        for (const uint32_t i : t) {
            tangent[i] = tangent[i] + tan;
            bitangent[i] = bitangent[i] + bit;
        }
    }
    std::vector<double> handed(vertices.size(), 1.0);
    for (std::size_t i = 0; i < vertices.size(); i++) {
        const V3 n = vertices[i].normal;
        V3 t = tangent[i] - n * Dot(tangent[i], n);
        if (!(Length(t) > 1e-9) || !std::isfinite(Length(t))) {
            t = Cross(n, {0, 0, 1});
            if (Length(t) < 0.1) {
                t = Cross(n, {0, 1, 0});
            }
        }
        tangent[i] = Unit(t);
        handed[i] = Dot(Cross(n, tangent[i]), bitangent[i]) < 0.0 ? -1.0 : 1.0;
    }

    // The plate's triangles last.
    std::vector<uint32_t> indices;
    indices.reserve(triangles.size() * 3);
    uint32_t body = 0, plate = 0;
    for (const bool want : {false, true}) {
        for (const auto& t : triangles) {
            if (vertices[t[0]].plate == want) {
                indices.insert(indices.end(), t.begin(), t.end());
                (want ? plate : body) += 3;
            }
        }
    }

    // Where the handle lies in the palm, and the fit that puts the controller's on it.
    const double tilt = kGripTiltDegrees * 3.14159265358979323846 / 180.0;
    const V3 axis{std::sin(tilt), 0.0, std::cos(tilt)};
    const double grip_x = joint[kMiddleKnuckle].at.x - kGripBehindKnuckles;
    std::vector<double> low;
    for (int s = 0; s <= 10; s++) {
        const V3 c = V3{grip_x, 0, 0} + axis * (-0.03 + 0.005 * s);
        double least = 1e300;
        for (const Vertex& v : vertices) {
            if (v.tex == kSkinTex && !v.stub && std::abs(v.position.x - c.x) < 0.012 && std::abs(v.position.z - c.z) < 0.012) {
                least = std::min(least, v.position.y);
            }
        }
        if (least < 1e299) {
            low.push_back(least);
        }
    }
    if (low.empty()) {
        Fail("The model has no left hand.");
    }
    const V3 centre{grip_x, Median(low) - kHandleRadius + 0.0015, 0.0};
    const V3 f1 = Unit(kHandleAxis);
    const V3 f2 = Unit(V3{-1, 0, 0} - f1 * Dot(f1, {-1, 0, 0}));
    const V3 f3 = Cross(f1, f2);
    const V3 e1 = axis, e2{0, 1, 0}, e3 = Cross(e1, e2);
    // aim -> arm: r = [e1 e2 e3] [f1 f2 f3]^T; the fit is its inverse, arm -> aim.
    const auto to_aim = [&](V3 v) { return f1 * Dot(e1, v) + f2 * Dot(e2, v) + f3 * Dot(e3, v); };
    const V3 pivot = kHandleCentre + f1 * kHandleSlide;
    const V3 shift = pivot - to_aim(centre);

    // How thick the flesh is round each finger joint: its own vertices' distance from its bone.
    std::array<double, kJoints.size()> thickness{};
    for (std::size_t j = 0; j < kJoints.size(); j++) {
        if (kJoints[j].relative) {
            continue;
        }
        int child = -1;
        for (std::size_t k = 0; k < kJoints.size(); k++) {
            if (kJoints[k].parent == static_cast<int>(j)) {
                child = static_cast<int>(k);
            }
        }
        if (child < 0) {
            continue;
        }
        const V3 a = joint[j].at, b = joint[static_cast<std::size_t>(child)].at;
        const double length = Length(b - a);
        if (!(length > 1e-9)) {
            continue;
        }
        const V3 along = (b - a) * (1.0 / length);
        std::vector<double> around;
        for (const Vertex& v : vertices) {
            const auto most = std::max_element(v.weights.begin(), v.weights.end(), [](const auto& p, const auto& q) { return p.second < q.second; });
            if (most->first != static_cast<int>(j)) {
                continue;
            }
            const double t = Dot(v.position - a, along);
            if (t > 0.0 && t < length) {
                around.push_back(Length(v.position - a - along * t));
            }
        }
        if (around.size() >= 3) {
            thickness[j] = Median(std::move(around));
        }
    }
    for (std::size_t j = 0; j < kJoints.size(); j++) { // a tip's is its last joint's, a little less
        if (kJoints[j].name.ends_with("_eff")) {
            thickness[j] = thickness[static_cast<std::size_t>(kJoints[j].parent)] * 0.85;
        }
    }
    // The forearm's reach: the largest distance from its line of what hangs on it.
    double forearm_radius = 0.0, wrist_radius = 0.0;
    for (const Vertex& v : vertices) {
        const auto most = std::max_element(v.weights.begin(), v.weights.end(), [](const auto& p, const auto& q) { return p.second < q.second; });
        if (most->first != 0) {
            continue;
        }
        const double r = std::hypot(v.position.y, v.position.z);
        forearm_radius = std::max(forearm_radius, r);
        if (v.position.x > -0.06) {
            wrist_radius = std::max(wrist_radius, r);
        }
    }

    // ---- the file (pit_model.h) ----
    ask("Saving the hand.", 0.94f);
    std::vector<uint8_t> out;
    out.reserve(256 + vertices.size() * 56 + indices.size() * 4 + pixels * 8);
    const char magic[8] = {'P', 'I', 'T', 'H', 'A', 'N', 'D', '1'};
    out.insert(out.end(), magic, magic + 8);
    for (const uint32_t v : {uint32_t{1}, static_cast<uint32_t>(vertices.size()), body, plate, static_cast<uint32_t>(kJoints.size()),
                             static_cast<uint32_t>(width), static_cast<uint32_t>(height), uint32_t{0}}) {
        Put<uint32_t>(out, v);
    }
    for (const double v : {Length(joint[kMiddleTip].at), forearm_radius, wrist_radius, 0.0, elbow.x, elbow.y, elbow.z, centre.x, centre.y,
                           centre.z, axis.x, axis.y, axis.z, kHandleRadius}) {
        PutF(out, v);
    }
    // arm -> aim, column major: the columns are the arm's axes in aim space.
    for (const V3 column : {to_aim({1, 0, 0}), to_aim({0, 1, 0}), to_aim({0, 0, 1})}) {
        for (const double v : {column.x, column.y, column.z, 0.0}) {
            PutF(out, v);
        }
    }
    for (const double v : {shift.x, shift.y, shift.z, 1.0, pivot.x, pivot.y, pivot.z}) {
        PutF(out, v);
    }
    for (std::size_t j = 0; j < kJoints.size(); j++) {
        Put<int32_t>(out, kJoints[j].parent);
        Put<int32_t>(out, kJoints[j].role);
        Put<uint32_t>(out, kJoints[j].relative ? 1u : 0u);
        for (const double v : {joint[j].at.x, joint[j].at.y, joint[j].at.z}) {
            PutF(out, v);
        }
        // Its axes as the app's joints have them: X to the right, Y the back of the hand, +Z
        // back along the bone (the model's: x along the bone, y the palm's side), made square.
        const V3 back = joint[j].axis[0] * -1.0;
        V3 up = joint[j].axis[1] * -1.0;
        up = Unit(up - back * Dot(up, back));
        const V3 right = Cross(up, back);
        for (const double v : {right.x, up.x, back.x, right.y, up.y, back.y, right.z, up.z, back.z}) {
            PutF(out, v);
        }
        PutF(out, thickness[j]);
    }
    for (std::size_t i = 0; i < vertices.size(); i++) {
        const Vertex& v = vertices[i];
        for (const double f : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, tangent[i].x, tangent[i].y,
                               tangent[i].z, handed[i], uv[i][0], uv[i][1]}) {
            PutF(out, f);
        }
    }
    for (const Vertex& v : vertices) {
        // At most four joints, as bytes that sum to 255, the heaviest first.
        std::vector<std::pair<int, double>> w = v.weights;
        std::stable_sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (w.size() > 4) {
            w.resize(4);
        }
        double total = 0.0;
        for (const auto& p : w) {
            total += p.second;
        }
        std::array<int, 4> bytes{};
        int sum = 0;
        for (std::size_t k = 0; k < w.size(); k++) {
            bytes[k] = static_cast<int>(std::lround(255.0 * w[k].second / total));
            sum += bytes[k];
        }
        bytes[0] += 255 - sum;
        std::array<std::pair<int, int>, 4> slots{};
        for (std::size_t k = 0; k < 4; k++) {
            slots[k] = {k < w.size() ? bytes[k] : 0, k < w.size() ? w[k].first : w[0].first};
        }
        std::stable_sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& slot : slots) {
            out.push_back(static_cast<uint8_t>(slot.second));
        }
        for (const auto& slot : slots) {
            out.push_back(static_cast<uint8_t>(std::clamp(slot.first, 0, 255)));
        }
    }
    for (const uint32_t i : indices) {
        Put<uint32_t>(out, i);
    }
    out.insert(out.end(), colour.begin(), colour.end());
    out.insert(out.end(), surface.begin(), surface.end());
    // Once more through the app's own reader: what is handed back is a file it takes.
    PitModel check;
    std::string error;
    if (!ParsePitModel(out, check, error)) {
        Fail("The hand that came out was not right.");
    }
    ask("Saving the hand.", 1.0f);
    return out;
}

} // namespace

std::string ImportHand(std::span<const uint8_t> zip, std::vector<uint8_t>& out, const HandImportProgress& progress) {
    out.clear();
    try {
        out = Build(zip, progress);
        return {};
    } catch (const Stop& stop) {
        out.clear();
        return stop.why;
    } catch (const std::bad_alloc&) {
        out.clear();
        return "The zip is too big.";
    }
}

std::string ImportHandFile(const std::string& zip_path, const std::string& user_directory, const HandImportProgress& progress) {
    std::vector<uint8_t> zip;
    {
        std::ifstream in(zip_path, std::ios::binary | std::ios::ate);
        if (!in) {
            return "The zip could not be read.";
        }
        const std::streamoff size = in.tellg();
        if (size < 0) {
            return "The zip could not be read.";
        }
        if (static_cast<uint64_t>(size) > kHandZipMaxBytes) {
            return "The zip is too big.";
        }
        zip.resize(static_cast<std::size_t>(size));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(zip.data()), size);
        if (!in || in.gcount() != size) {
            return "The zip could not be read.";
        }
    }
    std::vector<uint8_t> hand;
    if (std::string error = ImportHand(zip, hand, progress); !error.empty()) {
        return error;
    }
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::path(user_directory) / "kiu_hand";
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path target = folder / "pit_hand.bin";
    const std::filesystem::path beside = folder / "pit_hand.bin.new";
    {
        std::ofstream file(beside, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(hand.data()), static_cast<std::streamsize>(hand.size()));
        file.flush();
        if (!file) {
            std::filesystem::remove(beside, ec);
            return "The hand could not be saved.";
        }
    }
    std::filesystem::rename(beside, target, ec);
    if (ec) {
        std::filesystem::remove(beside, ec);
        return "The hand could not be saved.";
    }
    return {};
}

std::vector<std::string> ListHandZips(const std::string& user_directory) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::path(user_directory) / "kiu_hand";
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".zip" && it->is_regular_file(ec)) {
            out.push_back(it->path().string());
        }
        if (out.size() >= 256) {
            break;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace FrameXr::Vis
