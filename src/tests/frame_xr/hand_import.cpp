// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: the hand importer (frame_xr/vis/hand_import.h): a zip with the parts it
// looks for becomes a hand file the app reads; one that lacks them, lies about its sizes, steps
// out of its folder, repeats a name, or whose model file carries a document type or an entity is
// refused with a plain sentence; and no damage to a zip makes it read past the end or crash. The
// zip here is made in the test (a few triangles, 4 x 4 pictures): no real model is in the tree.

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <lodepng.h>

#include "frame_xr/vis/hand_import.h"
#include "frame_xr/vis/pit_model.h"

using namespace FrameXr::Vis;

namespace {

struct File {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t claimed_size = 0; ///< above 0: what the directory says it unpacks to
    uint16_t method = 0;
    std::vector<uint8_t> crc_of{}; ///< not empty: the unpacked bytes the checksum is of
};

void Put16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>& out, uint32_t v) {
    Put16(out, v);
    Put16(out, v >> 16);
}

/// A zip of stored members.
std::vector<uint8_t> Zip(const std::vector<File>& files) {
    std::vector<uint8_t> out, directory;
    for (const File& f : files) {
        const uint32_t crc = f.crc_of.empty() ? lodepng_crc32(f.data.data(), f.data.size()) : lodepng_crc32(f.crc_of.data(), f.crc_of.size());
        const auto size = static_cast<uint32_t>(f.data.size());
        const uint32_t unpacked = f.claimed_size ? f.claimed_size : size;
        const auto offset = static_cast<uint32_t>(out.size());
        Put32(out, 0x04034b50u);
        for (const uint32_t v : {20u, 0u, uint32_t{f.method}, 0u, 0u}) {
            Put16(out, v);
        }
        Put32(out, crc);
        Put32(out, size);
        Put32(out, unpacked);
        Put16(out, static_cast<uint32_t>(f.name.size()));
        Put16(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), f.data.begin(), f.data.end());
        Put32(directory, 0x02014b50u);
        for (const uint32_t v : {20u, 20u, 0u, uint32_t{f.method}, 0u, 0u}) {
            Put16(directory, v);
        }
        Put32(directory, crc);
        Put32(directory, size);
        Put32(directory, unpacked);
        Put16(directory, static_cast<uint32_t>(f.name.size()));
        for (const uint32_t v : {0u, 0u, 0u, 0u}) {
            Put16(directory, v);
        }
        Put32(directory, 0);
        Put32(directory, offset);
        directory.insert(directory.end(), f.name.begin(), f.name.end());
    }
    const auto at = static_cast<uint32_t>(out.size());
    out.insert(out.end(), directory.begin(), directory.end());
    Put32(out, 0x06054b50u);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, static_cast<uint32_t>(files.size()));
    Put16(out, static_cast<uint32_t>(files.size()));
    Put32(out, static_cast<uint32_t>(directory.size()));
    Put32(out, at);
    Put16(out, 0);
    return out;
}

std::vector<uint8_t> Png(uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> pixels(4 * 4 * 4);
    for (std::size_t i = 0; i < 16; i++) {
        pixels[i * 4] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = 255;
    }
    unsigned char* png = nullptr;
    std::size_t size = 0;
    REQUIRE(lodepng_encode32(&png, &size, pixels.data(), 4, 4) == 0);
    std::vector<uint8_t> out(png, png + size);
    std::free(png);
    return out;
}

// The joints a model must have (hand_import.cpp), where they are in the made-up model's own
// units, and three it has besides.
struct J {
    const char* name;
    float x, y, z;
};
const std::vector<J> kModelJoints{
    {"ArmL", -2.6f, 0, 0},      {"HandL", 0, 0, 0},
    {"FingerL10", 0.1f, 0, 0.4f}, {"FingerL11", 1.2f, 0, 0.5f}, {"FingerL12", 1.9f, 0, 0.5f}, {"FingerL13", 2.2f, 0, 0.5f}, {"FingerL14_eff", 2.5f, 0, 0.5f},
    {"FingerL20", 0.1f, 0, 0.1f}, {"FingerL21", 1.3f, 0, 0.1f}, {"FingerL22", 2.0f, 0, 0.1f}, {"FingerL23", 2.3f, 0, 0.1f}, {"FingerL24_eff", 2.6f, 0, 0.1f},
    {"FingerL30", 0.1f, 0, -0.2f}, {"FingerL31", 1.2f, 0, -0.2f}, {"FingerL32", 1.9f, 0, -0.2f}, {"FingerL33", 2.2f, 0, -0.2f}, {"FingerL34_eff", 2.5f, 0, -0.2f},
    {"FingerL40", 0.1f, 0, -0.5f}, {"FingerL41", 1.1f, 0, -0.5f}, {"FingerL42", 1.6f, 0, -0.5f}, {"FingerL43", 1.9f, 0, -0.5f}, {"FingerL44_eff", 2.2f, 0, -0.5f},
    {"FingerL51", 0.1f, -0.2f, 0.4f}, {"FingerL52", 0.7f, -0.3f, 0.9f}, {"FingerL53", 1.4f, -0.3f, 1.0f}, {"FingerL54_eff", 1.8f, -0.3f, 1.0f},
    {"Ring1", -1.5f, 0, 0},     {"Ring2", -1.0f, 0, 0},         {"ShoulderL", -4.0f, 0, 0}};

struct Tri {
    float p[9];
    const char* joint;
};

/// A geometry and its skin: each triangle hangs whole on one joint.
std::string Geometry(const std::string& id, const std::vector<Tri>& tris, int bad_index) {
    std::string s = "<geometry id=\"" + id + "\"><mesh><source id=\"" + id + "-p\"><float_array count=\"" +
                    std::to_string(tris.size() * 9) + "\">";
    for (const Tri& t : tris) {
        for (const float v : t.p) {
            s += std::to_string(v) + " ";
        }
    }
    s += "</float_array><technique_common><accessor stride=\"3\"/></technique_common></source>";
    s += "<source id=\"" + id + "-n\"><float_array count=\"3\">0 1 0</float_array><technique_common><accessor stride=\"3\"/></technique_common></source>";
    s += "<source id=\"" + id + "-t\"><float_array count=\"9\">0.1 0.1 0 0.9 0.1 0 0.5 0.9 0</float_array><technique_common><accessor stride=\"3\"/></technique_common></source>";
    s += "<vertices id=\"" + id + "-v\"><input semantic=\"POSITION\" source=\"#" + id + "-p\"/></vertices>";
    s += "<triangles count=\"" + std::to_string(tris.size()) + "\"><input semantic=\"VERTEX\" source=\"#" + id + "-v\" offset=\"0\"/>";
    s += "<input semantic=\"NORMAL\" source=\"#" + id + "-n\" offset=\"1\"/><input semantic=\"TEXCOORD\" source=\"#" + id + "-t\" offset=\"2\" set=\"0\"/><p>";
    for (std::size_t i = 0; i < tris.size() * 3; i++) {
        const int index = (bad_index >= 0 && i == 1) ? bad_index : static_cast<int>(i);
        s += std::to_string(index) + " 0 " + std::to_string(i % 3) + " ";
    }
    s += "</p></triangles></mesh></geometry>";
    return s;
}

std::string Skin(const std::string& id, const std::vector<Tri>& tris, const std::string& skip_joint) {
    std::string names, binds;
    std::vector<std::string> list;
    for (std::size_t j = 0; j < kModelJoints.size(); j++) {
        if (kModelJoints[j].name == skip_joint) {
            continue;
        }
        list.emplace_back(kModelJoints[j].name);
        names += "joint" + std::to_string(j) + " ";
        // the inverse of a placement at (x, y, z)
        binds += "1 0 0 " + std::to_string(-kModelJoints[j].x) + " 0 1 0 " + std::to_string(-kModelJoints[j].y) + " 0 0 1 " +
                 std::to_string(-kModelJoints[j].z) + " 0 0 0 1 ";
    }
    std::string s = "<controller id=\"" + id + "-skin\"><skin source=\"#" + id + "\"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>";
    s += "<source id=\"" + id + "-j\"><Name_array count=\"" + std::to_string(list.size()) + "\">" + names + "</Name_array></source>";
    s += "<source id=\"" + id + "-b\"><float_array count=\"" + std::to_string(list.size() * 16) + "\">" + binds +
         "</float_array><technique_common><accessor stride=\"16\"/></technique_common></source>";
    s += "<source id=\"" + id + "-w\"><float_array count=\"1\">1</float_array><technique_common><accessor stride=\"1\"/></technique_common></source>";
    s += "<joints><input semantic=\"JOINT\" source=\"#" + id + "-j\"/><input semantic=\"INV_BIND_MATRIX\" source=\"#" + id + "-b\"/></joints>";
    s += "<vertex_weights count=\"" + std::to_string(tris.size() * 3) + "\"><input semantic=\"JOINT\" source=\"#" + id +
         "-j\" offset=\"0\"/><input semantic=\"WEIGHT\" source=\"#" + id + "-w\" offset=\"1\"/><vcount>";
    for (std::size_t i = 0; i < tris.size() * 3; i++) {
        s += "1 ";
    }
    s += "</vcount><v>";
    for (const Tri& t : tris) {
        std::size_t index = 0;
        for (std::size_t j = 0; j < list.size(); j++) {
            if (list[j] == t.joint) {
                index = j;
            }
        }
        for (int k = 0; k < 3; k++) {
            s += std::to_string(index) + " 0 ";
        }
    }
    s += "</v></vertex_weights></skin></controller>";
    return s;
}

struct Options {
    std::string skip_joint;   ///< a joint the model does not have
    std::string head;         ///< put before the root (a document type, say)
    std::string in_root;      ///< put inside the root
    int bad_index = -1;       ///< a corner that points past the positions
    bool no_hand = false;
};

std::string Dae(const Options& o = {}) {
    // The hand's skin: a triangle under the palm where the handle will lie, one on a finger.
    const std::vector<Tri> skin{{{1.0f, -0.3f, -0.1f, 1.1f, -0.3f, 0.1f, 1.05f, -0.3f, 0.0f}, "FingerL10"},
                                {{1.9f, 0.2f, 0.4f, 2.1f, 0.2f, 0.4f, 2.0f, 0.2f, 0.6f}, "FingerL12"}};
    const std::vector<Tri> sleeve{{{-1.5f, 0.9f, 0, -0.8f, 0.9f, 0, -1.2f, 0.9f, 0.4f}, "ArmL"}};
    // The gold: a band, the plate (on the hand's joint), a ring and the upper arm's cuff.
    const std::vector<Tri> gold{{{-0.6f, 1.1f, 0, -0.4f, 1.1f, 0, -0.5f, 1.1f, 0.3f}, "ArmL"},
                                {{0.1f, 0.5f, 0, 0.9f, 0.5f, 0, 0.4f, 0.5f, 0.4f}, "HandL"},
                                {{-1.5f, 1.8f, 0, -1.4f, 1.8f, 0, -1.45f, 1.8f, 0.3f}, "Ring1"},
                                {{-4.0f, 0.8f, 0, -3.8f, 0.8f, 0, -3.9f, 0.8f, 0.3f}, "ShoulderL"}};
    const std::string hand = o.no_hand ? "geom-other" : "geom-arm_L_VIS_O_OBJShape_2";
    std::string s = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" + o.head + "<COLLADA version=\"1.4.1\">" + o.in_root + "<library_geometries>";
    s += Geometry(hand, skin, o.bad_index) + Geometry("geom-arm_L_VIS_O_OBJShape", sleeve, -1) +
         Geometry("geom-arm_L_VIS_O_OBJShape_1", gold, -1);
    s += "</library_geometries><library_controllers>";
    s += Skin(hand, skin, o.skip_joint) + Skin("geom-arm_L_VIS_O_OBJShape", sleeve, o.skip_joint) +
         Skin("geom-arm_L_VIS_O_OBJShape_1", gold, o.skip_joint);
    s += "</library_controllers><library_visual_scenes><visual_scene id=\"s\"><!-- the joints --><node name=\"root\">";
    for (std::size_t j = 0; j < kModelJoints.size(); j++) {
        s += "<node name=\"" + std::string(kModelJoints[j].name) + "\" sid=\"joint" + std::to_string(j) + "\" type=\"JOINT\"/>";
    }
    s += "</node></visual_scene></library_visual_scenes></COLLADA>\n";
    return s;
}

File Stored(const std::string& dae) {
    return {"Model/pit.dae", {dae.begin(), dae.end()}};
}

std::vector<File> Files(const std::string& dae) {
    // The model deflated, as a real zip's is (the pictures stay stored).
    unsigned char* packed = nullptr;
    std::size_t packed_size = 0;
    LodePNGCompressSettings settings;
    lodepng_compress_settings_init(&settings);
    REQUIRE(lodepng_deflate(&packed, &packed_size, reinterpret_cast<const unsigned char*>(dae.data()), dae.size(), &settings) == 0);
    File model{"Model/pit.dae", {packed, packed + packed_size}, static_cast<uint32_t>(dae.size()), 8};
    model.crc_of.assign(dae.begin(), dae.end());
    std::free(packed);
    std::vector<File> files{model};
    for (const char* set : {"skin_pit_001", "def_pit_001", "met_pit_001"}) {
        // "mtl" is white on skin (it marks skin there); the gold's picture is yellow, not red
        files.push_back({std::string("Model/images/") + set + "_col.png", Png(230, 200, 60)});
        files.push_back({std::string("Model/images/") + set + "_nor.png", Png(128, 128, 255)});
        files.push_back({std::string("Model/images/") + set + "_ao.png", Png(255, 255, 255)});
        files.push_back({std::string("Model/images/") + set + "_mtl.png", Png(255, 255, 255)});
    }
    return files;
}

std::string Import(const std::vector<uint8_t>& zip, std::vector<uint8_t>* out = nullptr) {
    std::vector<uint8_t> hand;
    const std::string error = ImportHand(zip, hand);
    CHECK(error.empty() == !hand.empty()); // a sentence or a hand, never both, never neither
    if (out) {
        *out = hand;
    }
    return error;
}

} // namespace

TEST_CASE("Hand importer: a zip with the parts it looks for becomes a hand file", "[kiuhand]") {
    std::vector<uint8_t> hand;
    std::vector<std::string> said;
    const std::string error = ImportHand(Zip(Files(Dae())), hand, [&said](std::string_view doing, float done) {
        CHECK(done >= 0.0f);
        CHECK(done <= 1.0f);
        if (said.empty() || said.back() != doing) {
            said.emplace_back(doing);
        }
        return true;
    });
    REQUIRE(error == "");
    CHECK(said.front() == "Reading the zip.");
    CHECK(said.back() == "Saving the hand.");
    PitModel m;
    std::string why;
    REQUIRE(ParsePitModel(hand, m, why));
    CHECK(m.hand_length == kHandImportLength);
    CHECK(m.joints.size() == 26);
    CHECK(m.plate_index_count > 0);          // the gold on the hand's joint, last
    CHECK(m.body_index_count > 3 * 4);       // the hand, sleeve and band (made rounder) and the stub
    CHECK(m.handle.radius > 0.0f);
    CHECK(m.texture_width >= 1024);
    // In arm space at the size drawn: the wrist at the origin, the middle finger's tip a hand away.
    bool wrist = false, tip = false, ring = false;
    for (const PitModelJoint& j : m.joints) {
        if (j.role == kPitWrist) {
            wrist = std::abs(j.position[0]) < 1e-6f;
        }
        if (j.role == kPitMiddleTip) {
            tip = std::abs(std::hypot(j.position[0], j.position[1], j.position[2]) - kHandImportLength) < 1e-5f;
        }
    }
    CHECK(wrist);
    CHECK(tip);
    // The ring and the upper arm's cuff are left out: nothing is further than the elbow's stub
    // or as far off the arm's line as the ring was.
    const float scale = kHandImportLength / 2.6f;
    for (uint32_t v = 0; v < m.vertex_count; v++) {
        const float* p = &m.vertices[std::size_t{v} * kPitVertexFloats];
        ring = ring || p[1] > 1.5f * scale;
        CHECK(p[0] > -3.5f * scale);
    }
    CHECK_FALSE(ring);
    // The same zip, the same bytes; saved through a file beside it, then renamed.
    std::vector<uint8_t> again;
    CHECK(Import(Zip(Files(Dae())), &again) == "");
    CHECK(again == hand);
}

TEST_CASE("Hand importer: what is missing or wrong is said in a plain sentence", "[kiuhand]") {
    CHECK(Import({}) == "This is not a zip file.");
    CHECK(Import(std::vector<uint8_t>(100, 'x')) == "This is not a zip file.");
    {
        Options o;
        o.skip_joint = "FingerL32";
        CHECK(Import(Zip(Files(Dae(o)))) == "The model is missing a joint: FingerL32.");
        o = {};
        o.no_hand = true;
        CHECK(Import(Zip(Files(Dae(o)))) == "The model has no left hand.");
        o = {};
        o.bad_index = 999;
        CHECK(Import(Zip(Files(Dae(o)))) == "The model's numbers do not add up.");
        o = {};
        o.head = "<!DOCTYPE COLLADA [<!ENTITY a \"aaaa\">]>";
        CHECK(Import(Zip(Files(Dae(o)))) == "The model file uses something this refuses (a document type or an entity).");
        o = {};
        o.in_root = "<asset><unit name=\"&a;\"/></asset>";
        CHECK(Import(Zip(Files(Dae(o)))) == "The model file uses something this refuses (a document type or an entity).");
        o = {};
        o.in_root = "<asset><![CDATA[x]]></asset>";
        CHECK(Import(Zip(Files(Dae(o)))) == "The model file uses something this refuses (a document type or an entity).");
        o = {};
        o.in_root = "<asset><unit></asset>";
        CHECK(Import(Zip(Files(Dae(o)))) == "The model file is not well formed.");
        o = {};
        for (int i = 0; i < 100; i++) {
            o.in_root += "<a>";
        }
        CHECK(Import(Zip(Files(Dae(o)))) == "The model file is too big or too deep.");
    }
    std::vector<File> files = Files(Dae());
    files.push_back({"../outside.txt", {'x'}});
    CHECK(Import(Zip(files)) == "The zip has a file with an unsafe name.");
    files.back().name = "/etc/outside";
    CHECK(Import(Zip(files)) == "The zip has a file with an unsafe name.");
    files.back().name = "Model/a/../../../outside";
    CHECK(Import(Zip(files)) == "The zip has a file with an unsafe name.");
    files.back().name = "C:\\outside";
    CHECK(Import(Zip(files)) == "The zip has a file with an unsafe name.");
    files.back().name = "Model/pit.dae";
    CHECK(Import(Zip(files)) == "The zip has the same file twice.");
    files.back().name = "Other/pit.dae";
    CHECK(Import(Zip(files)) == "The zip has more than one model in it (pit.dae).");
    files = Files(Dae());
    files.erase(files.begin());
    CHECK(Import(Zip(files)) == "The zip has no model in it (pit.dae).");
    files = Files(Dae());
    files.erase(files.begin() + 3);
    CHECK(Import(Zip(files)) == "The zip is missing a picture: skin_pit_001_ao.png.");
    files = Files(Dae());
    files[2].data = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
    CHECK(Import(Zip(files)) == "A picture could not be read: skin_pit_001_nor.png.");
    // A member that says it unpacks to more than is allowed, or to more than its packing could
    // hold, or is packed some other way, or whose bytes do not match its checksum.
    files = Files(Dae());
    files[0] = Stored(Dae());
    files[0].claimed_size = 0x7FFFFFFFu;
    CHECK(Import(Zip(files)) == "A file in the zip is too big: Model/pit.dae.");
    files[0].claimed_size = static_cast<uint32_t>(files[0].data.size()) + 5;
    CHECK(Import(Zip(files)) == "A file in the zip is damaged: Model/pit.dae.");
    files = Files(Dae());
    files[0] = Stored(Dae());
    files[0].method = 8; // "deflated", but it is not
    files[0].claimed_size = static_cast<uint32_t>(files[0].data.size()) * 100;
    CHECK(Import(Zip(files)) == "A file in the zip is damaged: Model/pit.dae.");
    files[0].claimed_size = static_cast<uint32_t>(files[0].data.size()) * 1000; // past the packing ratio
    CHECK(Import(Zip(files)) == "A file in the zip is damaged: Model/pit.dae.");
    files = Files(Dae());
    files[0] = Stored(Dae());
    files[0].method = 14;
    CHECK(Import(Zip(files)) == "A file in the zip is packed in a way this cannot read: Model/pit.dae.");
    {
        std::vector<File> stored = Files(Dae());
        stored[0] = Stored(Dae());
        std::vector<uint8_t> zip = Zip(stored);
        zip[60] ^= 0x20; // a letter of the model's text
        CHECK(Import(zip) == "A file in the zip is damaged: Model/pit.dae.");
    }
    files = Files(Dae());
    for (int i = 0; i < 5000; i++) {
        files.push_back({"extra/" + std::to_string(i), {}});
    }
    CHECK(Import(Zip(files)) == "The zip has too many files.");
    // Asked to stop.
    std::vector<uint8_t> hand;
    int calls = 0;
    CHECK(ImportHand(Zip(Files(Dae())), hand, [&calls](std::string_view, float) { return ++calls < 3; }) == "Stopped.");
    CHECK(hand.empty());
    // ... at any point of the work: every count of answers before the "no" stops it cleanly.
    int asked = 0;
    REQUIRE(ImportHand(Zip(Files(Dae())), hand, [&asked](std::string_view, float) { return ++asked > 0; }) == "");
    CHECK(asked > 12);
    for (int stop_at = 1; stop_at <= asked; stop_at++) {
        int n = 0;
        REQUIRE(ImportHand(Zip(Files(Dae())), hand, [&n, stop_at](std::string_view, float) { return ++n < stop_at; }) == "Stopped.");
        REQUIRE(hand.empty());
    }
}

TEST_CASE("Hand importer: no damage to a zip does worse than an error", "[kiuhand]") {
    const std::string dae = Dae();
    const std::vector<uint8_t> good = Zip(Files(dae));
    uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    int refused = 0, taken = 0;
    const auto run = [&](const std::vector<uint8_t>& zip) {
        std::vector<uint8_t> hand;
        const std::string error = ImportHand(zip, hand);
        REQUIRE(error.empty() == !hand.empty());
        if (error.empty()) {
            PitModel m;
            std::string why;
            REQUIRE(ParsePitModel(hand, m, why)); // what comes out is always a file the app takes
            taken++;
        } else {
            REQUIRE(error.back() == '.');
            refused++;
        }
    };
    // The zip itself: cut short anywhere near its ends, and with bytes changed anywhere.
    for (std::size_t n = 0; n < good.size(); n += (n < 200 || n + 300 > good.size()) ? 1 : 97) {
        run({good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n)});
    }
    for (int i = 0; i < 1500; i++) {
        std::vector<uint8_t> bad = good;
        const int changes = 1 + static_cast<int>(next() % 4);
        for (int k = 0; k < changes; k++) {
            // the directory and the end record are small: aim half the changes at them
            const std::size_t at = (next() % 2) ? good.size() - 1 - next() % 1400 : next() % good.size();
            bad[at] = static_cast<uint8_t>(next());
        }
        run(bad);
    }
    // The model's text, put in a sound zip each time (so it is the reader that meets it).
    for (int i = 0; i < 400; i++) {
        std::string text = dae;
        const int kind = static_cast<int>(next() % 4);
        const std::size_t at = next() % text.size();
        if (kind == 0) {
            text[at] = static_cast<char>(next());
        } else if (kind == 1) {
            text.erase(at, 1 + next() % 40);
        } else if (kind == 2) {
            static const char* const bits[] = {"<", ">", "&", "\"", "</", "<!--", "-1", "99999999999", "1e999", "<a b='", "/>"};
            text.insert(at, bits[next() % 11]);
        } else {
            text.resize(at);
        }
        run(Zip(Files(text)));
    }
    CHECK(refused > 1000);
    CHECK(taken >= 0);
}

TEST_CASE("Hand importer: from a path into the user directory, through a file beside it", "[kiuhand]") {
    namespace fs = std::filesystem;
    const fs::path user = fs::temp_directory_path() / "azahar-hand-import-test";
    fs::remove_all(user);
    fs::create_directories(user / "kiu_hand");
    CHECK_FALSE(HandFileExists(user.string()));
    CHECK(ImportHandFile((user / "none.zip").string(), user.string()) == "The zip could not be read.");
    const std::vector<uint8_t> zip = Zip(Files(Dae()));
    const fs::path a = user / "kiu_hand" / "b model.ZIP", b = user / "kiu_hand" / "a.zip";
    for (const fs::path& p : {a, b}) {
        std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
    }
    CHECK(ImportHandFile(a.string(), user.string()) == "");
    CHECK(HandFileExists(user.string()));
    PitModel m;
    std::string error;
    CHECK(LoadPitModel((user / kPitModelFile).string(), m, error));
    CHECK_FALSE(fs::exists(user / "kiu_hand" / "pit_hand.bin.new"));
    // A bad zip leaves the hand that is there alone.
    const auto size = fs::file_size(user / kPitModelFile);
    std::ofstream(b, std::ios::binary | std::ios::trunc) << "not a zip";
    CHECK(ImportHandFile(b.string(), user.string()) == "This is not a zip file.");
    CHECK(fs::file_size(user / kPitModelFile) == size);
    fs::remove_all(user);
}
