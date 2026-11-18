// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <utility>
#include <vector>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/input/kiu_hooks.h"

// AzaharFrame kiuhook: the game-code patches' pure part (frame_xr/input/kiu_hooks.h) over a fake
// memory: the table, putting groups in and out, what is refused, the cave and its image, the
// mailbox's layout, the player's guards, the small maths. The words used for "something else" are
// invented (never another build's).
using namespace FrameXr::Input::KiuHooks;
using Catch::Approx;

namespace {

constexpr uint32_t kInvented = 0xE1A00000; // mov r0, r0: neither the game's word nor ours

/// The game's memory: only the bytes set are mapped; every write is logged in order.
class FakeMemory final : public GameMemory {
public:
    struct Write {
        uint32_t addr;
        uint32_t value;
        bool code;
    };
    void Set32(uint32_t addr, uint32_t v) {
        for (uint32_t i = 0; i < 4; ++i) {
            bytes_[addr + i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }
    void SetF(uint32_t addr, float f) {
        Set32(addr, std::bit_cast<uint32_t>(f));
    }
    void Set8(uint32_t addr, uint8_t v) {
        bytes_[addr] = v;
    }
    void Unmap32(uint32_t addr) {
        for (uint32_t i = 0; i < 4; ++i) {
            bytes_.erase(addr + i);
        }
    }
    uint32_t Get32(uint32_t addr) const {
        uint32_t v = 0;
        REQUIRE(Read32(addr, v));
        return v;
    }
    bool Read32(uint32_t addr, uint32_t& v) const override {
        if ((addr & 3u) != 0) {
            return false;
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            const auto it = bytes_.find(addr + i);
            if (it == bytes_.end()) {
                return false;
            }
            out |= static_cast<uint32_t>(it->second) << (8 * i);
        }
        v = out;
        return true;
    }
    bool Read8(uint32_t addr, uint8_t& v) const override {
        const auto it = bytes_.find(addr);
        if (it == bytes_.end()) {
            return false;
        }
        v = it->second;
        return true;
    }
    bool WriteCode(uint32_t addr, uint32_t value) override {
        return Store(addr, value, true);
    }
    bool WriteData(uint32_t addr, uint32_t value) override {
        return Store(addr, value, false);
    }
    std::vector<Write> writes;

private:
    bool Store(uint32_t addr, uint32_t value, bool code) {
        uint32_t old = 0;
        if (!Read32(addr, old)) {
            return false;
        }
        Set32(addr, value);
        writes.push_back({addr, value, code});
        return true;
    }
    std::map<uint32_t, uint8_t> bytes_;
};

/// The European game's code where the hooks reach: every site and context word, kiu_turn.h's
/// nine, and an empty cave.
FakeMemory Game() {
    FakeMemory m;
    for (const Patch& p : kPatches) {
        m.Set32(p.addr, p.original);
    }
    for (const Context& c : kContext) {
        m.Set32(c.addr, c.word);
    }
    for (const auto& w : FrameXr::Input::KiuTurn::kCodeCheck) {
        m.Set32(w.addr, w.word);
    }
    for (uint32_t addr = kCaveStart; addr < kCaveEnd; addr += 4) {
        m.Set32(addr, 0);
    }
    return m;
}

constexpr uint32_t kAll = Bit(Group::NoCull) | Bit(Group::BulletKeep) | Bit(Group::Eye) |
                          Bit(Group::Shot);

bool SiteIs(const FakeMemory& m, Group g, bool patched) {
    bool all = true;
    for (const Patch& p : kPatches) {
        if (p.group == g) {
            all = all && m.Get32(p.addr) == (patched ? p.patched : p.original);
        }
    }
    return all;
}

bool ImageInPlace(const FakeMemory& m) {
    for (std::size_t i = 0; i < Stubs::kImage.size(); ++i) {
        if (m.Get32(kImageBase + static_cast<uint32_t>(i) * 4u) != Stubs::kImage[i]) {
            return false;
        }
    }
    return true;
}

constexpr uint32_t kRoot = 0x14100000, kAimLink = 0x14101010, kOwner = 0x14102000,
                   kActor = 0x14103000, kNameAt = 0x14104000, kNode = 0x14105000;

/// A battle with the player: the chain from the aim root to the actor, as the game has it.
void AddPlayer(FakeMemory& m, bool land) {
    namespace KT = FrameXr::Input::KiuTurn;
    m.Set8(KT::kPlaying, 1);
    m.Set32(KT::kAimRoot, kRoot);
    m.Set32(kRoot + KT::kAimRootNext, kAimLink);
    const uint32_t aim = kAimLink - KT::kAimBack;
    m.Set32(aim, land ? KT::kAimVtableLand : KT::kAimVtableAir);
    m.Set32(aim + KT::kAimOwner, kOwner);
    m.Set32(kOwner, kOwnerVtable);
    m.Set32(kOwner + kOwnerActor, kActor);
    m.Set32(kOwner + kOwnerEyeNode, kNode);
    m.Set32(kActor, kActorVtable);
    m.Set32(kActor + kActorName, kNameAt);
    const char name[] = "CHID_PIT";
    for (uint32_t i = 0; i < sizeof(name); ++i) {
        m.Set8(kNameAt + i, static_cast<uint8_t>(name[i]));
    }
    const float world_q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (uint32_t i = 0; i < 4; ++i) {
        m.SetF(kActor + kActorWorldQuat + 4 * i, world_q[i]);
        m.SetF(kActor + kActorLocalQuat + 4 * i, world_q[i]);
    }
    const float pos[3] = {1.0f, 2.0f, 300.0f};
    for (uint32_t i = 0; i < 3; ++i) {
        m.SetF(kActor + kActorWorldPos + 4 * i, pos[i]);
        m.SetF(kActor + kActorLocalPos + 4 * i, pos[i]);
    }
    m.SetF(kActor + kActorLocalScale, 1.0f);
}

void SetQuat(FakeMemory& m, uint32_t addr, const Quat& q) {
    m.SetF(addr, q.x);
    m.SetF(addr + 4, q.y);
    m.SetF(addr + 8, q.z);
    m.SetF(addr + 12, q.w);
}

} // namespace

TEST_CASE("Kid Icarus hooks: branch words and the table", "[frame_xr][kiuhook]") {
    // The encoders against words of the game's own code.
    CHECK(Bl(0x5043E4, 0x485638) == 0xEBFE0493);
    CHECK(Bl(0x41FF1C, 0x4EC640) == 0xEB0331C7);
    CHECK(B(0x77DB7C, 0x77DB9C) == 0xEA000006);
    CHECK(B(0x504390, 0x5042E8) == 0xEAFFFFD4);
    // A branch into the cave and back decodes to where it was aimed.
    const auto target = [](uint32_t from, uint32_t word) {
        const int32_t off = static_cast<int32_t>(word << 8) >> 6;
        return from + 8u + static_cast<uint32_t>(off);
    };
    CHECK(target(kSiteCameraSink, B(kSiteCameraSink, kEyeStub)) == kEyeStub);
    CHECK(target(kSiteShotSpawn, Bl(kSiteShotSpawn, kShotStub)) == kShotStub);
    CHECK((Bl(kSiteShotSpawn, kShotStub) >> 24) == 0xEB);
    CHECK((B(kSiteCameraSink, kEyeStub) >> 24) == 0xEA);

    for (std::size_t i = 0; i < kPatches.size(); ++i) {
        const Patch& p = kPatches[i];
        INFO("patch " << i);
        CHECK((p.addr & 3u) == 0);
        CHECK(p.addr >= 0x100000);
        CHECK(p.addr < kCaveStart); // in the game's own code, never in the cave
        CHECK(p.original != p.patched);
        for (std::size_t j = 0; j < i; ++j) {
            CHECK(kPatches[j].addr != p.addr);
        }
        for (const Context& c : kContext) {
            CHECK(c.addr != p.addr); // a context word is never one we change
        }
    }
    // The stubs' entries are inside the image, the image inside the cave, below the mailbox.
    CHECK(kEyeStub > kImageBase);
    CHECK(kShotStub > kEyeStub);
    CHECK(kShotStub < kImageBase + kImageBytes);
    CHECK(kImageBase + kImageBytes <= kMailbox);
    CHECK(kMailbox + kMailboxWords * 4 <= kCaveEnd);
    // The eye stub's first word is the instruction its site loses; both stubs end by loading pc
    // from a literal (ldr pc, [pc, #n]).
    CHECK(Stubs::kImage[Stubs::kEyeStub / 4] == kPatches[4].original);
    CHECK(kPatches[4].addr == kSiteCameraSink);
    CHECK((Stubs::kImage[Stubs::kShotStub / 4 - 1] & 0xFFFFF000u) == 0xE59FF000u);
    CHECK((Stubs::kImage[Stubs::kLitMailbox / 4 - 1] & 0xFFFFF000u) == 0xE59FF000u);
    CHECK(Stubs::kImage[Stubs::kLitNoTarget / 4] == 0xFFFFFE00u);
}

TEST_CASE("Kid Icarus hooks: the groups go in, stay, and come out", "[frame_xr][kiuhook]") {
    FakeMemory m = Game();
    REQUIRE(GameCodeMatches(m));
    REQUIRE_FALSE(AnyOurs(m));

    // Nothing wanted: nothing read as ours, nothing written.
    SyncResult r = Sync(m, 0, Mailbox{});
    CHECK(r.code_writes == 0);
    CHECK(m.writes.empty());
    CHECK(r.cave == Cave::NotLooked);
    for (std::size_t g = 0; g < kGroupCount; ++g) {
        CHECK(r.state[g] == GroupState::Out);
    }

    SECTION("a plain group: two words, no cave") {
        r = Sync(m, Bit(Group::NoCull), Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::In);
        CHECK(r.in_mask == Bit(Group::NoCull));
        CHECK(r.code_writes == 2);
        CHECK(r.image_writes == 0);
        CHECK(r.cave == Cave::NotLooked);
        CHECK(m.Get32(0x77DB44) == 0xEA000040); // b 0x77DC4C
        CHECK(m.Get32(0x773294) == 0xEAFFFFF9); // b 0x773280
        CHECK(SiteIs(m, Group::BulletKeep, false));
        CHECK(m.Get32(kImageBase) == 0); // the cave untouched
        for (const auto& w : m.writes) {
            CHECK(w.code); // every code word goes through the write that drops the code cache
        }
        // Again: nothing to do, nothing written.
        const std::size_t before = m.writes.size();
        r = Sync(m, Bit(Group::NoCull), Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::In);
        CHECK(r.code_writes == 0);
        CHECK(m.writes.size() == before);
        // Out: the game's words are back.
        r = Sync(m, 0, Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::Out);
        CHECK(r.code_writes == 2);
        CHECK(SiteIs(m, Group::NoCull, false));
        CHECK_FALSE(AnyOurs(m));
        r = Sync(m, 0, Mailbox{});
        CHECK(r.code_writes == 0);
    }

    SECTION("the bullet sites") {
        r = Sync(m, Bit(Group::BulletKeep), Mailbox{});
        CHECK(r.Of(Group::BulletKeep) == GroupState::In);
        CHECK(m.Get32(0x41FF1C) == 0xE3A00001);
        CHECK(m.Get32(0x40BD6C) == 0xE3A00001);
        CHECK(SiteIs(m, Group::NoCull, false));
    }

    SECTION("a stub group: the image, then the mailbox, the site last") {
        Mailbox mb;
        mb.player = 0x141DB1E0;
        mb.eye_on = true;
        r = Sync(m, Bit(Group::Eye), mb);
        CHECK(r.Of(Group::Eye) == GroupState::In);
        CHECK(r.cave == Cave::Ours);
        CHECK(r.image_writes == Stubs::kImage.size());
        CHECK(r.code_writes == Stubs::kImage.size() + 1);
        CHECK(r.mailbox_written);
        CHECK(ImageInPlace(m));
        CHECK(m.Get32(kSiteCameraSink) == B(kSiteCameraSink, kEyeStub));
        CHECK(m.Get32(kSiteShotSpawn) == 0xEBFE0493); // the other stub group's site untouched
        // The order of the writes: every image word, then every mailbox word, then the site.
        REQUIRE(m.writes.size() == Stubs::kImage.size() + kMailboxWords + 1);
        for (std::size_t i = 0; i < m.writes.size(); ++i) {
            const auto& w = m.writes[i];
            if (i < Stubs::kImage.size()) {
                CHECK(w.addr == kImageBase + i * 4);
                CHECK(w.code);
            } else if (i < Stubs::kImage.size() + kMailboxWords) {
                CHECK(w.addr == kMailbox + (i - Stubs::kImage.size()) * 4);
                CHECK_FALSE(w.code);
            } else {
                CHECK(w.addr == kSiteCameraSink);
                CHECK(w.code);
            }
        }
        // Again: only the mailbox is written (it changes every VBlank), no code word.
        m.writes.clear();
        r = Sync(m, Bit(Group::Eye), mb);
        CHECK(r.Of(Group::Eye) == GroupState::In);
        CHECK(r.code_writes == 0);
        CHECK(m.writes.size() == kMailboxWords);
        // The second stub group shares the image: one more code word.
        m.writes.clear();
        r = Sync(m, Bit(Group::Eye) | Bit(Group::Shot), mb);
        CHECK(r.in_mask == (Bit(Group::Eye) | Bit(Group::Shot)));
        CHECK(r.code_writes == 1);
        CHECK(r.image_writes == 0);
        CHECK(m.Get32(kSiteShotSpawn) == Bl(kSiteShotSpawn, kShotStub));
        // Out: the sites go back; the image stays (a guest thread may be inside it) and the
        // cave is not touched again.
        m.writes.clear();
        r = Sync(m, 0, Mailbox{});
        CHECK(r.code_writes == 2);
        CHECK(m.writes.size() == 2);
        CHECK(SiteIs(m, Group::Eye, false));
        CHECK(SiteIs(m, Group::Shot, false));
        CHECK(ImageInPlace(m));
        CHECK(r.cave == Cave::NotLooked);
        CHECK_FALSE(AnyOurs(m));
    }

    SECTION("everything at once, and out again") {
        r = Sync(m, kAll, Mailbox{});
        CHECK(r.in_mask == kAll);
        CHECK(r.code_writes == Stubs::kImage.size() + kPatches.size());
        for (const Patch& p : kPatches) {
            CHECK(m.Get32(p.addr) == p.patched);
        }
        r = Sync(m, 0, Mailbox{});
        CHECK(r.in_mask == 0);
        for (const Patch& p : kPatches) {
            CHECK(m.Get32(p.addr) == p.original);
        }
    }
}

TEST_CASE("Kid Icarus hooks: a word that differs keeps its group out", "[frame_xr][kiuhook]") {
    FakeMemory m = Game();

    SECTION("a site holds something else") {
        m.Set32(0x773294, kInvented);
        SyncResult r = Sync(m, kAll, Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::Different);
        CHECK(r.different_addr == 0x773294);
        CHECK(r.different_word == kInvented);
        // Neither of the group's words is written (all or nothing); the others go in.
        CHECK(m.Get32(0x77DB44) == 0xE20100FF);
        CHECK(m.Get32(0x773294) == kInvented);
        CHECK(r.Of(Group::BulletKeep) == GroupState::In);
        CHECK(r.Of(Group::Eye) == GroupState::In);
        CHECK(r.Of(Group::Shot) == GroupState::In);
        for (const auto& w : m.writes) {
            CHECK(w.addr != 0x773294);
            CHECK(w.addr != 0x77DB44);
        }
        // Still so at the next pass, and with nothing wanted the foreign word stays as it is.
        r = Sync(m, kAll, Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::Different);
        r = Sync(m, 0, Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::Out);
        CHECK(m.Get32(0x773294) == kInvented);
    }

    SECTION("a site changes under us: our other word is taken out") {
        SyncResult r = Sync(m, Bit(Group::NoCull), Mailbox{});
        REQUIRE(r.Of(Group::NoCull) == GroupState::In);
        m.Set32(0x77DB44, kInvented); // something else wrote there
        r = Sync(m, Bit(Group::NoCull), Mailbox{});
        CHECK(r.Of(Group::NoCull) == GroupState::Different);
        CHECK(m.Get32(0x773294) == 0xE1A01004); // ours, put back
        CHECK(m.Get32(0x77DB44) == kInvented);  // theirs, left
    }

    SECTION("a context word differs") {
        m.Set32(0x6340A4, kInvented); // the code after the eye's site is not the game's
        SyncResult r = Sync(m, Bit(Group::Eye) | Bit(Group::Shot), Mailbox{});
        CHECK(r.Of(Group::Eye) == GroupState::Different);
        CHECK(r.different_addr == 0x6340A4);
        CHECK(m.Get32(kSiteCameraSink) == 0xE92D4070);
        CHECK(r.Of(Group::Shot) == GroupState::In);
    }

    SECTION("a site is not mapped") {
        m.Unmap32(0x41FF1C);
        SyncResult r = Sync(m, kAll, Mailbox{});
        CHECK(r.Of(Group::BulletKeep) == GroupState::Unreadable);
        CHECK(m.Get32(0x40BD6C) == 0xEB038233);
        CHECK(r.Of(Group::NoCull) == GroupState::In);
    }

    SECTION("another region or build: the code check fails before anything") {
        m.Set32(0x704178, kInvented);
        CHECK_FALSE(GameCodeMatches(m));
    }
}

TEST_CASE("Kid Icarus hooks: the cave", "[frame_xr][kiuhook]") {
    FakeMemory m = Game();

    SECTION("something else lives in the cave: the stub groups stay out, the others go in") {
        m.Set32(kCaveEnd - 0x40, kInvented);
        CHECK(LookAtCave(m) == Cave::Foreign);
        SyncResult r = Sync(m, kAll, Mailbox{});
        CHECK(r.cave == Cave::Foreign);
        CHECK(r.Of(Group::Eye) == GroupState::NoCave);
        CHECK(r.Of(Group::Shot) == GroupState::NoCave);
        CHECK(r.Of(Group::NoCull) == GroupState::In);
        CHECK(r.Of(Group::BulletKeep) == GroupState::In);
        CHECK(r.image_writes == 0);
        CHECK_FALSE(r.mailbox_written);
        CHECK(m.Get32(kImageBase) == 0);
        CHECK(m.Get32(kMailbox) == 0);
        CHECK(m.Get32(kSiteCameraSink) == 0xE92D4070);
        CHECK(m.Get32(kSiteShotSpawn) == 0xEBFE0493);
    }

    SECTION("foreign words where the image goes") {
        m.Set32(kImageBase + 0x20, kInvented);
        CHECK(LookAtCave(m) == Cave::Foreign);
        SyncResult r = Sync(m, Bit(Group::Shot), Mailbox{});
        CHECK(r.Of(Group::Shot) == GroupState::NoCave);
        CHECK(m.Get32(kImageBase + 0x20) == kInvented);
    }

    SECTION("an older image of ours (its tag) is replaced") {
        m.Set32(kImageBase, Stubs::kImageTag);
        m.Set32(kImageBase + 4, Stubs::kImageVersion + 7);
        m.Set32(kImageBase + 8, kInvented);
        CHECK(LookAtCave(m) == Cave::Older);
        SyncResult r = Sync(m, Bit(Group::Eye), Mailbox{});
        CHECK(r.cave == Cave::Ours);
        CHECK(r.image_writes == Stubs::kImage.size());
        CHECK(ImageInPlace(m));
        CHECK(r.Of(Group::Eye) == GroupState::In);
    }

    SECTION("the cave is not mapped") {
        m.Unmap32(kImageBase + 0x10);
        SyncResult r = Sync(m, Bit(Group::Eye), Mailbox{});
        CHECK(r.cave == Cave::Unreadable);
        CHECK(r.Of(Group::Eye) == GroupState::Unreadable);
        CHECK(m.Get32(kSiteCameraSink) == 0xE92D4070);
    }

    SECTION("the image is ours already and the cave is looked at only for stub groups") {
        Sync(m, Bit(Group::Shot), Mailbox{});
        m.writes.clear();
        CHECK(LookAtCave(m) == Cave::Ours);
        const SyncResult r = Sync(m, Bit(Group::NoCull), Mailbox{});
        CHECK(r.cave == Cave::NotLooked);
        CHECK(m.Get32(kSiteShotSpawn) == 0xEBFE0493);
    }
}

TEST_CASE("Kid Icarus hooks: a save state loaded under us", "[frame_xr][kiuhook]") {
    FakeMemory m = Game();
    Mailbox mb;
    mb.player = 0x141DB1E0;
    mb.eye_on = true;
    REQUIRE(Sync(m, kAll, mb).in_mask == kAll);
    const FakeMemory patched = m; // a state saved with everything in

    SECTION("a state from before the patches: they go in again at the next pass") {
        m = Game();
        const SyncResult r = Sync(m, kAll, mb);
        CHECK(r.in_mask == kAll);
        CHECK(r.code_writes == Stubs::kImage.size() + kPatches.size());
    }

    SECTION("a state with the patches in, every switch off: they come out") {
        m = patched;
        m.writes.clear();
        REQUIRE(AnyOurs(m));
        const SyncResult r = Sync(m, 0, Mailbox{});
        CHECK(r.code_writes == kPatches.size());
        CHECK_FALSE(AnyOurs(m));
        for (const Patch& p : kPatches) {
            CHECK(m.Get32(p.addr) == p.original);
        }
    }

    SECTION("a state with the patches in, the switches on: adopted, nothing rewritten") {
        m = patched;
        m.writes.clear();
        const SyncResult r = Sync(m, kAll, mb);
        CHECK(r.in_mask == kAll);
        CHECK(r.code_writes == 0);
        CHECK(m.writes.size() == kMailboxWords); // the mailbox only
    }

    SECTION("a state with a stale mailbox: the first pass overwrites it before any stub runs") {
        m = patched;
        Mailbox off; // no player: all flags down
        Sync(m, kAll, off);
        CHECK(m.Get32(kMailbox + Stubs::kMbPlayer) == 0);
        CHECK(m.Get32(kMailbox + Stubs::kMbEyeFlags) == 0);
        CHECK(m.Get32(kMailbox + Stubs::kMbShotFlags) == 0);
    }
}

TEST_CASE("Kid Icarus hooks: the mailbox's layout", "[frame_xr][kiuhook]") {
    // The offsets the stubs were assembled with (tools/kiu_hooks/stubs.s).
    CHECK(Stubs::kMbMagic == 0x00);
    CHECK(Stubs::kMbSerial == 0x04);
    CHECK(Stubs::kMbPlayer == 0x08);
    CHECK(Stubs::kMbEyeNode == 0x0C);
    CHECK(Stubs::kMbEyeFlags == 0x10);
    CHECK(Stubs::kMbEyeOffset == 0x14);
    CHECK(Stubs::kMbShotFlags == 0x20);
    CHECK(Stubs::kMbShotRotation == 0x24);
    CHECK(Stubs::kMbShotFrame == 0x34);
    CHECK(Stubs::kMbShotOffset == 0x44);
    CHECK(Stubs::kMbSize == 0x50);
    // The stubs read those offsets off r12: ldr rX, [r12, #imm] = 0xE59C?imm.
    const auto loads = [](uint32_t offset) {
        int n = 0;
        for (const uint32_t w : Stubs::kImage) {
            n += (w & 0xFFFF0FFFu) == (0xE59C0000u | offset) ? 1 : 0;
        }
        return n;
    };
    CHECK(loads(Stubs::kMbEyeFlags) == 1);
    CHECK(loads(Stubs::kMbEyeNode) == 1);
    CHECK(loads(Stubs::kMbPlayer) == 2); // both stubs
    CHECK(loads(Stubs::kMbShotFlags) == 1);
    // and the float blocks through add rX, r12, #imm = 0xE28C?0imm.
    const auto adds = [](uint32_t offset) {
        int n = 0;
        for (const uint32_t w : Stubs::kImage) {
            n += (w & 0xFFFF0FFFu) == (0xE28C0000u | offset) ? 1 : 0;
        }
        return n;
    };
    CHECK(adds(Stubs::kMbEyeOffset) == 1);
    CHECK(adds(Stubs::kMbShotRotation) == 1);
    CHECK(adds(Stubs::kMbShotFrame) == 1);
    CHECK(adds(Stubs::kMbShotOffset) == 1);

    // All zero but the tag: every stub passes the game through.
    const auto empty = Mailbox{}.Words();
    CHECK(empty[0] == 0x4D55494B); // "KIUM"
    CHECK(empty[Stubs::kMbPlayer / 4] == 0);
    CHECK(empty[Stubs::kMbEyeFlags / 4] == 0);
    CHECK(empty[Stubs::kMbShotFlags / 4] == 0);
    CHECK(std::bit_cast<float>(empty[Stubs::kMbShotRotation / 4 + 3]) == 1.0f); // identity
    CHECK(std::bit_cast<float>(empty[Stubs::kMbShotFrame / 4 + 3]) == 1.0f);

    Mailbox mb;
    mb.serial = 77;
    mb.player = 0x141DB1E0;
    mb.eye_node = 0x141DC000;
    mb.eye_on = true;
    mb.eye_offset = {1.0f, 12.3f, -2.0f};
    mb.shot_rotate = true;
    mb.shot_move = true;
    mb.shot_no_target = true;
    mb.shot_rotation = {0.1f, 0.2f, 0.3f, 0.9f};
    mb.shot_frame = {0.4f, 0.5f, 0.6f, 0.7f};
    mb.shot_offset = {20.0f, 5.0f, -1.0f};
    const auto w = mb.Words();
    const auto f = [&w](uint32_t offset) { return std::bit_cast<float>(w[offset / 4]); };
    CHECK(w[0x04 / 4] == 77);
    CHECK(w[0x08 / 4] == 0x141DB1E0);
    CHECK(w[0x0C / 4] == 0x141DC000);
    CHECK(w[0x10 / 4] == 1);
    CHECK(f(0x14) == 1.0f);
    CHECK(f(0x18) == 12.3f);
    CHECK(f(0x1C) == -2.0f);
    CHECK(w[0x20 / 4] == 7);
    CHECK(f(0x24) == 0.1f);
    CHECK(f(0x30) == 0.9f);
    CHECK(f(0x34) == 0.4f);
    CHECK(f(0x40) == 0.7f);
    CHECK(f(0x44) == 20.0f);
    CHECK(f(0x4C) == -1.0f);
    mb.shot_rotate = false;
    mb.shot_no_target = false;
    CHECK(mb.Words()[0x20 / 4] == 2);

    // Sync puts exactly these words at the mailbox's address.
    FakeMemory m = Game();
    Sync(m, Bit(Group::Shot), mb);
    const auto now = mb.Words();
    for (uint32_t i = 0; i < kMailboxWords; ++i) {
        CHECK(m.Get32(kMailbox + i * 4) == now[i]);
    }
}

TEST_CASE("Kid Icarus hooks: finding the player", "[frame_xr][kiuhook]") {
    namespace KT = FrameXr::Input::KiuTurn;
    FakeMemory m;
    CHECK(FindPlayer(m).guard == PlayerGuard::NoRoot);
    AddPlayer(m, true);
    Player p = FindPlayer(m);
    REQUIRE(p.Ok());
    CHECK(p.land);
    CHECK(p.actor == kActor);
    CHECK(p.owner == kOwner);
    CHECK(p.eye_node == kNode);
    CHECK(p.world_p.z == 300.0f);
    CHECK(p.local_scale_bits == std::bit_cast<uint32_t>(1.0f));

    SECTION("the air battle's aim") {
        m.Set32(kAimLink - KT::kAimBack, KT::kAimVtableAir);
        p = FindPlayer(m);
        CHECK(p.Ok());
        CHECK_FALSE(p.land);
    }
    SECTION("not playing: the actor is still named, nothing acts") {
        m.Set8(KT::kPlaying, 0);
        p = FindPlayer(m);
        CHECK(p.guard == PlayerGuard::NotPlaying);
        CHECK(p.actor == kActor);
        const Mailbox mb = Fill(p, EyeRequest{true, kEyeOffsetDefault}, ShotRequest{}, 1);
        CHECK(mb.player == 0);
        CHECK_FALSE(mb.eye_on);
    }
    SECTION("each link of the chain") {
        m.Set32(KT::kAimRoot, 0);
        CHECK(FindPlayer(m).guard == PlayerGuard::NoRoot);
        m.Set32(KT::kAimRoot, 0x15000000); // not mapped
        CHECK(FindPlayer(m).guard == PlayerGuard::NoRoot);
        m.Set32(KT::kAimRoot, kRoot);
        m.Set32(kAimLink - KT::kAimBack, 0xCF0000); // another class
        CHECK(FindPlayer(m).guard == PlayerGuard::NoAim);
        m.Set32(kAimLink - KT::kAimBack, KT::kAimVtableLand);
        m.Set32(kOwner, 0xCE0000);
        CHECK(FindPlayer(m).guard == PlayerGuard::NoOwner);
        m.Set32(kOwner, kOwnerVtable);
        m.Set32(kActor, 0xCA0000);
        CHECK(FindPlayer(m).guard == PlayerGuard::NoActor);
        m.Set32(kActor, kActorVtable);
        m.Set8(kNameAt + 8, ':'); // "CHID_PIT:<something>": one of his bullets, not him
        CHECK(FindPlayer(m).guard == PlayerGuard::NoActor);
        m.Set8(kNameAt + 8, 0);
        m.SetF(kActor + kActorWorldPos + 4, std::nanf(""));
        p = FindPlayer(m);
        CHECK(p.guard == PlayerGuard::Unreadable);
        CHECK(p.actor == 0);
        m.SetF(kActor + kActorWorldPos + 4, 2.0f);
        m.Set32(kOwner + kOwnerEyeNode, 0);
        CHECK(FindPlayer(m).guard == PlayerGuard::NoEyeNode);
        m.Set32(kOwner + kOwnerEyeNode, kNode);
        CHECK(FindPlayer(m).Ok());
    }
}

TEST_CASE("Kid Icarus hooks: filling the mailbox", "[frame_xr][kiuhook]") {
    const float pi = std::numbers::pi_v<float>;
    // The product and the stub's way of applying a quaternion agree with each other.
    const Quat a = Normalized({0.1f, 0.4f, -0.2f, 0.8f});
    const Quat b = Normalized({-0.3f, 0.2f, 0.5f, 0.6f});
    const Vec3 v{1.5f, -2.0f, 3.0f};
    const Vec3 one = Rotate(Mul(a, b), v);
    const Vec3 two = Rotate(a, Rotate(b, v));
    CHECK(one.x == Approx(two.x).margin(1e-5));
    CHECK(one.y == Approx(two.y).margin(1e-5));
    CHECK(one.z == Approx(two.z).margin(1e-5));
    const Vec3 back = Rotate(Conj(a), Rotate(a, v));
    CHECK(back.x == Approx(v.x).margin(1e-5));
    CHECK(back.z == Approx(v.z).margin(1e-5));
    // A quarter turn about +y takes +z to +x (the right-hand rule).
    const Vec3 turned = Rotate(AxisAngle({0.0f, 1.0f, 0.0f}, pi / 2), {0.0f, 0.0f, 1.0f});
    CHECK(turned.x == Approx(1.0f).margin(1e-6));
    CHECK(turned.z == Approx(0.0f).margin(1e-6));

    FakeMemory m;
    AddPlayer(m, false);
    // The player faces -z (a half turn about y) under a parent turned a quarter about y.
    const Quat parent = AxisAngle({0.0f, 1.0f, 0.0f}, pi / 2);
    const Quat local = AxisAngle({0.0f, 1.0f, 0.0f}, pi);
    SetQuat(m, kActor + kActorLocalQuat, local);
    SetQuat(m, kActor + kActorWorldQuat, Mul(parent, local));
    Player p = FindPlayer(m);
    REQUIRE(p.Ok());

    SECTION("the eye's offset turns with the actor; up stays up") {
        EyeRequest eye;
        eye.on = true;
        Mailbox mb = Fill(p, eye, ShotRequest{}, 5);
        CHECK(mb.serial == 5);
        CHECK(mb.player == kActor);
        CHECK(mb.eye_node == kNode);
        CHECK(mb.eye_on);
        CHECK(mb.eye_offset.x == Approx(0.0f).margin(1e-5));
        CHECK(mb.eye_offset.y == Approx(12.3f));
        CHECK(mb.eye_offset.z == Approx(0.0f).margin(1e-5));
        eye.offset = {0.0f, 0.0f, 2.0f}; // two units ahead of the actor
        mb = Fill(p, eye, ShotRequest{}, 6);
        const Vec3 ahead = Rotate(Mul(parent, local), {0.0f, 0.0f, 2.0f});
        CHECK(mb.eye_offset.x == Approx(ahead.x).margin(1e-5));
        CHECK(mb.eye_offset.z == Approx(ahead.z).margin(1e-5));
        eye.on = false;
        CHECK_FALSE(Fill(p, eye, ShotRequest{}, 7).eye_on);
    }

    SECTION("the shot's frame is the parent's rotation, whatever is asked") {
        const Mailbox mb = Fill(p, EyeRequest{}, ShotRequest{}, 1);
        CHECK(mb.shot_frame.x == Approx(parent.x).margin(1e-5));
        CHECK(mb.shot_frame.y == Approx(parent.y).margin(1e-5));
        CHECK(mb.shot_frame.z == Approx(parent.z).margin(1e-5));
        CHECK(mb.shot_frame.w == Approx(parent.w).margin(1e-5));
        CHECK_FALSE(mb.shot_rotate);
        CHECK_FALSE(mb.shot_move);
        // On land there is no parent: local = world, the frame is the identity.
        SetQuat(m, kActor + kActorWorldQuat, local);
        const Mailbox land = Fill(FindPlayer(m), EyeRequest{}, ShotRequest{}, 1);
        CHECK(land.shot_frame.w == Approx(1.0f).margin(1e-6));
        CHECK(land.shot_frame.y == Approx(0.0f).margin(1e-6));
    }

    SECTION("the test switch's shot") {
        // Facing +z (no turn at all): yaw 90 takes the aim to +x, pitch 30 lifts it.
        SetQuat(m, kActor + kActorWorldQuat, Quat{});
        SetQuat(m, kActor + kActorLocalQuat, Quat{});
        p = FindPlayer(m);
        ShotRequest r = ShotTest(p, pi / 2, 0.0f, {}, false, false);
        CHECK(r.rotate);
        CHECK_FALSE(r.move);
        Vec3 aim = Rotate(r.rotation, {0.0f, 0.0f, 1.0f});
        CHECK(aim.x == Approx(1.0f).margin(1e-5));
        CHECK(aim.z == Approx(0.0f).margin(1e-5));
        r = ShotTest(p, 0.0f, pi / 6, {}, false, false);
        aim = Rotate(r.rotation, {0.0f, 0.0f, 1.0f});
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        CHECK(aim.z == Approx(std::cos(pi / 6)).margin(1e-5));
        // Both: the lift happens about the player's side axis, then the turn about the world's up.
        r = ShotTest(p, pi / 2, pi / 6, {}, false, false);
        aim = Rotate(r.rotation, {0.0f, 0.0f, 1.0f});
        CHECK(aim.x == Approx(std::cos(pi / 6)).margin(1e-5));
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        // Facing -z, the lift still goes up.
        SetQuat(m, kActor + kActorWorldQuat, local);
        SetQuat(m, kActor + kActorLocalQuat, local);
        p = FindPlayer(m);
        r = ShotTest(p, 0.0f, pi / 6, {3.0f, 4.0f, 5.0f}, true, true);
        aim = Rotate(r.rotation, Rotate(local, {0.0f, 0.0f, 1.0f}));
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        CHECK(aim.z == Approx(-std::cos(pi / 6)).margin(1e-5));
        // The offset is in the actor's axes: facing -z, his +z is the world's -z.
        CHECK(r.move);
        CHECK(r.no_target);
        CHECK(r.offset.x == Approx(-3.0f).margin(1e-5));
        CHECK(r.offset.y == Approx(4.0f).margin(1e-5));
        CHECK(r.offset.z == Approx(-5.0f).margin(1e-5));
        const Mailbox mb = Fill(p, EyeRequest{}, r, 9);
        CHECK(mb.shot_rotate);
        CHECK(mb.shot_move);
        CHECK(mb.shot_no_target);
        CHECK(mb.shot_offset.z == Approx(-5.0f).margin(1e-5));
        // No turn asked: nothing to rotate.
        CHECK_FALSE(ShotTest(p, 0.0f, 0.0f, {}, true, false).rotate);
    }

    SECTION("no player: everything off") {
        m.Set32(kActor, 0);
        ShotRequest shot;
        shot.rotate = shot.move = shot.no_target = true;
        const Mailbox mb = Fill(FindPlayer(m), EyeRequest{true, kEyeOffsetDefault}, shot, 3);
        CHECK(mb.player == 0);
        CHECK_FALSE(mb.eye_on);
        CHECK_FALSE(mb.shot_rotate);
        CHECK_FALSE(mb.shot_move);
        CHECK_FALSE(mb.shot_no_target);
    }
}

TEST_CASE("Kid Icarus hooks: hiding the player and the switch values", "[frame_xr][kiuhook]") {
    const uint32_t one = std::bit_cast<uint32_t>(1.0f);
    const uint32_t tiny = std::bit_cast<uint32_t>(0.001f);
    CHECK(HideWrite(one, true) == tiny);
    CHECK_FALSE(HideWrite(tiny, true).has_value());  // already hidden
    CHECK(HideWrite(tiny, false) == one);            // ours: put back
    CHECK_FALSE(HideWrite(one, false).has_value());  // nothing to do
    // A scale the game set itself is never touched, either way.
    const uint32_t games = std::bit_cast<uint32_t>(0.5f);
    CHECK_FALSE(HideWrite(games, true).has_value());
    CHECK_FALSE(HideWrite(games, false).has_value());

    float v[5] = {};
    CHECK(ParseFloats("30,-10", v, 5) == 2);
    CHECK(v[0] == 30.0f);
    CHECK(v[1] == -10.0f);
    CHECK(ParseFloats("30, -10, 20,5 , 0.5", v, 5) == 5);
    CHECK(v[2] == 20.0f);
    CHECK(v[4] == 0.5f);
    CHECK(ParseFloats("0 12.3 0", v, 3) == 3);
    CHECK(v[1] == 12.3f);
    CHECK(ParseFloats("", v, 5) == 0);
    CHECK(ParseFloats(nullptr, v, 5) == 0);
    CHECK(ParseFloats("1,2,x", v, 5) == 0);
    CHECK(ParseFloats("1,2,3,4,5,6", v, 5) == 0); // too many
    CHECK(ParseFloats("1,nan", v, 5) == 0);
}
