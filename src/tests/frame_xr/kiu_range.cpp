// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiurange: Shot Range's table finder and patch math (frame_xr/kiu_range.h) over a
// fake guest memory. The numbers are made up; nothing here comes from the game.

#include <cstdint>
#include <cstring>
#include <map>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/kiu_range.h"

namespace KR = FrameXr::KiuRange;

namespace {

/// Sparse little-endian memory: a read or write of any unmapped byte fails.
class FakeMemory final : public KR::Memory {
public:
    bool Read(std::uint32_t addr, void* out, std::size_t size) const override {
        auto* p = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            auto it = bytes_.find(addr + static_cast<std::uint32_t>(i));
            if (it == bytes_.end()) {
                return false;
            }
            p[i] = it->second;
        }
        return true;
    }
    bool Write(std::uint32_t addr, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            if (!bytes_.count(addr + static_cast<std::uint32_t>(i))) {
                return false;
            }
        }
        for (std::size_t i = 0; i < size; ++i) {
            bytes_[addr + static_cast<std::uint32_t>(i)] = p[i];
        }
        ++writes;
        return true;
    }
    void Map(std::uint32_t addr, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.emplace(addr + static_cast<std::uint32_t>(i), std::uint8_t{0});
        }
    }
    void SetU32(std::uint32_t addr, std::uint32_t v) {
        Map(addr, 4);
        Write(addr, &v, 4);
        --writes;
    }
    void SetF(std::uint32_t addr, float v) { SetU32(addr, KR::Bits(v)); }
    float GetF(std::uint32_t addr) const {
        std::uint32_t v = 0;
        Read(addr, &v, 4);
        return KR::FromBits(v);
    }
    int writes = 0;

private:
    std::map<std::uint32_t, std::uint8_t> bytes_;
};

constexpr KR::Layout kL{};

/// Puts a shot table at addr, the way the game's class lays it out (the rest of its bytes are zero).
void PutTable(FakeMemory& m, std::uint32_t addr, std::uint8_t exit, float speed, float distance,
              float exist = 0.f, float exist2 = 0.f) {
    m.Map(addr, 0xC4);
    m.SetU32(addr, kL.table_vtable);
    std::uint32_t w = 0;
    m.Read(addr + kL.off_exit - (kL.off_exit % 4), &w, 4);
    w = (w & ~(0xFFu << ((kL.off_exit % 4) * 8))) | (std::uint32_t{exit} << ((kL.off_exit % 4) * 8));
    m.SetU32(addr + kL.off_exit - (kL.off_exit % 4), w);
    m.SetF(addr + kL.off_speed, speed);
    m.SetF(addr + kL.off_distance, distance);
    m.SetF(addr + kL.off_exist_frame, exist);
    m.SetF(addr + kL.off_exist_frame + 4, exist2);
}

constexpr std::uint32_t kRoot = 0x14100000;

/// A document at kRoot: root slot set, three length tables, one time-exit table, one stray
/// object of another class, one table-looking object with garbage values.
void BuildBattle(FakeMemory& m, std::uint32_t root = kRoot) {
    m.SetU32(kL.root_slot, root);
    m.Map(root, 0x8000);
    PutTable(m, root + 0x1000, 1, 2.0f, 300.0f);
    PutTable(m, root + 0x1100, 1, 9.0f, 100.0f);
    PutTable(m, root + 0x1200, 1, 4.0f, 0.0f);          // length exit with no distance: left alone
    PutTable(m, root + 0x1300, 2, 0.0f, 50.0f, 210.f, 3.5f); // time exit, no speed
    m.SetU32(root + 0x1400, 0x00D12345);                 // another class
    PutTable(m, root + 0x1500, 1, 1.0f, 1e30f);          // garbage distance: rejected
}

} // namespace

TEST_CASE("KiuRange: finds the tables and scales length exits", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    REQUIRE(r.Rescan(m) == 4);
    REQUIRE(r.Apply(m, 2.0f) == 2);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 600.0f);
    CHECK(m.GetF(kRoot + 0x1100 + kL.off_distance) == 200.0f);
    CHECK(m.GetF(kRoot + 0x1200 + kL.off_distance) == 0.0f);
    CHECK(m.GetF(kRoot + 0x1300 + kL.off_distance) == 50.0f);
    CHECK(m.GetF(kRoot + 0x1300 + kL.off_exist_frame) == 210.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_speed) == 2.0f); // the speed is not touched
    CHECK(m.GetF(kRoot + 0x1500 + kL.off_distance) == 1e30f);
}

TEST_CASE("KiuRange: applying twice is the same as once", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 2.0f); // no Rescan first: Apply finds the root itself
    const int writes = m.writes;
    r.Apply(m, 2.0f);
    r.Apply(m, 2.0f);
    CHECK(m.writes == writes);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 600.0f);
}

TEST_CASE("KiuRange: a factor change never compounds", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 2.0f);
    r.Apply(m, 3.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 900.0f);
    r.Apply(m, 1.5f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 450.0f);
    r.Apply(m, 1.0f);
    CHECK(KR::Bits(m.GetF(kRoot + 0x1000 + kL.off_distance)) == KR::Bits(300.0f));
    CHECK(KR::Bits(m.GetF(kRoot + 0x1100 + kL.off_distance)) == KR::Bits(100.0f));
}

TEST_CASE("KiuRange: the factor is clamped to 1..3", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 10.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 900.0f);
    r.Apply(m, 0.2f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 300.0f);
    r.Apply(m, std::nanf(""));
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 600.0f); // not finite: the default
}

TEST_CASE("KiuRange: a table the game parses again becomes the new original", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 2.0f);
    // The game re-parses in place: the original values are back.
    m.SetF(kRoot + 0x1000 + kL.off_distance, 300.0f);
    m.SetF(kRoot + 0x1100 + kL.off_distance, 100.0f);
    r.Apply(m, 2.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 600.0f); // not 1200
    CHECK(m.GetF(kRoot + 0x1100 + kL.off_distance) == 200.0f);
    // A different weapon is parsed into the same slot.
    m.SetF(kRoot + 0x1000 + kL.off_distance, 410.0f);
    r.Apply(m, 3.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == 1230.0f);
    CHECK(m.GetF(kRoot + 0x1100 + kL.off_distance) == 300.0f);
}

TEST_CASE("KiuRange: a new document (battle reload) is found without touching the old one", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 2.0f);
    constexpr std::uint32_t kRoot2 = 0x14300000;
    BuildBattle(m, kRoot2); // the root slot now points at the new document
    const float old_dist = m.GetF(kRoot + 0x1000 + kL.off_distance);
    CHECK(r.Apply(m, 2.0f) == 2);
    CHECK(m.GetF(kRoot2 + 0x1000 + kL.off_distance) == 600.0f);
    CHECK(m.GetF(kRoot + 0x1000 + kL.off_distance) == old_dist); // the old one is left as it was
    CHECK(r.Tables().size() == 4);
}

TEST_CASE("KiuRange: a table that vanished is dropped, and nothing is written there", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 2.0f);
    m.SetU32(kRoot + 0x1100, 0); // freed: the vtable is gone
    m.SetF(kRoot + 0x1100 + kL.off_distance, 77.0f);
    CHECK(r.Apply(m, 2.0f) == 1);
    CHECK(m.GetF(kRoot + 0x1100 + kL.off_distance) == 77.0f);
}

TEST_CASE("KiuRange: Restore puts the originals back", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    KR::Range r;
    r.Apply(m, 3.0f);
    r.Restore(m);
    CHECK(KR::Bits(m.GetF(kRoot + 0x1000 + kL.off_distance)) == KR::Bits(300.0f));
    CHECK(KR::Bits(m.GetF(kRoot + 0x1100 + kL.off_distance)) == KR::Bits(100.0f));
    CHECK(r.Tables().empty());
}

TEST_CASE("KiuRange: guards: no root, a root outside the heap, unmapped memory", "[frame_xr][kiurange]") {
    FakeMemory m;
    KR::Range r;
    CHECK(r.Apply(m, 2.0f) == 0); // the root slot is not even mapped
    m.SetU32(kL.root_slot, 0x00100000); // not in the heap
    CHECK(r.Apply(m, 2.0f) == 0);
    m.SetU32(kL.root_slot, 0x14100002); // not aligned
    CHECK(r.Apply(m, 2.0f) == 0);
    m.SetU32(kL.root_slot, kRoot); // in the heap but nothing mapped there
    CHECK(r.Apply(m, 2.0f) == 0);
    CHECK(m.writes == 0);
}

TEST_CASE("KiuRange: an arena that ends before the window is read as far as it goes", "[frame_xr][kiurange]") {
    FakeMemory m;
    m.SetU32(kL.root_slot, kRoot);
    m.Map(kRoot, 0x3000);
    PutTable(m, kRoot + 0x2000, 1, 2.0f, 300.0f);
    KR::Range r;
    CHECK(r.Apply(m, 2.0f) == 1);
    CHECK(m.GetF(kRoot + 0x2000 + kL.off_distance) == 600.0f);
}

TEST_CASE("KiuRange: time exits scale only when their rule is on", "[frame_xr][kiurange]") {
    FakeMemory m;
    BuildBattle(m);
    PutTable(m, kRoot + 0x2000, 2, 5.0f, 0.0f, 40.f, 0.66f);
    KR::Rules rules;
    rules.exit_time = 2;
    KR::Range r({}, rules);
    r.Apply(m, 2.0f);
    CHECK(m.GetF(kRoot + 0x2000 + kL.off_exist_frame) == 80.0f);
    CHECK(m.GetF(kRoot + 0x2000 + kL.off_exist_frame + 4) == 1.32f);
    CHECK(m.GetF(kRoot + 0x1300 + kL.off_exist_frame) == 210.0f); // speed 0: left alone
    r.Apply(m, 1.0f);
    CHECK(m.GetF(kRoot + 0x2000 + kL.off_exist_frame) == 40.0f);
}
