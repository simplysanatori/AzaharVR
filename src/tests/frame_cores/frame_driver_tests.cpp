// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "frame_cores/driver_runner.h"
#include "frame_cores/frame_driver.h"
#include "frame_cores/second_screen.h"

using namespace FrameCores;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("azaharvr_driver_test_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void Write(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

/// A core of 2 x 2 pixels in a buffer 3 wide, that writes down what it is asked.
struct FakeCore {
    std::vector<std::string> calls;
    std::vector<uint32_t> buffer{0x00000001, 0x00000002, 0x99999999,
                                 0x11000003, 0xFF000004, 0x99999999};

    DriverServices Services() {
        DriverServices s;
        s.step = [this](uint32_t keys) { calls.push_back("step " + std::to_string(keys)); };
        s.drain_audio = [this](int16_t* out, std::size_t max_frames) -> std::size_t {
            calls.push_back("audio " + std::to_string(max_frames));
            out[0] = 7;
            out[1] = -7;
            return 1;
        };
        s.rumble = [this] {
            calls.push_back("rumble");
            return 0.25f;
        };
        s.picture = [this] {
            calls.push_back("picture");
            return DriverPicture{buffer.data(), 2, 2, 3};
        };
        return s;
    }
};

/// What a test says a driver does.
struct Script {
    int steps = 1;
    bool own_thread = false;
    bool touch = false;
    std::pair<uint32_t, uint32_t> size{};
    DriverInput last;
    DriverServices services;
    int resets = 0;
    int loads = 0;
    int gone = 0;
};

class ScriptDriver final : public FrameDriver {
public:
    ScriptDriver(const DriverServices& services, Script& script_) : script(script_) {
        script.services = services;
    }
    ~ScriptDriver() override {
        ++script.gone;
    }
    void RunFrame(const DriverInput& in, DriverFrame& out) override {
        CHECK(out.primary.pixels == nullptr);
        CHECK(out.audio_pcm.empty());
        script.last = in;
        const auto run = [this, &in] {
            for (int i = 0; i < script.steps; ++i) {
                script.services.step(in.keys);
            }
        };
        if (script.own_thread) {
            std::thread(run).join();
        } else {
            run();
        }
        out.audio_pcm = {1, 2};
    }
    void Reset() override {
        ++script.resets;
    }
    void AfterStateLoad() override {
        ++script.loads;
    }
    std::pair<uint32_t, uint32_t> SecondarySize() const override {
        return script.size;
    }
    bool HasTouch() const override {
        return script.touch;
    }

private:
    Script& script;
};

CoreBootOptions With(Script& script) {
    CoreBootOptions opts;
    opts.driver.make = [&script](const DriverServices& services) {
        return std::make_unique<ScriptDriver>(services, script);
    };
    return opts;
}

using Common::AltCore::Capabilities;
using Common::AltCore::InputState;

} // namespace

TEST_CASE("ChooseDriver: the choice, then what the game has", "[frame_cores]") {
    using K = DriverKind;
    using S = SecondScreen;
    // choice, PokeDaisy knows it, gba-ds knows it
    CHECK(ChooseDriver(S::PokeDaisy, true, true) == K::PokeDaisy);
    CHECK(ChooseDriver(S::PokeDaisy, true, false) == K::PokeDaisy);
    CHECK(ChooseDriver(S::PokeDaisy, false, true) == K::GbaDs);
    CHECK(ChooseDriver(S::PokeDaisy, false, false) == K::Plain);
    CHECK(ChooseDriver(S::GbaDs, true, true) == K::GbaDs);
    CHECK(ChooseDriver(S::GbaDs, true, false) == K::PokeDaisy);
    CHECK(ChooseDriver(S::GbaDs, false, true) == K::GbaDs);
    CHECK(ChooseDriver(S::GbaDs, false, false) == K::Plain);
    for (const bool pokedaisy : {false, true}) {
        for (const bool gbads : {false, true}) {
            CHECK(ChooseDriver(S::Off, pokedaisy, gbads) == K::Plain);
        }
    }
}

TEST_CASE("DriverBufferWidth: PokeDaisy's is 240", "[frame_cores]") {
    for (const DriverKind kind : {DriverKind::Plain, DriverKind::GbaDs}) {
        CHECK(DriverBufferWidth(kind, 160) == 160);
        CHECK(DriverBufferWidth(kind, 240) == 240);
    }
    CHECK(DriverBufferWidth(DriverKind::PokeDaisy, 160) == 240);
    CHECK(DriverBufferWidth(DriverKind::PokeDaisy, 256) == 256);
}

TEST_CASE("MapCaptureDue: the first picture, a new room, the interval", "[frame_cores]") {
    // has_image, room_changed, frame, since, interval
    CHECK_FALSE(MapCaptureDue(false, false, 29, 29, 0));
    CHECK(MapCaptureDue(false, false, 30, 30, 0));
    CHECK_FALSE(MapCaptureDue(false, false, 100, 29, 0));
    CHECK_FALSE(MapCaptureDue(true, false, 100, 30, 0));
    CHECK_FALSE(MapCaptureDue(true, true, 100, 14, 0));
    CHECK(MapCaptureDue(true, true, 100, 15, 0));
    CHECK_FALSE(MapCaptureDue(true, false, 200, 119, 0));
    CHECK(MapCaptureDue(true, false, 200, 120, 0));
    CHECK_FALSE(MapCaptureDue(true, false, 900, 599, 600));
    CHECK(MapCaptureDue(true, false, 900, 600, 600));
    CHECK(MapCaptureDue(true, false, 900, 5, 5));
}

TEST_CASE("The Plain driver: one frame, opaque, then the sound and the rumble", "[frame_cores]") {
    FakeCore core;
    RomInfo info;
    info.kind = Common::AltCore::CoreKind::GB;
    const auto driver = MakePlainDriver(core.Services(), info);
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{});
    CHECK_FALSE(driver->HasTouch());
    CHECK(driver->StateSafeNow());

    DriverInput in;
    in.keys = 0x3FF;
    DriverFrame out;
    driver->RunFrame(in, out);
    CHECK(core.calls == std::vector<std::string>{"step 1023", "picture", "audio 4096", "rumble"});
    REQUIRE(out.primary.pixels != nullptr);
    CHECK(out.primary.width == 2);
    CHECK(out.primary.height == 2);
    CHECK(out.primary.stride == 2);
    const std::vector<uint32_t> picture(out.primary.pixels, out.primary.pixels + 4);
    CHECK(picture == std::vector<uint32_t>{0xFF000001, 0xFF000002, 0xFF000003, 0xFF000004});
    CHECK(out.secondary.pixels == nullptr);
    CHECK(out.overlay.pixels == nullptr);
    CHECK(out.audio_pcm == std::vector<int16_t>{7, -7});
    CHECK(out.rumble == 0.25f);

    // Nothing but a frame asks the core for anything
    core.calls.clear();
    driver->Reset();
    driver->AfterStateLoad();
    CHECK(core.calls.empty());
}

TEST_CASE("DriverRunner counts the steps of a frame", "[frame_cores]") {
    FakeCore core;
    Script script;
    Capabilities caps = Capabilities::ForGB();
    DriverRunner runner;
    FrameOutput out;
    runner.RunFrame({}, out, caps); // no driver yet
    CHECK(out.audio_pcm.empty());

    runner.Start(DriverKind::Plain, core.Services(), {}, With(script), {}, caps);
    InputState in;
    in.buttons = 0xFFFF;
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 0);
    CHECK(core.calls == std::vector<std::string>{"step 1023"});
    CHECK(out.audio_pcm == std::vector<int16_t>{1, 2});
    // The sound of one frame only
    runner.RunFrame(in, out, caps);
    CHECK(out.audio_pcm == std::vector<int16_t>{1, 2});

    script.own_thread = true; // a driver may step on a thread of its own
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 0);
    script.own_thread = false;

    script.steps = 0;
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 1);
    script.steps = 2;
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 2);
    script.steps = 1;
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 2);
    script.services.step(0); // between two frames
    runner.RunFrame(in, out, caps);
    CHECK(runner.Missteps() == 3);

    runner.Reset();
    runner.AfterStateLoad();
    CHECK(script.resets == 1);
    CHECK(script.loads == 1);
    runner.Stop();
    CHECK(script.gone == 1);
    CHECK(runner.StateSafeNow());
}

TEST_CASE("DriverRunner: the Secondary Screen's size and touch", "[frame_cores]") {
    FakeCore core;
    Script script;
    FrameOutput out;
    InputState touch;
    touch.touching = true;
    touch.touch_x = 12;
    touch.touch_y = 34;

    Capabilities caps = Capabilities::ForGBA();
    DriverRunner runner;
    runner.Start(DriverKind::Plain, core.Services(), {}, With(script), {}, caps);
    CHECK(caps == Capabilities::ForGBA());
    runner.RunFrame(touch, out, caps);
    CHECK_FALSE(script.last.touching); // the driver takes none
    CHECK_FALSE(out.metrics_changed);

    // A picture without touch leaves the touch size alone
    script.size = {480, 320};
    runner.RunFrame(touch, out, caps);
    CHECK(out.metrics_changed);
    CHECK(out.updated_metrics == caps.screens);
    CHECK(caps.screens.has_bottom);
    CHECK(caps.screens.bottom_aspect == 1.5f);
    CHECK(caps.screens.touch_width == 240);
    runner.RunFrame(touch, out, caps);
    CHECK_FALSE(out.metrics_changed);

    script.touch = true;
    script.size = {320, 240};
    caps = Capabilities::ForGBA();
    runner.Start(DriverKind::Plain, core.Services(), {}, With(script), {}, caps);
    CHECK(caps.has_touch);
    CHECK(caps.screens.has_bottom);
    CHECK(caps.screens.touch_width == 320);
    CHECK(caps.screens.touch_height == 240);
    runner.RunFrame(touch, out, caps);
    CHECK_FALSE(out.metrics_changed);
    CHECK(script.last.touching);
    CHECK(script.last.touch_x == 12);
    CHECK(script.last.touch_y == 34);
    runner.RunFrame({}, out, caps);
    CHECK_FALSE(script.last.touching);

    script.size = {};
    runner.RunFrame({}, out, caps);
    CHECK(out.metrics_changed);
    CHECK_FALSE(caps.screens.has_bottom);
    runner.Stop();
}

TEST_CASE("The battery save is copied three times over, once a session", "[frame_cores]") {
    TempDir tmp;
    const fs::path save = tmp.path / "game.sav";
    const auto copy = [&save](int n) {
        return fs::path(save.string() + ".before-edit-" + std::to_string(n));
    };
    CHECK(BackUpSave(save.string())); // no save yet
    Write(save, "");
    CHECK(BackUpSave(save.string())); // the core's empty file
    CHECK_FALSE(fs::exists(copy(1)));

    for (const char* text : {"one", "two", "three", "four"}) {
        Write(save, text);
        REQUIRE(BackUpSave(save.string()));
        CHECK(Read(copy(1)) == text);
    }
    CHECK(Read(copy(2)) == "three");
    CHECK(Read(copy(3)) == "two");
    CHECK(Read(save) == "four");
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(tmp.path)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 4);

    // Not copied: false, and the copies stay
    fs::create_directories(save.string() + ".before-edit.tmp"); // in the way
    CHECK_FALSE(BackUpSave(save.string()));
    CHECK(Read(copy(1)) == "four");
    CHECK(Read(copy(2)) == "three");
    fs::remove_all(save.string() + ".before-edit.tmp");

    FakeCore core;
    Script script;
    Capabilities caps = Capabilities::ForGB();
    DriverRunner runner;
    for (const char* text : {"five", "six"}) {
        Write(save, text);
        runner.Start(DriverKind::Plain, core.Services(), {}, With(script), save.string(), caps);
        CHECK(script.services.backup_save());
        Write(save, "later");
        CHECK(script.services.backup_save());
        CHECK(Read(copy(1)) == text);
    }
    CHECK(Read(copy(2)) == "five");
    CHECK(Read(copy(3)) == "four");
    runner.Stop();
}
