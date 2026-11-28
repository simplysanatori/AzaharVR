// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR pcdiag: the PC controllers' drawings for the controls help: the parts by our mapping,
// the converter on SteamVR-like folders, the cache, and (hidden, [.pcdiaglocal]) the drawings of
// this PC's own SteamVR models as PNG files for a look.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <lodepng.h>

#include "core/frame_controller_models.h"
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/vis/controller_diagram.h"
#include "frame_xr/vis/help_layout.h"
#include "frame_xr/vis/model_file.h"

namespace fs = std::filesystem;
using namespace FrameXr::Vis;
using FrameXr::Input::ControllerScheme;

namespace {

ModelComponent Comp(const char* path) {
    ModelComponent c;
    c.path = path;
    return c;
}

std::optional<CtrlPart> Part(const char* path, ControllerScheme s, int hand) {
    return PartOfComponent(Comp(path), s, hand);
}

// A scratch folder, removed afterwards.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / (std::string("azaharvr-pcdiag-") + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// An OBJ box (normals per face), the way SteamVR's meshes are written.
void WriteBox(const fs::path& file, float x0, float y0, float z0, float x1, float y1, float z1) {
    std::ofstream f(file);
    for (int i = 0; i < 8; i++) {
        f << "v " << ((i & 1) ? x1 : x0) << ' ' << ((i & 2) ? y1 : y0) << ' ' << ((i & 4) ? z1 : z0)
          << '\n';
    }
    f << "vn 1 0 0\nvn -1 0 0\nvn 0 1 0\nvn 0 -1 0\nvn 0 0 1\nvn 0 0 -1\n";
    // Each face's corners (1-based, around it), with its normal.
    const int faces[6][4] = {{2, 4, 8, 6}, {1, 5, 7, 3}, {3, 7, 8, 4},
                             {1, 2, 6, 5}, {5, 6, 8, 7}, {1, 3, 4, 2}};
    for (int k = 0; k < 6; k++) {
        f << 'f';
        for (const int v : faces[k]) {
            f << ' ' << v << "//" << (k + 1);
        }
        f << '\n';
    }
}

struct FakePart {
    const char* key;
    const char* json; ///< the component's JSON, without its filename
    std::array<float, 6> box;
};

// A SteamVR render model folder `dir`/`name` with `parts` (a body is added) and an aim.
void WriteFolder(const fs::path& dir, const std::string& name, const std::vector<FakePart>& parts) {
    const fs::path d = dir / name;
    fs::create_directories(d);
    std::string json = "{\"components\": {\n";
    json += "\"body\": {\"filename\": \"body.obj\"},\n";
    WriteBox(d / "body.obj", -0.03f, -0.02f, 0.0f, 0.03f, 0.0f, 0.1f);
    for (const FakePart& p : parts) {
        const std::string file = std::string(p.key) + ".obj";
        WriteBox(d / file, p.box[0], p.box[1], p.box[2], p.box[3], p.box[4], p.box[5]);
        json += std::string("\"") + p.key + "\": {\"filename\": \"" + file + "\"" +
                (*p.json ? ", " : "") + p.json + "},\n";
    }
    json += "\"openxr_aim\": {\"component_local\": {\"origin\": [0, 0, 0], \"rotate_xyz\": "
            "[0, 0, 0]}}\n}}\n";
    std::ofstream(d / (name + ".json")) << json;
}

constexpr const char* kButton = "\"motion\": {\"type\": \"translate\", \"pressed_path\": "
                                "\"/input/%s/click\", \"axis\": [0, -1, 0]}";

std::string ButtonJson(const char* letter) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), kButton, letter);
    return buf;
}

// The Index's left hand, simplified: a trackpad, a stick, a and b (b nearer the front), a trigger.
void WriteIndexLike(const fs::path& rendermodels, const std::string& name) {
    static const std::string a = ButtonJson("a"), b = ButtonJson("b");
    WriteFolder(rendermodels, name,
                {{"trackpad",
                  "\"motion\": {\"type\": \"trackpad\", \"component_path\": \"/input/trackpad\"}",
                  {-0.025f, 0.0f, 0.05f, -0.005f, 0.003f, 0.08f}},
                 {"trackpad_touch",
                  "\"motion\": {\"type\": \"trackpad\", \"component_path\": \"/input/trackpad\"}, "
                  "\"visibility\": {\"default\": false}",
                  {-0.02f, 0.0f, 0.06f, -0.01f, 0.005f, 0.07f}},
                 {"thumbstick",
                  "\"motion\": {\"type\": \"joystick\", \"center\": [0, 0, 0], "
                  "\"joystick_path\": \"/input/joystick\", \"component_path\": "
                  "\"/input/thumbstick\"}",
                  {-0.02f, 0.0f, 0.01f, -0.005f, 0.006f, 0.03f}},
                 {"button_a", a.c_str(), {0.005f, 0.0f, 0.04f, 0.02f, 0.004f, 0.05f}},
                 {"button_b", b.c_str(), {0.005f, 0.0f, 0.015f, 0.02f, 0.004f, 0.025f}},
                 {"trigger",
                  "\"motion\": {\"type\": \"rotate\", \"trigger_path\": \"/input/trigger\", "
                  "\"pivot\": [0, 0, 0], \"axis\": [1, 0, 0]}",
                  {-0.01f, -0.035f, 0.0f, 0.01f, -0.02f, 0.02f}}});
}

// A Touch-like hand: a joystick, x (or a), y (or b), the "system" button, a trigger, a grip.
void WriteTouchLike(const fs::path& rendermodels, const std::string& name, bool left) {
    static const std::string x = ButtonJson("x"), y = ButtonJson("y"), a = ButtonJson("a"),
                             b = ButtonJson("b"), sys = ButtonJson("system");
    WriteFolder(
        rendermodels, name,
        {{"thumbstick",
          "\"motion\": {\"type\": \"joystick\", \"center\": [0, 0, 0], "
          "\"component_path\": \"/input/joystick\"}",
          {-0.02f, 0.0f, 0.04f, -0.005f, 0.006f, 0.06f}},
         {"button_1", (left ? x : a).c_str(), {0.005f, 0.0f, 0.04f, 0.02f, 0.004f, 0.05f}},
         {"button_2", (left ? y : b).c_str(), {0.005f, 0.0f, 0.015f, 0.02f, 0.004f, 0.025f}},
         {"button_enter", sys.c_str(), {-0.02f, 0.0f, 0.07f, -0.01f, 0.003f, 0.08f}},
         {"trigger",
          "\"motion\": {\"type\": \"rotate\", \"trigger_path\": \"/input/trigger\", "
          "\"pivot\": [0, 0, 0], \"axis\": [1, 0, 0]}",
          {-0.01f, -0.035f, 0.0f, 0.01f, -0.02f, 0.02f}},
         {"button_grip",
          "\"motion\": {\"type\": \"rotate\", \"trigger_path\": \"/input/grip\", "
          "\"pivot\": [0, 0, 0], \"axis\": [0, 0, -1]}",
          {0.03f, -0.018f, 0.05f, 0.036f, -0.004f, 0.09f}}});
}

const FrameControllerModels::ImageLoader kNoImages =
    [](const std::string&, FrameControllerModels::Image*) { return false; };

bool Has(const DiagramImage& img, CtrlPart p) {
    return img.anchor[static_cast<std::size_t>(p)].has_value();
}

} // namespace

TEST_CASE("AzaharVR pcdiag: the parts by our mapping", "[frame_xr][ctrldiag][pcdiag]") {
    for (const ControllerScheme s : {ControllerScheme::Frame, ControllerScheme::Touch,
                                     ControllerScheme::Index, ControllerScheme::Focus3}) {
        for (const int h : {0, 1}) {
            INFO("scheme " << static_cast<int>(s) << " hand " << h);
            // The sticks by either name, the trackpad by any of its paths.
            REQUIRE(Part("/input/thumbstick", s, h) == CtrlPart::Stick);
            REQUIRE(Part("/input/joystick", s, h) == CtrlPart::Stick);
            REQUIRE(Part("/input/trackpad", s, h) == CtrlPart::Trackpad);
            REQUIRE(Part("/input/trackpad/touch", s, h) == CtrlPart::Trackpad);
            REQUIRE(Part("/input/trigger", s, h) == CtrlPart::Trigger);
            REQUIRE(Part("/input/grip", s, h) == CtrlPart::Grip);
            REQUIRE_FALSE(Part("", s, h));
            REQUIRE_FALSE(Part("/input/trackpad/y", s, h));
        }
    }
    // The left three-line button is SteamVR's "system" on Touch and Focus 3: our Menu.
    REQUIRE(Part("/input/system/click", ControllerScheme::Touch, 0) == CtrlPart::Menu);
    REQUIRE(Part("/input/system/click", ControllerScheme::Touch, 1) == CtrlPart::System);
    REQUIRE(Part("/input/system/click", ControllerScheme::Focus3, 0) == CtrlPart::Menu);
    REQUIRE(Part("/input/system/click", ControllerScheme::Focus3, 1) == CtrlPart::System);
    REQUIRE(Part("/input/system/click", ControllerScheme::Index, 0) == CtrlPart::System);
    REQUIRE(Part("/input/system/click", ControllerScheme::Frame, 0) == CtrlPart::System);
    // The Index's left a and b are the 3DS's X and Y; the right ones A and B.
    REQUIRE(Part("/input/a/click", ControllerScheme::Index, 0) == CtrlPart::X);
    REQUIRE(Part("/input/b/click", ControllerScheme::Index, 0) == CtrlPart::Y);
    REQUIRE(Part("/input/a/click", ControllerScheme::Index, 1) == CtrlPart::A);
    REQUIRE(Part("/input/b/click", ControllerScheme::Index, 1) == CtrlPart::B);
    // The Frame's as before (its right X and Y are its own buttons).
    REQUIRE(Part("/input/a/click", ControllerScheme::Frame, 0) == CtrlPart::A);
    REQUIRE(Part("/input/x/click", ControllerScheme::Frame, 1) == CtrlPart::X);
    REQUIRE(Part("/input/menu/click", ControllerScheme::Frame, 1) == CtrlPart::Menu);
    REQUIRE(PartOfComponent(Comp("/input/view/click")) == CtrlPart::View);
}

TEST_CASE("AzaharVR pcdiag: the schemes' folders and angles", "[frame_xr][ctrldiag][pcdiag]") {
    REQUIRE(std::string(SchemeModels(ControllerScheme::Frame).left) == "frame_controller_left");
    REQUIRE(std::string(SchemeModels(ControllerScheme::Touch).right) ==
            "oculus_quest_plus_controller_right");
    REQUIRE(std::string(SchemeModels(ControllerScheme::Index).subdir) ==
            "drivers/indexcontroller/resources/rendermodels");
    REQUIRE(std::string(SchemeModels(ControllerScheme::Focus3).left) ==
            "vive_focus3_controller_left");
    // The Frame's angles are the ones its help always had.
    const SchemeView frame = DiagramView(ControllerScheme::Frame);
    REQUIRE(frame.top.roll == 0.0f);
    REQUIRE(frame.top.pitch == 90.0f);
    REQUIRE(frame.front.roll == 180.0f);
    REQUIRE(frame.front.pitch == 165.0f);
    REQUIRE(frame.front_scale == 0.9f);
    const DiagramAngle r = HandAngle({10.0f, 80.0f, 5.0f}, 1);
    REQUIRE(r.roll == -10.0f);
    REQUIRE(r.pitch == 80.0f);
    REQUIRE(r.yaw == -5.0f);
}

TEST_CASE("AzaharVR pcdiag: converting a PC controller's folder", "[frame_xr][ctrldiag][pcdiag]") {
    TempDir tmp("convert");
    WriteIndexLike(tmp.path, "index_left");
    std::string error;
    FrameControllerModels::Model model;
    REQUIRE(FrameControllerModels::ConvertFolder((tmp.path / "index_left").string(), "index_left",
                                                 FrameControllerModels::kNoTextures, kNoImages,
                                                 &model, &error));
    REQUIRE(model.tex_w == 1);
    const fs::path file = tmp.path / "index_left.model";
    std::ofstream(file, std::ios::binary)
        .write(model.bytes.data(), static_cast<std::streamsize>(model.bytes.size()));
    ModelFile m;
    REQUIRE(LoadModelFile(file.string(), m, error));
    std::vector<std::string> names;
    for (const ModelComponent& c : m.components) {
        names.push_back(c.name);
        if (c.name == "trackpad") {
            REQUIRE(c.path == "/input/trackpad"); // kept, not moving
            REQUIRE(c.motion_type == 0);
        }
        if (c.name == "thumbstick") {
            REQUIRE(c.path == "/input/thumbstick");
            REQUIRE(c.motion_type == 3);
        }
    }
    // The hidden trackpad_touch is not drawn.
    REQUIRE(std::find(names.begin(), names.end(), "trackpad_touch") == names.end());
    REQUIRE(std::find(names.begin(), names.end(), "trackpad") != names.end());

    SECTION("the Index's colour image is its _diff.png") {
        const FrameControllerModels::ImageLoader diff = [](const std::string& path,
                                                           FrameControllerModels::Image* out) {
            if (path.size() < 9 || path.compare(path.size() - 9, 9, "_diff.png") != 0) {
                return false;
            }
            out->width = out->height = 2;
            out->rgb.assign(12, 200);
            return true;
        };
        REQUIRE(FrameControllerModels::ConvertFolder((tmp.path / "index_left").string(),
                                                     "index_left", 0, diff, &model, &error));
        REQUIRE(model.tex_w == 2);
        REQUIRE_FALSE(FrameControllerModels::ConvertFolder(
            (tmp.path / "index_left").string(), "index_left", 0, kNoImages, &model, &error));
    }
}

TEST_CASE("AzaharVR pcdiag: a scheme's models from SteamVR, cached",
          "[frame_xr][ctrldiag][pcdiag]") {
    TempDir tmp("cache");
    const fs::path root = tmp.path / "SteamVR";
    const fs::path cache = tmp.path / "cache";
    const SchemeModelFolders& touch = SchemeModels(ControllerScheme::Touch);
    const SchemeModelFolders& index = SchemeModels(ControllerScheme::Index);
    WriteTouchLike(root / touch.subdir, touch.left, true);
    WriteTouchLike(root / touch.subdir, touch.right, false);
    WriteIndexLike(root / index.subdir, index.left);
    WriteIndexLike(root / index.subdir, index.right);
    const std::vector<std::string> roots{(tmp.path / "nothing").string(), root.string()};

    std::array<ModelFile, 2> files;
    std::string error;
    REQUIRE(ReadSchemeModelsFrom(ControllerScheme::Touch, roots, cache.string(), files, error));
    const fs::path left_model = cache / (std::string(touch.left) + ".model");
    REQUIRE(fs::is_regular_file(left_model));
    REQUIRE(fs::is_regular_file(cache / (std::string(touch.left) + ".stamp")));

    SECTION("the drawings: Touch's left system button is the Menu, the stick is drawn") {
        const SchemeHandSizes sizes{200, 340, 200, 440};
        const SchemeHands hands =
            DrawSchemeHands(files[0], files[1], ControllerScheme::Touch, sizes);
        REQUIRE(Has(hands.top[0], CtrlPart::Stick));
        REQUIRE(Has(hands.top[0], CtrlPart::X));
        REQUIRE(Has(hands.top[0], CtrlPart::Y));
        REQUIRE(Has(hands.top[0], CtrlPart::Menu));
        REQUIRE_FALSE(Has(hands.top[0], CtrlPart::System));
        REQUIRE(Has(hands.top[1], CtrlPart::A));
        REQUIRE(Has(hands.top[1], CtrlPart::B));
        REQUIRE(Has(hands.top[1], CtrlPart::System));
        REQUIRE_FALSE(Has(hands.top[1], CtrlPart::Menu));
        REQUIRE(Has(hands.front[0], CtrlPart::Trigger));
        REQUIRE(Has(hands.front[1], CtrlPart::Trigger));
    }
    SECTION("the Index: its left a and b are X and Y, the trackpad is drawn") {
        REQUIRE(ReadSchemeModelsFrom(ControllerScheme::Index, roots, cache.string(), files, error));
        const SchemeHands hands =
            DrawSchemeHands(files[0], files[1], ControllerScheme::Index, {200, 340, 200, 440});
        REQUIRE(Has(hands.top[0], CtrlPart::X));
        REQUIRE(Has(hands.top[0], CtrlPart::Y));
        REQUIRE_FALSE(Has(hands.top[0], CtrlPart::A));
        REQUIRE(Has(hands.top[1], CtrlPart::A));
        REQUIRE(Has(hands.top[0], CtrlPart::Trackpad));
        REQUIRE(Has(hands.top[1], CtrlPart::Trackpad));
        REQUIRE(Has(hands.top[0], CtrlPart::Stick));
        // b is nearer the front: drawn above a (the front at the top).
        REQUIRE(hands.top[0].anchor[static_cast<std::size_t>(CtrlPart::Y)]->y <
                hands.top[0].anchor[static_cast<std::size_t>(CtrlPart::X)]->y);
    }
    SECTION("made once, again when SteamVR's files change") {
        const auto before = fs::last_write_time(left_model);
        const std::string stamp_before = [&] {
            std::ifstream f(cache / (std::string(touch.left) + ".stamp"));
            return std::string(std::istreambuf_iterator<char>(f), {});
        }();
        REQUIRE(ReadSchemeModelsFrom(ControllerScheme::Touch, roots, cache.string(), files, error));
        REQUIRE(fs::last_write_time(left_model) == before);
        // SteamVR updated: a mesh changes size.
        std::ofstream(root / touch.subdir / touch.left / "thumbstick.obj", std::ios::app)
            << "# updated\n";
        REQUIRE(ReadSchemeModelsFrom(ControllerScheme::Touch, roots, cache.string(), files, error));
        std::ifstream f(cache / (std::string(touch.left) + ".stamp"));
        REQUIRE(std::string(std::istreambuf_iterator<char>(f), {}) != stamp_before);
    }
    SECTION("nothing for a scheme whose models are not there") {
        REQUIRE_FALSE(
            ReadSchemeModelsFrom(ControllerScheme::Focus3, roots, cache.string(), files, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE_FALSE(
            ReadSchemeModelsFrom(ControllerScheme::Touch, {}, cache.string(), files, error));
        REQUIRE_FALSE(
            ReadSchemeModelsFrom(ControllerScheme::Frame, roots, cache.string(), files, error));
    }
}

TEST_CASE("AzaharVR pcdiag: SteamVR from openvrpaths.vrpath", "[frame_xr][pcdiag]") {
#ifndef _WIN32
    TempDir tmp("roots");
    const fs::path steamvr = tmp.path / "Some Place" / "SteamVR";
    fs::create_directories(steamvr);
    fs::create_directories(tmp.path / "config" / "openvr");
    std::ofstream(tmp.path / "config" / "openvr" / "openvrpaths.vrpath")
        << "{\"runtime\": [\"" << (tmp.path / "gone").string() << "\", \"" << steamvr.string()
        << "\"], \"version\": 1}";
    const char* old = std::getenv("XDG_CONFIG_HOME");
    const std::string saved = old ? old : "";
    setenv("XDG_CONFIG_HOME", (tmp.path / "config").c_str(), 1);
    const std::vector<std::string> roots = FrameControllerModels::SteamVrRoots();
    if (old) {
        setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }
    REQUIRE_FALSE(roots.empty());
    REQUIRE(roots[0] == steamvr.string()); // the runtime list first, a missing one left out
#endif
}

// ---- The local look: AZAHARVR_PCDIAG_OUT=DIR (scratch, never the source tree), run with
// "[.pcdiaglocal]"; skipped without SteamVR's models ----

namespace {

void Blit(std::vector<uint8_t>& dst, int dw, const DiagramImage& img, int ox, int oy) {
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            const uint8_t* s = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4];
            uint8_t* d = &dst[(static_cast<std::size_t>(oy + y) * dw + ox + x) * 4];
            const float a = s[3] / 255.0f;
            for (int c = 0; c < 3; c++) {
                d[c] = static_cast<uint8_t>(std::min(255.0f, s[c] + d[c] * (1.0f - a)));
            }
        }
    }
}

void Dot(std::vector<uint8_t>& dst, int dw, int dh, float cx, float cy, uint32_t rgb) {
    for (int y = static_cast<int>(cy) - 4; y <= static_cast<int>(cy) + 4; y++) {
        for (int x = static_cast<int>(cx) - 4; x <= static_cast<int>(cx) + 4; x++) {
            if (x < 0 || y < 0 || x >= dw || y >= dh ||
                (x - cx) * (x - cx) + (y - cy) * (y - cy) > 16.0f) {
                continue;
            }
            uint8_t* d = &dst[(static_cast<std::size_t>(y) * dw + x) * 4];
            d[0] = rgb >> 16;
            d[1] = (rgb >> 8) & 0xFF;
            d[2] = rgb & 0xFF;
        }
    }
}

constexpr const char* kPartNames[kCtrlParts] = {"Stick",   "Dpad", "View",    "Menu", "System",
                                                "A",       "B",    "X",       "Y",    "Bumper",
                                                "Trigger", "Grip", "Trackpad"};
constexpr uint32_t kPartColours[kCtrlParts] = {0xFF4040, 0x40FF40, 0x4080FF, 0xFFFF40, 0xFF40FF,
                                               0x40FFFF, 0xFF9020, 0x9040FF, 0x20FF90, 0xFFFFFF,
                                               0xFF8080, 0x80FF80, 0x8080FF};

} // namespace

TEST_CASE("AzaharVR pcdiag: this PC's SteamVR controllers as PNG files", "[.pcdiaglocal]") {
    const char* out_env = std::getenv("AZAHARVR_PCDIAG_OUT");
    if (out_env == nullptr || *out_env == '\0') {
        WARN("AZAHARVR_PCDIAG_OUT not set: nothing drawn");
        return;
    }
    const fs::path out = out_env;
    // AZAHARVR_PCDIAG_STEAMVR=DIR: that SteamVR install (the test's HOME may be a scratch one).
    const char* steamvr = std::getenv("AZAHARVR_PCDIAG_STEAMVR");
    const std::vector<std::string> roots = steamvr != nullptr && *steamvr != '\0'
                                               ? std::vector<std::string>{steamvr}
                                               : FrameControllerModels::SteamVrRoots();
    const char* names[4] = {"frame", "touch", "index", "focus3"};
    int drawn = 0;
    for (int s = 0; s < 4; s++) {
        const auto scheme = static_cast<ControllerScheme>(s);
        std::array<ModelFile, 2> files;
        std::string error;
        bool ok = false;
        if (scheme == ControllerScheme::Frame) {
            // The Frame's, converted here the same way (no textures) to compare with.
            const SchemeModelFolders& f = SchemeModels(scheme);
            ok = true;
            for (int h = 0; h < 2 && ok; h++) {
                std::string path;
                ok = FrameControllerModels::EnsureCachedModel(roots, f.subdir, h ? f.right : f.left,
                                                              (out / "cache").string(), &path,
                                                              &error) &&
                     LoadModelFile(path, files[h], error);
            }
        } else {
            ok = ReadSchemeModelsFrom(scheme, roots, (out / "cache").string(), files, error);
        }
        if (!ok) {
            WARN(names[s] << ": skipped: " << error);
            continue;
        }
        constexpr int k = 2; // twice the help panel's pixels
        SchemeHandSizes sizes{kDiagramTopW * k, kDiagramTopH * k, kDiagramFrontW * k,
                              440 * k,          40 * k,           36 * k};
        DiagramStyle style;
        style.line_width = 2.2f * k;
        const SchemeHands hands = DrawSchemeHands(files[0], files[1], scheme, sizes, style);
        const int gap = 24;
        const int front_h = std::max(hands.front[0].height, hands.front[1].height);
        const int w = gap + 2 * (sizes.top_w + gap);
        const int h = gap + front_h + gap + sizes.top_h + gap;
        std::vector<uint8_t> px(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i] = 0x1C, px[i + 1] = 0x1B, px[i + 2] = 0x1F, px[i + 3] = 0xFF;
        }
        std::string anchors;
        for (int hand = 0; hand < 2; hand++) {
            const int ox = gap + hand * (sizes.top_w + gap);
            const int fy = gap + front_h - hands.front[hand].height, ty = gap + front_h + gap;
            Blit(px, w, hands.front[hand], ox, fy);
            Blit(px, w, hands.top[hand], ox, ty);
            for (std::size_t p = 0; p < kCtrlParts; p++) {
                const bool front = DiagramPartInFront(static_cast<CtrlPart>(p));
                const DiagramImage& img = front ? hands.front[hand] : hands.top[hand];
                if (const auto& a = img.anchor[p]) {
                    Dot(px, w, h, ox + a->x, (front ? fy : ty) + a->y, kPartColours[p]);
                    anchors += std::string(hand ? "right " : "left  ") +
                               (front ? "front " : "top   ") + kPartNames[p] + "\n";
                }
            }
        }
        fs::create_directories(out);
        const std::string png = (out / (std::string(names[s]) + ".png")).string();
        REQUIRE(lodepng_encode32_file(png.c_str(), px.data(), w, h) == 0);
        std::ofstream(out / (std::string(names[s]) + ".txt")) << anchors;
        drawn++;
    }
    WARN(drawn << " schemes drawn into " << out.string());
}
