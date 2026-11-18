// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <string_view>
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

// Every group that can be in at once (the plain enemy keep shares its site with the timed one).
constexpr uint32_t kAll = Bit(Group::NoCull) | Bit(Group::BulletKeep) | Bit(Group::Eye) |
                          Bit(Group::Shot) | Bit(Group::EnemyKeepTimed) | Bit(Group::GroupKeep) |
                          Bit(Group::NoFollow) | Bit(Group::AimHold) | Bit(Group::AimBox) |
                          Bit(Group::PlayerShadow) | Bit(Group::WideCamera);
constexpr std::size_t kAllWords = kPatches.size() - 1;

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
            // Two patches share an address only as the two enemy keeps do: the same game word,
            // different words of ours, different groups.
            if (kPatches[j].addr == p.addr) {
                CHECK(p.addr == kSitePathEnd);
                CHECK(kPatches[j].original == p.original);
                CHECK(kPatches[j].patched != p.patched);
                CHECK(kPatches[j].group != p.group);
            }
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
        CHECK(r.code_writes == Stubs::kImage.size() + kAllWords);
        for (const Patch& p : kPatches) {
            if (p.group != Group::EnemyKeep) {
                CHECK(m.Get32(p.addr) == p.patched);
            }
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
        CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::In);
        CHECK(r.Of(Group::GroupKeep) == GroupState::In);
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

    // AzaharFrame kiufov: the C-Stick & Gyro mod's hook at one of AimHold's context words.
    SECTION("with the mod the eased moves stay the game's, the whole-screen box goes in") {
        m.Set32(0x6D219C, kInvented);
        SyncResult r = Sync(m, Bit(Group::AimHold) | Bit(Group::AimBox), Mailbox{});
        CHECK(r.Of(Group::AimHold) == GroupState::Different);
        CHECK(r.Of(Group::AimBox) == GroupState::In);
        CHECK(m.Get32(kSiteReticleEase) == 0x0A000002);
        CHECK(m.Get32(kSiteReticleBox) == B(kSiteReticleBox, kReticleBoxWhole));
        r = Sync(m, 0, Mailbox{});
        CHECK(r.Of(Group::AimBox) == GroupState::Out);
        CHECK(m.Get32(kSiteReticleBox) == 0x0A00001F);
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

// AzaharFrame kiushadow: the player's round shadow (one stub group, in only with first person).
TEST_CASE("Kid Icarus hooks: the player's shadow", "[frame_xr][kiuhook]") {
    // The words: the site is the shadow function's first instruction, push {r4, r5, lr}; ours
    // branches into the cave, to the stub.
    const Patch* site = nullptr;
    for (const Patch& p : kPatches) {
        if (p.group == Group::PlayerShadow) {
            CHECK(site == nullptr); // one word only
            site = &p;
        }
    }
    REQUIRE(site != nullptr);
    CHECK(site->addr == 0x57B054);
    CHECK(site->addr == kSiteShadow);
    CHECK(site->original == 0xE92D4030);
    CHECK(site->patched == 0xEA1C6949); // b 0xC95580
    CHECK(site->patched == B(kSiteShadow, kShadowStub));
    CHECK(kShadowStub == 0xC95580);
    const int32_t off = static_cast<int32_t>(site->patched << 8) >> 6;
    CHECK(kSiteShadow + 8u + static_cast<uint32_t>(off) == kShadowStub);
    CHECK(UsesStubs(Group::PlayerShadow));
    CHECK(std::string_view(GroupName(Group::PlayerShadow)) == "player shadow");
    // The stub: inside the image, after the others; it reads the mailbox's player and the
    // state's actor (+0x30), returns (bx lr) when they match, else runs the site's instruction
    // and loads pc with the function's second instruction.
    CHECK(kShadowStub > kKeepStub);
    CHECK(kShadowStub < kImageBase + kImageBytes);
    const uint32_t at = Stubs::kShadowStub / 4;
    CHECK(Stubs::kImage[at] == 0xE59FC000u + (Stubs::kLitMailbox - Stubs::kShadowStub - 8)); // ldr r12, lit_mailbox
    CHECK(Stubs::kImage[at + 1] == (0xE59CC000u | Stubs::kMbPlayer)); // ldr r12, [r12, #player]
    CHECK(Stubs::kImage[at + 4] == 0xE5902030u);                     // ldr r2, [r0, #0x30]
    CHECK(Stubs::kImage[at + 6] == 0x012FFF1Eu);                     // bxeq lr
    CHECK(Stubs::kImage[at + 7] == site->original);                  // push {r4, r5, lr}
    CHECK((Stubs::kImage[at + 8] & 0xFFFFF000u) == 0xE59FF000u);     // ldr pc, [pc, #n]
    CHECK(Stubs::kImage[Stubs::kLitShadowContinue / 4] == 0x57B058);
    // Its context: the function after the push as the stub assumes it, and the two ways into
    // the shadow list.
    std::map<uint32_t, uint32_t> ctx;
    for (const Context& c : kContext) {
        if (c.group == Group::PlayerShadow) {
            ctx[c.addr] = c.word;
        }
    }
    CHECK(ctx.size() == 9);
    CHECK(ctx[0x57B058] == 0xE1A04000); // mov r4, r0
    CHECK(ctx[0x57B064] == 0xE59020AC); // ldr r2, [r0, #0xAC]: r2 written before it is read
    CHECK(ctx[0x57B08C] == 0xE5945030); // ldr r5, [r4, #0x30]: the actor
    CHECK(ctx[0x57B0D4] == B(0x57B0D4, 0x5B26AC));
    CHECK(ctx[0x57B190] == Bl(0x57B190, 0x50533C));
    CHECK(ctx[0x57B19C] == 0xE8BD8030); // pop {r4, r5, pc}

    FakeMemory m = Game();
    Mailbox mb;
    mb.player = 0x141DB1E0;

    SECTION("in: the image, the mailbox with the player, the site last; out again") {
        SyncResult r = Sync(m, Bit(Group::PlayerShadow), mb);
        CHECK(r.Of(Group::PlayerShadow) == GroupState::In);
        CHECK(r.in_mask == Bit(Group::PlayerShadow));
        CHECK(r.cave == Cave::Ours);
        CHECK(r.code_writes == Stubs::kImage.size() + 1);
        CHECK(ImageInPlace(m));
        CHECK(m.Get32(kMailbox + Stubs::kMbPlayer) == 0x141DB1E0);
        CHECK(m.Get32(kSiteShadow) == B(kSiteShadow, kShadowStub));
        REQUIRE_FALSE(m.writes.empty());
        CHECK(m.writes.back().addr == kSiteShadow);
        CHECK(m.writes.back().code);
        // The other groups' sites untouched.
        for (const Patch& p : kPatches) {
            if (p.group != Group::PlayerShadow) {
                CHECK(m.Get32(p.addr) == p.original);
            }
        }
        // Again: the mailbox only.
        m.writes.clear();
        r = Sync(m, Bit(Group::PlayerShadow), mb);
        CHECK(r.code_writes == 0);
        CHECK(m.writes.size() == kMailboxWords);
        // Out (first person off): the game's word is back, the image stays.
        m.writes.clear();
        r = Sync(m, 0, Mailbox{});
        CHECK(r.Of(Group::PlayerShadow) == GroupState::Out);
        CHECK(r.code_writes == 1);
        CHECK(m.Get32(kSiteShadow) == 0xE92D4030);
        CHECK(ImageInPlace(m));
        CHECK_FALSE(AnyOurs(m));
    }

    SECTION("no player: the group is in, the stub passes every shadow through") {
        const SyncResult r = Sync(m, Bit(Group::PlayerShadow), Mailbox{});
        CHECK(r.Of(Group::PlayerShadow) == GroupState::In);
        CHECK(m.Get32(kMailbox + Stubs::kMbPlayer) == 0);
    }

    SECTION("a context word differs: the site stays the game's") {
        m.Set32(0x57B08C, kInvented);
        const SyncResult r = Sync(m, Bit(Group::PlayerShadow) | Bit(Group::NoCull), mb);
        CHECK(r.Of(Group::PlayerShadow) == GroupState::Different);
        CHECK(r.different_addr == 0x57B08C);
        CHECK(m.Get32(kSiteShadow) == 0xE92D4030);
        CHECK(r.Of(Group::NoCull) == GroupState::In);
    }

    SECTION("the site holds something else: never written") {
        m.Set32(kSiteShadow, kInvented);
        SyncResult r = Sync(m, Bit(Group::PlayerShadow), mb);
        CHECK(r.Of(Group::PlayerShadow) == GroupState::Different);
        CHECK(m.Get32(kSiteShadow) == kInvented);
        r = Sync(m, 0, Mailbox{});
        CHECK(m.Get32(kSiteShadow) == kInvented);
    }

    SECTION("the cave is someone else's: out") {
        m.Set32(kCaveEnd - 0x40, kInvented);
        const SyncResult r = Sync(m, Bit(Group::PlayerShadow), mb);
        CHECK(r.Of(Group::PlayerShadow) == GroupState::NoCave);
        CHECK(m.Get32(kSiteShadow) == 0xE92D4030);
    }

    SECTION("a save state with it in, first person off: the game's word back") {
        REQUIRE(Sync(m, Bit(Group::PlayerShadow), mb).Of(Group::PlayerShadow) == GroupState::In);
        FakeMemory loaded = m;
        REQUIRE(AnyOurs(loaded));
        const SyncResult r = Sync(loaded, 0, Mailbox{});
        CHECK(r.code_writes == 1);
        CHECK(loaded.Get32(kSiteShadow) == 0xE92D4030);
    }
}

TEST_CASE("Kid Icarus hooks: the wide camera", "[frame_xr][kiuhook][kiuwide]") {
    // The words: the site is the camera node angle setter's first instruction,
    // vldr s1, [r0, #0x3C]; ours branches into the cave, to the stub.
    const Patch* site = nullptr;
    for (const Patch& p : kPatches) {
        if (p.group == Group::WideCamera) {
            CHECK(site == nullptr); // one word only
            site = &p;
        }
    }
    REQUIRE(site != nullptr);
    CHECK(site->addr == 0x45E04C);
    CHECK(site->addr == kSiteNodeFov);
    CHECK(site->original == 0xEDD00A0F);
    CHECK(site->patched == B(kSiteNodeFov, kFovStub));
    const int32_t off = static_cast<int32_t>(site->patched << 8) >> 6;
    CHECK(kSiteNodeFov + 8u + static_cast<uint32_t>(off) == kFovStub);
    CHECK(UsesStubs(Group::WideCamera));
    CHECK(std::string_view(GroupName(Group::WideCamera)) == "wide camera");
    CHECK(kSceneNode == 0xE14B88);
    CHECK(Stubs::kImageVersion == 5);
    // The stub: inside the image, after the others. Its first words compare the mailbox's node
    // with r0 through r12 only; the pass path runs the site's instruction and loads pc with the
    // setter's second; the node's path writes +0x3C, +0x40 and clears 0x5C in +0x6C.
    CHECK(kFovStub > kShadowStub);
    CHECK(kImageBase + Stubs::kFovPass + 4 < kImageBase + kImageBytes);
    const uint32_t at = Stubs::kFovStub / 4;
    CHECK(Stubs::kImage[at] == 0xE59FC000u + (Stubs::kLitMailbox - Stubs::kFovStub - 8));
    CHECK(Stubs::kImage[at + 1] == (0xE59CC000u | Stubs::kMbWideNode)); // ldr r12, [r12, #node]
    CHECK(Stubs::kImage[at + 2] == 0xE15C0000u);                        // cmp r12, r0
    CHECK(Stubs::kImage[Stubs::kFovPass / 4] == site->original);          // vldr s1, [r0, #0x3C]
    CHECK((Stubs::kImage[Stubs::kFovPass / 4 + 1] & 0xFFFFF000u) == 0xE59FF000u); // ldr pc, [pc, #n]
    CHECK(Stubs::kImage[Stubs::kLitFovContinue / 4] == 0x45E050);
    int stores = 0, bic = 0;
    for (uint32_t i = at; i < Stubs::kFovPass / 4; ++i) {
        const uint32_t w = Stubs::kImage[i];
        stores += (w == 0xE580C03Cu || w == 0xE580C040u) ? 1 : 0; // str r12, [r0, #0x3C / #0x40]
        bic += w == 0xE3C1105Cu ? 1 : 0;                          // bic r1, r1, #0x5C
    }
    CHECK(stores == 2);
    CHECK(bic == 1);
    // Its context: the setter as the stub stands in for it, the shape's place, the builder.
    std::map<uint32_t, uint32_t> ctx;
    for (const Context& c : kContext) {
        if (c.group == Group::WideCamera) {
            ctx[c.addr] = c.word;
        }
    }
    CHECK(ctx.size() == 11);
    CHECK(ctx[0x45E05C] == 0xED800A0F); // vstr s0, [r0, #0x3C]: the angle at +0x3C
    CHECK(ctx[0x45E064] == 0xE3C1105C); // bic r1, r1, #0x5C: the dirty bits
    CHECK(ctx[0x45E06C] == kBxLr);
    CHECK(ctx[0x45DFF4] == 0xED841A10); // vstr s2, [r4, #0x40]: the shape at +0x40

    FakeMemory m = Game();
    const float game_aspect = 400.0f / 240.0f;
    m.SetF(kSceneAspect, game_aspect);
    m.SetF(kSceneFovY, 0.7854f);
    m.Set32(kSceneNode + kNodeFlags, 0xFFu);
    Mailbox mb;
    FillWide(mb, {true, 1.95f, 1.32f}, game_aspect);
    REQUIRE(mb.wide_node == kSceneNode);

    SECTION("the mailbox's fields") {
        const auto w = mb.Words();
        CHECK(w[0x54 / 4] == kSceneNode);
        CHECK(std::bit_cast<float>(w[0x58 / 4]) == 1.95f);
        CHECK(std::bit_cast<float>(w[0x5C / 4]) == 1.32f);
        CHECK(std::bit_cast<float>(w[0x60 / 4]) == game_aspect);
        CHECK(Mailbox{}.Words()[0x54 / 4] == 0); // none: the stub passes every node
        // Values the builder could not take, or no shape of the game's: none.
        Mailbox bad;
        FillWide(bad, {true, 3.1f, 1.32f}, game_aspect);
        CHECK(bad.wide_node == 0);
        FillWide(bad, {true, 1.95f, 0.0f}, game_aspect);
        CHECK(bad.wide_node == 0);
        FillWide(bad, {true, std::nanf(""), 1.32f}, game_aspect);
        CHECK(bad.wide_node == 0);
        FillWide(bad, {true, 1.95f, 1.32f}, 0.0f);
        CHECK(bad.wide_node == 0);
        FillWide(bad, {false, 1.95f, 1.32f}, game_aspect);
        CHECK(bad.wide_node == 0);
    }

    SECTION("in: the image, the mailbox, the site last; out again") {
        SyncResult r = Sync(m, Bit(Group::WideCamera), mb);
        CHECK(r.Of(Group::WideCamera) == GroupState::In);
        CHECK(r.cave == Cave::Ours);
        CHECK(ImageInPlace(m));
        CHECK(m.Get32(kMailbox + Stubs::kMbWideNode) == kSceneNode);
        CHECK(m.Get32(kSiteNodeFov) == B(kSiteNodeFov, kFovStub));
        REQUIRE_FALSE(m.writes.empty());
        CHECK(m.writes.back().addr == kSiteNodeFov);
        // The app never writes the node itself while it is in (the stub does, in the game's
        // own frame).
        for (const auto& w : m.writes) {
            CHECK((w.addr < kSceneNode || w.addr >= kSceneNode + 0x80));
        }
        r = Sync(m, 0, Mailbox{});
        CHECK(r.Of(Group::WideCamera) == GroupState::Out);
        CHECK(m.Get32(kSiteNodeFov) == 0xEDD00A0F);
        CHECK_FALSE(AnyOurs(m));
    }

    SECTION("a context word differs (another build): the site stays the game's") {
        m.Set32(0x45E064, kInvented);
        const SyncResult r = Sync(m, Bit(Group::WideCamera), mb);
        CHECK(r.Of(Group::WideCamera) == GroupState::Different);
        CHECK(r.different_addr == 0x45E064);
        CHECK(m.Get32(kSiteNodeFov) == 0xEDD00A0F);
    }

    SECTION("the game's own shape and putting it back") {
        // Before anything: the record is empty, the node's shape is the game's.
        WideRecord rec = ReadWideRecord(m);
        CHECK(rec.node == 0);
        const uint32_t game_bits = std::bit_cast<uint32_t>(game_aspect);
        CHECK(WideGameAspect(game_bits, rec) == game_aspect);
        CHECK_FALSE(WideRestore(game_bits, rec));
        // Widened (the stub wrote the node in the game's frame): the record says so.
        REQUIRE(Sync(m, Bit(Group::WideCamera), mb).Of(Group::WideCamera) == GroupState::In);
        m.SetF(kSceneAspect, 1.32f);
        rec = ReadWideRecord(m);
        CHECK(rec.node == kSceneNode);
        const uint32_t ours = std::bit_cast<uint32_t>(1.32f);
        CHECK(WideAspectIsOurs(ours, rec));
        CHECK(WideGameAspect(ours, rec) == game_aspect); // not our own shape taken for the game's
        CHECK(WideRestore(ours, rec) == game_bits);
        // The shape asked for last, before the stub has run with it: ours too.
        const uint32_t newer = std::bit_cast<uint32_t>(1.30f);
        CHECK(WideRestore(newer, rec, newer) == game_bits);
        CHECK_FALSE(WideRestore(newer, rec));
        // Anything else is the game's doing: never touched.
        CHECK_FALSE(WideRestore(std::bit_cast<uint32_t>(1.5f), rec, newer));
        CHECK(WideGameAspect(std::bit_cast<uint32_t>(1.5f), rec) == 1.5f);
        // Already the game's: nothing to do.
        CHECK_FALSE(WideRestore(game_bits, rec));
        // An old mailbox (no tag): no record.
        m.Set32(kMailbox, 0);
        CHECK(ReadWideRecord(m).node == 0);
    }

    SECTION("a save state made widened, loaded with first person off") {
        REQUIRE(Sync(m, Bit(Group::WideCamera), mb).Of(Group::WideCamera) == GroupState::In);
        m.SetF(kSceneAspect, 1.32f);
        FakeMemory loaded = m;
        REQUIRE(AnyOurs(loaded));
        // The record travels with the state (the cave is in it): the shape goes back to the
        // game's even though this run never asked for anything.
        const WideRecord rec = ReadWideRecord(loaded);
        const auto back = WideRestore(loaded.Get32(kSceneAspect), rec);
        REQUIRE(back);
        CHECK(std::bit_cast<float>(*back) == game_aspect);
        const SyncResult r = Sync(loaded, 0, Mailbox{});
        CHECK(r.code_writes == 1);
        CHECK(loaded.Get32(kSiteNodeFov) == 0xEDD00A0F);
    }
}

TEST_CASE("Kid Icarus hooks: the enemy keeps", "[frame_xr][kiuhook]") {
    FakeMemory m = Game();
    const uint32_t plain = Beq(kSitePathEnd, Stubs::kPathRestart);
    const uint32_t timed = Beq(kSitePathEnd, kKeepStub);
    CHECK(plain == 0x0A00000E);
    CHECK((timed >> 24) == 0x0A);
    CHECK(timed != plain);

    // The plain keep: one word, no cave.
    SyncResult r = Sync(m, Bit(Group::EnemyKeep), Mailbox{});
    CHECK(r.Of(Group::EnemyKeep) == GroupState::In);
    CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::Out);
    CHECK(r.code_writes == 1);
    CHECK(r.cave == Cave::NotLooked);
    CHECK(m.Get32(kSitePathEnd) == plain);

    // Over to the timed keep: the image, the mailbox with its limit, then the shared word; no
    // "the game's code differs" for the sibling's word.
    Mailbox mb;
    mb.keep_seconds = 20.0f;
    r = Sync(m, Bit(Group::EnemyKeepTimed), mb);
    CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::In);
    CHECK(r.Of(Group::EnemyKeep) == GroupState::Out);
    CHECK(r.different_addr == 0);
    CHECK(m.Get32(kSitePathEnd) == timed);
    CHECK(ImageInPlace(m));
    CHECK(std::bit_cast<float>(m.Get32(kMailbox + Stubs::kMbKeepSeconds)) == 20.0f);
    // Stays so, and nothing but the mailbox is written again.
    m.writes.clear();
    r = Sync(m, Bit(Group::EnemyKeepTimed), mb);
    CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::In);
    CHECK(r.code_writes == 0);

    // And back to the plain keep.
    r = Sync(m, Bit(Group::EnemyKeep), Mailbox{});
    CHECK(r.Of(Group::EnemyKeep) == GroupState::In);
    CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::Out);
    CHECK(r.different_addr == 0);
    CHECK(m.Get32(kSitePathEnd) == plain);

    // Both off: the game's word.
    r = Sync(m, 0, Mailbox{});
    CHECK(m.Get32(kSitePathEnd) == 0x0A000017);
    CHECK_FALSE(AnyOurs(m));

    // Something else at the site keeps both out.
    m.Set32(kSitePathEnd, kInvented);
    r = Sync(m, Bit(Group::EnemyKeep), Mailbox{});
    CHECK(r.Of(Group::EnemyKeep) == GroupState::Different);
    r = Sync(m, Bit(Group::EnemyKeepTimed), mb);
    CHECK(r.Of(Group::EnemyKeepTimed) == GroupState::Different);
    CHECK(m.Get32(kSitePathEnd) == kInvented);
    m.Set32(kSitePathEnd, 0x0A000017);

    // The land groups' despawn: its first word becomes bx lr.
    r = Sync(m, Bit(Group::GroupKeep), Mailbox{});
    CHECK(r.Of(Group::GroupKeep) == GroupState::In);
    CHECK(m.Get32(kSiteGroupDespawn) == 0xE12FFF1E);
    r = Sync(m, 0, Mailbox{});
    CHECK(m.Get32(kSiteGroupDespawn) == 0xE92D4FF8);

    // The camera's follow input: one call becomes a nop.
    r = Sync(m, Bit(Group::NoFollow), Mailbox{});
    CHECK(r.Of(Group::NoFollow) == GroupState::In);
    CHECK(r.code_writes == 1);
    CHECK(m.Get32(kSiteFollowInput) == 0xE1A00000);
    r = Sync(m, 0, Mailbox{});
    CHECK(m.Get32(kSiteFollowInput) == 0xEB031897);

    // The limit: only a finite, positive number of seconds; it needs no player.
    FakeMemory none;
    const Player nobody = FindPlayer(none);
    CHECK(Fill(nobody, EyeRequest{}, ShotRequest{}, 1, 25.0f).keep_seconds == 25.0f);
    CHECK(Fill(nobody, EyeRequest{}, ShotRequest{}, 1, -3.0f).keep_seconds == 0.0f);
    CHECK(Fill(nobody, EyeRequest{}, ShotRequest{}, 1, std::nanf("")).keep_seconds == 0.0f);
    CHECK(Fill(nobody, EyeRequest{}, ShotRequest{}, 1).keep_seconds == 0.0f);
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
        CHECK(r.code_writes == Stubs::kImage.size() + kAllWords);
    }

    SECTION("a state with the patches in, every switch off: they come out") {
        m = patched;
        m.writes.clear();
        REQUIRE(AnyOurs(m));
        const SyncResult r = Sync(m, 0, Mailbox{});
        CHECK(r.code_writes == kAllWords);
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
    CHECK(Stubs::kMbKeepSeconds == 0x50);
    CHECK(Stubs::kMbWideNode == 0x54); // AzaharFrame kiuwide
    CHECK(Stubs::kMbWideFov == 0x58);
    CHECK(Stubs::kMbWideAspect == 0x5C);
    CHECK(Stubs::kMbWideGameAspect == 0x60);
    CHECK(Stubs::kMbSize == 0x64);
    // The keep stub reads its limit off r12 too: vldr s1, [r12, #0x50] = 0xEDDC0A14.
    CHECK(Stubs::kImage[Stubs::kKeepStub / 4 + 2] == (0xEDDC0A00u | (Stubs::kMbKeepSeconds / 4)));
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
    CHECK(loads(Stubs::kMbPlayer) == 3); // the eye, shot and shadow stubs
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
    mb.keep_seconds = 25.0f;
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
    CHECK(f(0x50) == 25.0f);
    CHECK(std::bit_cast<float>(Mailbox{}.Words()[0x50 / 4]) == 0.0f); // nothing kept
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

    // AzaharFrame kiufov: Behind Pit's eye, laid out along the camera's level view.
    SECTION("an eye in the camera's axes stays behind the view whatever the body faces") {
        EyeRequest eye;
        eye.on = true;
        eye.offset = {0.0f, 13.0f, -3.85f};
        eye.own_axes = true;
        eye.axes = LevelYawAxes({1.0f, -0.3f, 0.0f}); // the camera looks along +x, a bit down
        const Mailbox mb = Fill(p, eye, ShotRequest{}, 8);
        CHECK(mb.eye_offset.x == Approx(-3.85f).margin(1e-5));
        CHECK(mb.eye_offset.y == Approx(13.0f).margin(1e-5));
        CHECK(mb.eye_offset.z == Approx(0.0f).margin(1e-5));
        const Quat straight_down = LevelYawAxes({0.0f, -1.0f, 0.0f});
        CHECK(straight_down.w == 1.0f);
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
        // The view along +z: yaw 90 takes the aim to +x, pitch 30 lifts it.
        const Vec3 z{0.0f, 0.0f, 1.0f};
        ShotRequest r = ShotTest(z, pi / 2, 0.0f, {}, false, false);
        CHECK(r.rotate);
        CHECK_FALSE(r.move);
        Vec3 aim = Rotate(r.rotation, z);
        CHECK(aim.x == Approx(1.0f).margin(1e-5));
        CHECK(aim.z == Approx(0.0f).margin(1e-5));
        r = ShotTest(z, 0.0f, pi / 6, {}, false, false);
        aim = Rotate(r.rotation, z);
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        CHECK(aim.z == Approx(std::cos(pi / 6)).margin(1e-5));
        // Both: the lift happens about the view's side axis, then the turn about the world's up.
        r = ShotTest(z, pi / 2, pi / 6, {}, false, false);
        aim = Rotate(r.rotation, z);
        CHECK(aim.x == Approx(std::cos(pi / 6)).margin(1e-5));
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        // The view along -z and tilted down (only its level part counts): the lift still goes up,
        // and the offset is (side, up, ahead) of that view: ahead is -z, side is -z x up = +x.
        const Vec3 back{0.0f, -0.4f, -2.0f};
        r = ShotTest(back, 0.0f, pi / 6, {3.0f, 4.0f, 5.0f}, true, true);
        aim = Rotate(r.rotation, {0.0f, 0.0f, -1.0f});
        CHECK(aim.y == Approx(0.5f).margin(1e-5));
        CHECK(aim.z == Approx(-std::cos(pi / 6)).margin(1e-5));
        CHECK(r.move);
        CHECK(r.no_target);
        CHECK(r.offset.x == Approx(3.0f).margin(1e-5));
        CHECK(r.offset.y == Approx(4.0f).margin(1e-5));
        CHECK(r.offset.z == Approx(-5.0f).margin(1e-5));
        // A view along +x: ahead is +x, side is +x x up = +z.
        r = ShotTest({7.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {3.0f, 4.0f, 5.0f}, true, false);
        CHECK(r.offset.x == Approx(5.0f).margin(1e-5));
        CHECK(r.offset.z == Approx(3.0f).margin(1e-5));
        // Looking straight down: still a finite answer.
        r = ShotTest({0.0f, -1.0f, 0.0f}, 0.3f, 0.2f, {1.0f, 2.0f, 3.0f}, true, false);
        CHECK(Finite(r.rotation));
        CHECK(Finite(r.offset));
        const Mailbox mb = Fill(p, EyeRequest{}, ShotTest(back, 0.0f, pi / 6, {3.0f, 4.0f, 5.0f}, true, true), 9);
        CHECK(mb.shot_rotate);
        CHECK(mb.shot_move);
        CHECK(mb.shot_no_target);
        CHECK(mb.shot_offset.z == Approx(-5.0f).margin(1e-5));
        // No turn asked: nothing to rotate.
        CHECK_FALSE(ShotTest(z, 0.0f, 0.0f, {}, true, false).rotate);
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
