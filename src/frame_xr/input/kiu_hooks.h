// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhook: the app's own small patches to Kid Icarus: Uprising's ORIGINAL code, in
// the game's memory only (nothing is shipped as game files), the pure part: the patch table, the
// code cave's stubs and their mailbox, and the rules for putting them in and taking them out.
// No emulator or OpenXR types here, so tests/frame_xr/kiu_hooks.cpp checks it over a fake memory.
// frame_xr/kiu_hooks.h runs it on the emulation thread at each VBlank; KIU_HOOKS.md beside this
// file has every address and why. Test switches only (no setting, no row, no text in the app).
//
// Everything is for the European game (title 0004000000030200, code.bin md5 b107c9b1), checked
// word by word before anything is written:
//   - a patch is one instruction word: {address, the game's word, ours}. A group of patches goes
//     in or out as a whole, live with its switch. The game's memory is the truth, not a flag
//     kept here: at each VBlank every site is read again and is the game's word, ours, or
//     something else (another build, a mod, a cheat). Ours while unwanted is put back;
//     something else leaves the whole group out (said once in the log) and is never written.
//     So a save state loaded with or without patches ends up right one VBlank later.
//   - some groups reach a stub in the code cave (the text segment's unused tail,
//     0xC95370-0xC96000, all zero in the game): the image of kiu_hooks_stubs.h, written once
//     before the first site that jumps to it and never changed or cleared after (a guest thread
//     may be stopped inside it). The stubs read the mailbox, the cave's last bytes, which the
//     app writes at each VBlank; the cave is read-only and executable for the game, so nothing
//     of the game can spoil it, and the stubs never write there.
//   - every code word written is followed by a code cache drop for it (the emulator part).

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include "frame_xr/input/kiu_hooks_stubs.h"
#include "frame_xr/input/kiu_turn.h"

namespace FrameXr::Input::KiuHooks {

using KiuTurn::Reader;

// ---- ARM words ----

/// `b to` placed at `from` (both 4-byte aligned, within +-32 MB).
constexpr uint32_t B(uint32_t from, uint32_t to) {
    return 0xEA000000u | (((to - from - 8u) >> 2) & 0x00FFFFFFu);
}
/// `bl to` placed at `from`.
constexpr uint32_t Bl(uint32_t from, uint32_t to) {
    return 0xEB000000u | (((to - from - 8u) >> 2) & 0x00FFFFFFu);
}
/// `beq to` placed at `from`.
constexpr uint32_t Beq(uint32_t from, uint32_t to) {
    return 0x0A000000u | (((to - from - 8u) >> 2) & 0x00FFFFFFu);
}
inline constexpr uint32_t kMovR0One = 0xE3A00001; ///< mov r0, #1
inline constexpr uint32_t kBxLr = 0xE12FFF1E;     ///< bx lr
inline constexpr uint32_t kNop = 0xE1A00000;      ///< mov r0, r0

// ---- The game (EUR original code) ----

inline constexpr uint32_t kCaveStart = 0xC95370; ///< the text segment's unused tail
inline constexpr uint32_t kCaveEnd = 0xC96000;
inline constexpr uint32_t kImageBase = 0xC95380; ///< where the stubs' image goes
inline constexpr uint32_t kImageBytes = static_cast<uint32_t>(Stubs::kImage.size()) * 4u;
inline constexpr uint32_t kMailbox = Stubs::kMailbox;
inline constexpr uint32_t kMailboxWords = Stubs::kMbSize / 4u;
inline constexpr uint32_t kMailboxMagic = 0x4D55494B; ///< "KIUM"

inline constexpr uint32_t kSiteSceneCull = 0x77DB44;  ///< the scene model's cull loop
inline constexpr uint32_t kSceneCullInside = 0x77DC4C; ///< its "wholly in view" exit
inline constexpr uint32_t kSiteObjectCull = 0x773294; ///< the object cull's test
inline constexpr uint32_t kObjectCullVisible = 0x773280; ///< its "visible" exit
inline constexpr uint32_t kSiteBulletView = 0x41FF1C; ///< a flying bullet's in-view test
inline constexpr uint32_t kSitePooledBulletView = 0x40BD6C; ///< the pooled rapid bullet's
inline constexpr uint32_t kSiteCameraSink = 0x63409C; ///< the camera hand-over's entry
inline constexpr uint32_t kSiteShotSpawn = 0x5043E4;  ///< the player's shot: bl 0x485638
inline constexpr uint32_t kSitePathEnd = 0x3E8C9C;     ///< a flier's path end: beq "vanish"
inline constexpr uint32_t kSiteGroupDespawn = 0xA430FC; ///< a land spawner group's despawn
inline constexpr uint32_t kSiteFollowInput = 0x70401C;  ///< the land camera's follow: its input
inline constexpr uint32_t kSiteReticleEase = 0x6D215C;  ///< the land reticle's eased moves: their gate
inline constexpr uint32_t kReticleEaseNone = 0x6D216C;  ///< its own "none" exit
inline constexpr uint32_t kSiteReticleBox = 0x6D1680;   ///< the aim box: ground or not
inline constexpr uint32_t kReticleBoxWhole = 0x6D1704;  ///< its own whole-screen box (the air's)
inline constexpr uint32_t kSiteShadow = 0x57B054;       ///< an actor's round shadow: its entry
inline constexpr uint32_t kEyeStub = kImageBase + Stubs::kEyeStub;
inline constexpr uint32_t kKeepStub = kImageBase + Stubs::kKeepStub;
inline constexpr uint32_t kShotStub = kImageBase + Stubs::kShotStub;
inline constexpr uint32_t kShadowStub = kImageBase + Stubs::kShadowStub;

inline constexpr uint32_t kSceneEye = 0xE14B90;    ///< the scene camera's eye, 3 floats
inline constexpr uint32_t kSceneLookAt = 0xE14B9C; ///< and its look-at point
inline constexpr uint32_t kSceneUp = 0xE14BA8;     ///< and its up vector
inline constexpr uint32_t kSceneFovY = 0xE14BC4;   ///< the scene projection's angle, radians
inline constexpr uint32_t kSceneAspect = 0xE14BC8; ///< and its width over its height

// The player (KIU_HOOKS.md, "The player"): aim = [[0xE14504] + 8] - 0x10 as kiu_turn.h.
inline constexpr uint32_t kOwnerVtable = 0xCEAC40;
inline constexpr uint32_t kOwnerActor = 0x1C;   ///< the player's actor
inline constexpr uint32_t kOwnerEyeNode = 0x30; ///< the camera node handed to the sink
inline constexpr uint32_t kActorVtable = Stubs::kActorVtable;
inline constexpr uint32_t kActorWorldQuat = 0x44;  ///< x y z w
inline constexpr uint32_t kActorWorldPos = 0x54;
inline constexpr uint32_t kActorLocalScale = 0x60; ///< one float
inline constexpr uint32_t kActorLocalQuat = 0x64;
inline constexpr uint32_t kActorLocalPos = 0x74;
inline constexpr uint32_t kActorName = 0x1BC; ///< -> "CHID_PIT"

// The stubs and this file agree on where things are.
static_assert(kImageBase >= kCaveStart && (kImageBase & 3u) == 0);
static_assert(kImageBase + kImageBytes <= kMailbox, "the image runs into the mailbox");
static_assert(kMailbox + Stubs::kMbSize <= kCaveEnd && (kMailbox & 3u) == 0);
static_assert(Stubs::kImage[0] == Stubs::kImageTag && Stubs::kImage[1] == Stubs::kImageVersion);
static_assert(Stubs::kImage[Stubs::kLitMailbox / 4] == kMailbox);
static_assert(Stubs::kImage[Stubs::kLitActorVtable / 4] == kActorVtable);
static_assert(Stubs::kImage[Stubs::kLitSinkContinue / 4] == kSiteCameraSink + 4);
static_assert(Stubs::kImage[Stubs::kLitSpawn / 4] == 0x485638);
static_assert(Stubs::kImage[Stubs::kEyeStub / 4] == 0xE92D4070,
              "the eye stub starts with the instruction it replaces");
static_assert(Stubs::kImage[Stubs::kLitShadowContinue / 4] == kSiteShadow + 4);
static_assert(Stubs::kShadowContinue == kSiteShadow + 4);
// The encoders give the game's own words back.
static_assert(Bl(kSiteShotSpawn, 0x485638) == 0xEBFE0493);
static_assert(Bl(kSiteBulletView, 0x4EC640) == 0xEB0331C7);
static_assert(Bl(kSitePooledBulletView, 0x4EC640) == 0xEB038233);
static_assert(B(kSiteSceneCull, kSceneCullInside) == 0xEA000040);
static_assert(B(kSiteObjectCull, kObjectCullVisible) == 0xEAFFFFF9);
static_assert(Bl(kSiteFollowInput, 0x7CA280) == 0xEB031897);
static_assert(Beq(kSiteReticleEase, kReticleEaseNone) == 0x0A000002);
static_assert(Beq(kSiteReticleBox, kReticleBoxWhole) == 0x0A00001F);
static_assert(Beq(kSitePathEnd, Stubs::kPathVanish) == 0x0A000017);
static_assert(Beq(kSitePathEnd, Stubs::kPathRestart) == 0x0A00000E);
static_assert(B(0x57B0D4, 0x5B26AC) == 0xEA00DD74);  // AzaharFrame kiushadow: its context words
static_assert(Bl(0x57B190, 0x50533C) == 0xEBFE2869);
static_assert(Stubs::kImage[Stubs::kLitPathRestart / 4] == Stubs::kPathRestart);
static_assert(Stubs::kImage[Stubs::kLitPathVanish / 4] == Stubs::kPathVanish);

// ---- The patch table ----

enum class Group : uint8_t {
    NoCull,     ///< AZAHARFRAME_KIU_NOCULL: the two render culls pass everything
    BulletKeep, ///< AZAHARFRAME_KIU_BULLET_KEEP: bullets out of the game camera's view live on
    Eye,        ///< AZAHARFRAME_KIU_EYE: the camera hand-over goes through the eye stub
    Shot,       ///< AZAHARFRAME_KIU_SHOT_TEST: the player's shot spawn goes through the shot stub
    EnemyKeep,  ///< AZAHARFRAME_KIU_ENEMY_KEEP: passed fliers fly their path again, for good
    EnemyKeepTimed, ///< AZAHARFRAME_KIU_ENEMY_KEEP_S: the same until they are that old (a stub)
    GroupKeep,  ///< AZAHARFRAME_KIU_GROUP_KEEP: land spawner groups are not despawned
    NoFollow,   ///< AZAHARFRAME_KIU_NO_FOLLOW: the land camera takes no turn back while running
    /// AzaharFrame kiuvr: AZAHARFRAME_KIU_AIM_NO_MOD / _AIM_6DOF: on land the reticle stays
    /// where it is put (no eased moves) and may be anywhere on the screen (never the original
    /// game's aim box, so it never turns the camera). Only on the game without the C-Stick &
    /// Gyro mod: its context holds three of the mod's hook sites.
    AimHold,
    /// AzaharFrame kiufov: on land the aim box is the whole screen (the air's), so the game's
    /// clamp never turns the camera for the reticle: with Right Stick turning (and first
    /// person) and with AimHold. Its words are the same in the C-Stick & Gyro mod's v172 EUR
    /// builds, so it goes in with the mod too.
    AimBox,
    /// AzaharFrame kiushadow: the player's own round shadow is not drawn (first person: it lies
    /// right under the eye); everyone else's is, as in the game (a stub).
    PlayerShadow,
};
inline constexpr std::size_t kGroupCount = 11;

constexpr uint32_t Bit(Group g) {
    return 1u << static_cast<uint32_t>(g);
}
constexpr bool UsesStubs(Group g) {
    return g == Group::Eye || g == Group::Shot || g == Group::EnemyKeepTimed ||
           g == Group::PlayerShadow;
}
inline const char* GroupName(Group g) {
    switch (g) {
    case Group::NoCull:
        return "no cull";
    case Group::BulletKeep:
        return "bullet keep";
    case Group::Eye:
        return "eye";
    case Group::Shot:
        return "shot";
    case Group::EnemyKeep:
        return "enemy keep";
    case Group::EnemyKeepTimed:
        return "enemy keep (timed)";
    case Group::GroupKeep:
        return "group keep";
    case Group::NoFollow:
        return "no follow";
    case Group::AimHold:
        return "aim hold";
    case Group::AimBox:
        return "aim box";
    case Group::PlayerShadow:
        return "player shadow";
    }
    return "?";
}

/// One instruction word the app replaces.
struct Patch {
    Group group;
    uint32_t addr;
    uint32_t original; ///< the game's word: anything else there and the group stays out
    uint32_t patched;
};
inline constexpr std::array<Patch, 13> kPatches{{
    // and r0, r1, #0xFF (the top of the sphere-against-six-planes loop) -> b 0x77DC4C, the
    // function's own "wholly inside" exit (the same stack: sub sp, #0xC before, add after).
    {Group::NoCull, kSiteSceneCull, 0xE20100FF, B(kSiteSceneCull, kSceneCullInside)},
    // mov r1, r4 (the start of the plane test; r0 = the object's flags) -> b 0x773280, the
    // function's own "visible" exit (clears the culled bit, returns 1).
    {Group::NoCull, kSiteObjectCull, 0xE1A01004, B(kSiteObjectCull, kObjectCullVisible)},
    // bl 0x4EC640 (is the bullet's sphere in the camera's view?) -> mov r0, #1: seen, never
    // "seen and then out of view", which ends the bullet in air battles. Only r0 is read after.
    {Group::BulletKeep, kSiteBulletView, 0xEB0331C7, kMovR0One},
    {Group::BulletKeep, kSitePooledBulletView, 0xEB038233, kMovR0One},
    // push {r4, r5, r6, lr} (the function's first instruction) -> b eye_stub, which runs it.
    {Group::Eye, kSiteCameraSink, 0xE92D4070, B(kSiteCameraSink, kEyeStub)},
    // bl 0x485638 -> bl shot_stub, which ends in 0x485638 (lr stays the call's).
    {Group::Shot, kSiteShotSpawn, 0xEBFE0493, Bl(kSiteShotSpawn, kShotStub)},
    // beq 0x3E8D00 (the placement has no "fly it again" flag: vanish) -> beq 0x3E8CDC, the
    // function's own restart code a few words on (the same frame, r4 = the path state).
    {Group::EnemyKeep, kSitePathEnd, 0x0A000017, Beq(kSitePathEnd, Stubs::kPathRestart)},
    // The same word -> beq keep_stub, which picks the restart or the vanish code by the enemy's
    // age. The two groups share the site: only one of them is ever wanted.
    {Group::EnemyKeepTimed, kSitePathEnd, 0x0A000017, Beq(kSitePathEnd, kKeepStub)},
    // push {r3-r11, lr} (the function's first instruction) -> bx lr: the despawn does nothing.
    {Group::GroupKeep, kSiteGroupDespawn, 0xE92D4FF8, kBxLr},
    // bl 0x7CA280 (in the battle camera's update, with the block cam+0x4AC that the eased follow
    // 0x7CAFC8 is handed later in the same update) -> mov r0, r0. Found by replacing each call of
    // that update in turn: without this one a turn made while the player runs is no longer partly
    // taken back, and the follow still re-levels the pitch. What 0x7CA280 computes was not read
    // (by its effect: the player's movement, for the follow).
    {Group::NoFollow, kSiteFollowInput, 0xEB031897, kNop},
    // AzaharFrame kiuvr. beq 0x6D216C (no battle camera: in the air; the function that eases the
    // land reticle, back to its rest height after a touch and after a flick, does nothing) ->
    // b 0x6D216C: nothing on land either, so a reticle put somewhere stays there.
    {Group::AimHold, kSiteReticleEase, 0x0A000002, B(kSiteReticleEase, kReticleEaseNone)},
    // beq 0x6D1704 (the aim object's "ground" byte is 0: the box is the whole screen) ->
    // b 0x6D1704: the whole screen on the ground too, so the game's clamp never finds the
    // reticle past the box and never turns the camera for it.
    // (AzaharFrame kiufov: its own group, AimBox, so it goes in with the mod too.)
    {Group::AimBox, kSiteReticleBox, 0x0A00001F, B(kSiteReticleBox, kReticleBoxWhole)},
    // AzaharFrame kiushadow. push {r4, r5, lr} (the first instruction of the function that puts
    // an actor's round shadow in the frame's shadow list; r0 = its movement state, the actor at
    // +0x30) -> b shadow_stub, which returns at once for the player's actor and otherwise runs
    // the push and goes on.
    {Group::PlayerShadow, kSiteShadow, 0xE92D4030, B(kSiteShadow, kShadowStub)},
}};

/// Words a group relies on without changing them (where it jumps to, what the stub assumes of the
/// code after its site): they must be the game's too.
struct Context {
    Group group;
    uint32_t addr;
    uint32_t word;
};
inline constexpr std::array<Context, 64> kContext{{
    {Group::NoCull, 0x77DB24, 0xE24DD00C},   // sub sp, sp, #0xC (the frame the exit undoes)
    {Group::NoCull, 0x77DC4C, 0xE1C401D8},   // ldrd r0, r1, [r4, #0x18]
    {Group::NoCull, 0x77DC50, 0xE28DD00C},   // add sp, sp, #0xC
    {Group::NoCull, 0x77DC54, 0xE3A020FF},   // mov r2, #0xFF (every part visible)
    {Group::NoCull, 0x773270, 0xE5940014},   // ldr r0, [r4, #0x14] (the flags in r0)
    {Group::NoCull, 0x773280, 0xE3C00010},   // bic r0, r0, #0x10
    {Group::NoCull, 0x773284, 0xE5840014},   // str r0, [r4, #0x14]
    {Group::NoCull, 0x77328C, 0xE3A00001},   // mov r0, #1
    {Group::BulletKeep, 0x41FF20, 0xE3500000}, // cmp r0, #0
    {Group::BulletKeep, 0x41FF24, 0xE5D60020}, // ldrb r0, [r6, #0x20] (r0 not read further)
    {Group::BulletKeep, 0x40BD70, 0xE3500000}, // cmp r0, #0
    {Group::BulletKeep, 0x40BD74, 0xE5D50000}, // ldrb r0, [r5]
    // The camera hand-over after its push: r1, r4, r5 and s0-s6 are written before they are
    // read (what the eye stub uses), and the transform is read from r2.
    {Group::Eye, 0x6340A0, 0xE2821004},      // add r1, r2, #4
    {Group::Eye, 0x6340A4, 0xE1A04000},      // mov r4, r0
    {Group::Eye, 0x6340A8, 0xEC910A07},      // vldmia r1, {s0-s6}
    {Group::Eye, 0x6340B4, 0xE2821014},      // add r1, r2, #0x14 (the position)
    {Group::Eye, 0x6340BC, 0xE1A0500D},      // mov r5, sp
    // The shot's call: quaternion*, position* and the target go on the stack as the stub reads
    // them, the owner in r2; and the spawn's own first instruction.
    {Group::Shot, 0x5043C8, 0xE8830404},     // stm r3, {r2, sl}: [sp+8] position*, [sp+0xC] target
    {Group::Shot, 0x5043D4, 0xE88D1040},     // stm sp, {r6, ip}: [sp+4] quaternion*
    {Group::Shot, 0x5043DC, 0xE1A02007},     // mov r2, r7 (the owner)
    {Group::Shot, 0x485638, 0xE92D4FFF},     // push {r0-r11, lr}
    {Group::Shot, 0x5043C0, 0xE28D204C},     // add r2, sp, #0x4C (the position local)
    // AzaharFrame kiuvr: the caller's frame as the stub's "absolute" mode reads it: the aim
    // quaternion before scatter at its sp + 0x70, written by the shooter's aim call.
    {Group::Shot, 0x503F98, 0xE24DD08C},     // sub sp, sp, #0x8C
    {Group::Shot, 0x503F9C, 0xE28D3070},     // add r3, sp, #0x70 (the aim call's fourth argument)
    {Group::Shot, 0x504014, 0xE12FFF3C},     // blx ip (the shooter's aim call)
    {Group::Shot, 0x5043CC, 0xE28DC058},     // add ip, sp, #0x58 (the quaternion local)
    // The path end (0x3E8C34): its frame and r4, the flag test before the site, and the two
    // places it may go on to.
    {Group::EnemyKeep, 0x3E8C34, 0xE92D40F8}, // push {r3-r7, lr}
    {Group::EnemyKeep, 0x3E8C38, 0xE1A04000}, // mov r4, r0
    {Group::EnemyKeep, 0x3E8C98, 0xE1B01A21}, // lsrs r1, r1, #0x14 (the flag the beq tests)
    {Group::EnemyKeep, 0x3E8CDC, 0xE3A05000}, // mov r5, #0 (the restart code)
    {Group::EnemyKeep, 0x3E8CFC, 0xE8BD80F8}, // pop {r3-r7, pc} (its end)
    {Group::EnemyKeepTimed, 0x3E8C34, 0xE92D40F8},
    {Group::EnemyKeepTimed, 0x3E8C38, 0xE1A04000},
    {Group::EnemyKeepTimed, 0x3E8C98, 0xE1B01A21},
    {Group::EnemyKeepTimed, 0x3E8CDC, 0xE3A05000},
    {Group::EnemyKeepTimed, 0x3E8D00, 0xE5945030}, // ldr r5, [r4, #0x30] (the vanish code)
    {Group::GroupKeep, 0xA43100, 0xE280106C},      // add r1, r0, #0x6C (the function's next word)
    {Group::NoFollow, 0x704018, 0xE58D0050},       // str r0, [sp, #0x50] (the block, kept for later)
    {Group::NoFollow, 0x7041BC, 0xE59D3050},       // ldr r3, [sp, #0x50] (handed to the follow)
    {Group::NoFollow, 0x7041D0, 0xEB031B7C},       // bl 0x7CAFC8 (the follow's update)
    // AzaharFrame kiuvr: the eased moves' gate and its "none" exit; the box's test, its
    // whole-screen code and the two numbers that code loads (1 and -1: the box's bottom and top
    // as shares of the screen's middle, and the same for its sides).
    {Group::AimHold, 0x6D2158, 0xE24DD0B0},        // sub sp, sp, #0xB0
    {Group::AimHold, 0x6D2160, 0xE5D400D5},        // ldrb r0, [r4, #0xD5]
    {Group::AimHold, 0x6D216C, 0xE5C45208},        // strb r5, [r4, #0x208] (no eased move)
    {Group::AimHold, 0x6D2170, 0xE28DD0B0},        // add sp, sp, #0xB0
    {Group::AimBox, 0x6D167C, 0xE3510000},         // cmp r1, #0 (the "ground" byte)
    {Group::AimBox, 0x6D1704, 0xED9F0A6D},         // vldr s0, [pc, #0x1B4] (1.0)
    {Group::AimBox, 0x6D1708, 0xEDDF0A6D},         // vldr s1, [pc, #0x1B4] (-1.0)
    {Group::AimBox, 0x6D170C, 0xE28400F0},         // add r0, r4, #0xF0
    {Group::AimBox, 0x6D1710, 0xEC800A02},         // vstmia r0, {s0, s1}
    {Group::AimBox, 0x6D1714, 0xEA00001D},         // b 0x6D1790 (the box from them)
    {Group::AimBox, 0x6D18C0, 0x3F800000},         // 1.0
    {Group::AimBox, 0x6D18C4, 0xBF800000},         // -1.0
    // Three places of the reticle's own code where the C-Stick & Gyro mod puts its hooks: with
    // the mod these differ, the group stays out and the mod's own route aims.
    {Group::AimHold, 0x6D219C, 0xEB05512B},        // bl 0x826650
    {Group::AimHold, 0x6D280C, 0xEB10FF21},        // bl 0xB12498
    {Group::AimHold, 0x6D2F7C, 0xE594008C},        // ldr r0, [r4, #0x8C]
    // AzaharFrame kiushadow: the round shadow after its push, as the stub assumes it: r2 is
    // written before it is read, the actor is [r0 + 0x30]; its two ways into the shadow list
    // (0x5B26AC and 0x50533C, which append to the list at [0xD49490]) and its own exit.
    {Group::PlayerShadow, 0x57B058, 0xE1A04000},   // mov r4, r0
    {Group::PlayerShadow, 0x57B05C, 0xED2D8B02},   // vpush {d8}
    {Group::PlayerShadow, 0x57B060, 0xE24DD02C},   // sub sp, sp, #0x2C
    {Group::PlayerShadow, 0x57B064, 0xE59020AC},   // ldr r2, [r0, #0xAC]
    {Group::PlayerShadow, 0x57B08C, 0xE5945030},   // ldr r5, [r4, #0x30] (the actor)
    {Group::PlayerShadow, 0x57B0D4, 0xEA00DD74},   // b 0x5B26AC (the shadow under the actor)
    {Group::PlayerShadow, 0x57B190, 0xEBFE2869},   // bl 0x50533C (the shadow, the other way)
    {Group::PlayerShadow, 0x57B198, 0xECBD8B02},   // vpop {d8}
    {Group::PlayerShadow, 0x57B19C, 0xE8BD8030},   // pop {r4, r5, pc}
}};

// ---- Small maths (float, as the game's) ----

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
/// (AzaharFrame kiuvr: found with the type, so input/kiu_room.h and its users share them.)
constexpr Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr Vec3 operator*(const Vec3& a, float k) {
    return {a.x * k, a.y * k, a.z * k};
}

/// The quaternion product, as the game composes them: (a * b) applied to v is a applied to
/// (b applied to v).
constexpr Quat Mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
constexpr Quat Conj(const Quat& q) {
    return {-q.x, -q.y, -q.z, q.w};
}
/// q applied to v (q a unit quaternion), the way the shot stub does it.
constexpr Vec3 Rotate(const Quat& q, const Vec3& v) {
    const Vec3 t{2.0f * (q.y * v.z - q.z * v.y), 2.0f * (q.z * v.x - q.x * v.z),
                 2.0f * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
            v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
}
inline Quat Normalized(const Quat& q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        return {};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
/// A turn of `radians` about `axis` (need not be unit; the zero axis gives no turn), by the
/// right-hand rule.
inline Quat AxisAngle(const Vec3& axis, float radians) {
    const float n = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (!(n > 1e-6f) || !std::isfinite(n) || !std::isfinite(radians)) {
        return {};
    }
    const float s = std::sin(radians * 0.5f) / n;
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}
inline bool Finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool Finite(const Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

// ---- The mailbox ----

/// What the stubs read (tools/kiu_hooks/stubs.s). All zero: every stub passes the game through.
struct Mailbox {
    uint32_t serial = 0;
    uint32_t player = 0;   ///< the local player's actor; 0: none, both stubs pass through
    uint32_t eye_node = 0; ///< the camera node of the player's view
    bool eye_on = false;
    Vec3 eye_offset;       ///< world axes, from the player's world position
    bool shot_rotate = false;
    bool shot_move = false;
    bool shot_no_target = false;
    /// AzaharFrame kiuvr: the rotation is the aim itself: q' = rotation * conj(the game's aim
    /// before scatter) * q (needs shot_rotate).
    bool shot_absolute = false;
    Quat shot_rotation; ///< q' = rotation * q, in the spawn's space (world axes on land)
    Quat shot_frame;    ///< the player's parent's rotation: spawn space from his local position
    Vec3 shot_offset;   ///< from the player's position, in the spawn's space
    float keep_seconds = 0.0f; ///< a passed flier younger than this flies its path again

    /// The words as they lie in the game's memory from kMailbox.
    std::array<uint32_t, kMailboxWords> Words() const {
        std::array<uint32_t, kMailboxWords> w{};
        const auto f = [&w](uint32_t offset, float v) {
            w[offset / 4] = std::bit_cast<uint32_t>(v);
        };
        w[Stubs::kMbMagic / 4] = kMailboxMagic;
        w[Stubs::kMbSerial / 4] = serial;
        w[Stubs::kMbPlayer / 4] = player;
        w[Stubs::kMbEyeNode / 4] = eye_node;
        w[Stubs::kMbEyeFlags / 4] = eye_on ? Stubs::kEyeOn : 0u;
        f(Stubs::kMbEyeOffset, eye_offset.x);
        f(Stubs::kMbEyeOffset + 4, eye_offset.y);
        f(Stubs::kMbEyeOffset + 8, eye_offset.z);
        w[Stubs::kMbShotFlags / 4] = (shot_rotate ? Stubs::kShotRotate : 0u) |
                                     (shot_move ? Stubs::kShotMove : 0u) |
                                     (shot_no_target ? Stubs::kShotNoTarget : 0u) |
                                     (shot_absolute && shot_rotate ? Stubs::kShotAbsolute : 0u);
        f(Stubs::kMbShotRotation, shot_rotation.x);
        f(Stubs::kMbShotRotation + 4, shot_rotation.y);
        f(Stubs::kMbShotRotation + 8, shot_rotation.z);
        f(Stubs::kMbShotRotation + 12, shot_rotation.w);
        f(Stubs::kMbShotFrame, shot_frame.x);
        f(Stubs::kMbShotFrame + 4, shot_frame.y);
        f(Stubs::kMbShotFrame + 8, shot_frame.z);
        f(Stubs::kMbShotFrame + 12, shot_frame.w);
        f(Stubs::kMbShotOffset, shot_offset.x);
        f(Stubs::kMbShotOffset + 4, shot_offset.y);
        f(Stubs::kMbShotOffset + 8, shot_offset.z);
        f(Stubs::kMbKeepSeconds, keep_seconds);
        return w;
    }
};

// ---- The player ----

/// Why there is no player now (Ok: the stubs may act). In the order checked.
enum class PlayerGuard : uint8_t {
    Ok,
    NoRoot,     ///< [0xE14504] or its +8 not mapped, or zero
    NoAim,      ///< not the land or the air battle's aim object
    NoOwner,    ///< [aim + 0x8C] zero, not mapped, or not the owner's class
    NoActor,    ///< [owner + 0x1C] zero, not aligned, not an actor, or not named CHID_PIT
    Unreadable, ///< the actor's transform not readable, or not finite
    NoEyeNode,  ///< [owner + 0x30] zero
    NotPlaying, ///< u8 [0xE17514] != 1 (the actor is known all the same)
};

inline const char* PlayerGuardName(PlayerGuard g) {
    switch (g) {
    case PlayerGuard::Ok:
        return "ok";
    case PlayerGuard::NotPlaying:
        return "not playing";
    case PlayerGuard::NoRoot:
        return "no aim root";
    case PlayerGuard::NoAim:
        return "no aim";
    case PlayerGuard::NoOwner:
        return "no aim owner";
    case PlayerGuard::NoActor:
        return "no player actor";
    case PlayerGuard::NoEyeNode:
        return "no camera node";
    case PlayerGuard::Unreadable:
        return "player unreadable";
    }
    return "?";
}

struct Player {
    PlayerGuard guard = PlayerGuard::NoRoot;
    bool land = false; ///< the land battle's aim (else the air battle's)
    uint32_t aim = 0;
    uint32_t owner = 0;
    uint32_t actor = 0; ///< set once the actor and its transform passed their guards
    uint32_t eye_node = 0;
    Quat world_q, local_q;
    Vec3 world_p, local_p;
    uint32_t local_scale_bits = 0;
    bool Ok() const {
        return guard == PlayerGuard::Ok;
    }
};

inline bool ReadVec3(const Reader& m, uint32_t addr, Vec3& v) {
    uint32_t a = 0, b = 0, c = 0;
    if (!m.Read32(addr, a) || !m.Read32(addr + 4, b) || !m.Read32(addr + 8, c)) {
        return false;
    }
    v = {std::bit_cast<float>(a), std::bit_cast<float>(b), std::bit_cast<float>(c)};
    return Finite(v);
}
inline bool ReadQuat(const Reader& m, uint32_t addr, Quat& q) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    if (!m.Read32(addr, a) || !m.Read32(addr + 4, b) || !m.Read32(addr + 8, c) ||
        !m.Read32(addr + 12, d)) {
        return false;
    }
    q = {std::bit_cast<float>(a), std::bit_cast<float>(b), std::bit_cast<float>(c),
         std::bit_cast<float>(d)};
    return Finite(q);
}

/// The local player's actor, land and air, each pointer checked mapped before it is read:
/// aim = [[0xE14504] + 8] - 0x10 (the land or air aim's class), owner = [aim + 0x8C] (its
/// class), actor = [owner + 0x1C] (the actor's class, named "CHID_PIT"), the camera node
/// [owner + 0x30], the actor's two transform blocks, and last the playing flag: with every
/// other guard passed and the flag down (a pause, a cutscene) the actor is still named
/// (`actor`), so the player can be shown again, but nothing acts on him.
inline Player FindPlayer(const Reader& m) {
    namespace KT = KiuTurn;
    Player p;
    uint8_t b = 0;
    uint32_t root = 0, next = 0;
    if (!m.Read32(KT::kAimRoot, root) || root == 0 || (root & 3u) != 0 ||
        !m.Read32(root + KT::kAimRootNext, next) || next < KT::kAimBack || (next & 3u) != 0) {
        p.guard = PlayerGuard::NoRoot;
        return p;
    }
    const uint32_t aim = next - KT::kAimBack;
    uint32_t vtable = 0;
    if (!m.Read32(aim, vtable) || (vtable != KT::kAimVtableLand && vtable != KT::kAimVtableAir)) {
        p.guard = PlayerGuard::NoAim;
        return p;
    }
    p.aim = aim;
    p.land = vtable == KT::kAimVtableLand;
    uint32_t owner = 0;
    if (!m.Read32(aim + KT::kAimOwner, owner) || owner == 0 || (owner & 3u) != 0 ||
        !m.Read32(owner, vtable) || vtable != kOwnerVtable) {
        p.guard = PlayerGuard::NoOwner;
        return p;
    }
    p.owner = owner;
    uint32_t actor = 0, name = 0;
    if (!m.Read32(owner + kOwnerActor, actor) || actor == 0 || (actor & 3u) != 0 ||
        !m.Read32(actor, vtable) || vtable != kActorVtable ||
        !m.Read32(actor + kActorName, name) || name == 0) {
        p.guard = PlayerGuard::NoActor;
        return p;
    }
    static constexpr char kName[] = "CHID_PIT";
    for (uint32_t i = 0; i < sizeof(kName); ++i) { // the NUL too: not "CHID_PIT:<a bullet>"
        if (!m.Read8(name + i, b) || b != static_cast<uint8_t>(kName[i])) {
            p.guard = PlayerGuard::NoActor;
            return p;
        }
    }
    if (!ReadQuat(m, actor + kActorWorldQuat, p.world_q) ||
        !ReadVec3(m, actor + kActorWorldPos, p.world_p) ||
        !ReadQuat(m, actor + kActorLocalQuat, p.local_q) ||
        !ReadVec3(m, actor + kActorLocalPos, p.local_p) ||
        !m.Read32(actor + kActorLocalScale, p.local_scale_bits)) {
        p.guard = PlayerGuard::Unreadable;
        return p;
    }
    p.actor = actor;
    if (!m.Read32(owner + kOwnerEyeNode, p.eye_node) || p.eye_node == 0) {
        p.guard = PlayerGuard::NoEyeNode;
        return p;
    }
    if (!m.Read8(KT::kPlaying, b) || b != 1) {
        p.guard = PlayerGuard::NotPlaying;
        return p;
    }
    p.guard = PlayerGuard::Ok;
    return p;
}

// ---- What fills the mailbox ----

/// First person from the game's side. `offset` is in the actor's own axes (y up).
struct EyeRequest {
    bool on = false;
    Vec3 offset{0.0f, 12.3f, 0.0f};
    /// AzaharFrame kiufov: the offset in these axes (world) instead of the player's own: an eye
    /// moved behind the player stays behind the camera's view when his body turns to fire.
    bool own_axes = false;
    Quat axes;
};
/// AzaharFrame kiufov: the level yaw of a view direction (world), as axes for an EyeRequest:
/// +z along the direction's level part, +y up. Identity when the direction is (nearly) vertical.
inline Quat LevelYawAxes(const Vec3& direction) {
    const float h = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (!(h > 1e-4f) || !std::isfinite(h)) {
        return {};
    }
    const float yaw = std::atan2(direction.x, direction.z);
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}
inline constexpr Vec3 kEyeOffsetDefault{0.0f, 12.3f, 0.0f};

/// The player's shots. What a later lane fills from the controller; here from a test switch.
struct ShotRequest {
    bool rotate = false;
    Quat rotation;       ///< in the spawn's space: each bullet's quaternion q becomes rotation * q
    bool move = false;
    Vec3 offset;         ///< from the player's position, world axes (= the spawn's on land)
    bool no_target = false;
    /// AzaharFrame kiuvr: `rotation` is the aim itself (the game's kind of aim quaternion for
    /// the direction wanted, KiuRoom::ShotQuat): each bullet's q becomes
    /// rotation * conj(the game's own aim before scatter) * q, so it keeps its scatter and the
    /// weapon's tilt about the new direction. The stub reads the game's aim at the spawn.
    bool absolute = false;
};

/// The rotation of the player's parent (the rail in the air; none on land, where it comes out
/// the identity): world = parent * local.
inline Quat ParentRotation(const Player& p) {
    return Normalized(Mul(p.world_q, Conj(p.local_q)));
}

/// The test switch's shot, laid out on the game camera's view direction made level (`view`:
/// from the scene camera's eye to its look-at; the camera does not turn when the player fires,
/// his body does): `yaw` turns the game's aim about the world's up axis (+y; by the right-hand
/// rule, so +z turns toward +x), `pitch` lifts it about the view's level side axis (positive:
/// up), `offset` is (side, up, ahead) of the view, side = view x up.
inline ShotRequest ShotTest(const Vec3& view, float yaw_rad, float pitch_rad, const Vec3& offset,
                            bool move, bool no_target) {
    ShotRequest r;
    Vec3 ahead{view.x, 0.0f, view.z};
    const float n = std::sqrt(ahead.x * ahead.x + ahead.z * ahead.z);
    if (n > 1e-4f && std::isfinite(n)) {
        ahead = {ahead.x / n, 0.0f, ahead.z / n};
    } else {
        ahead = {0.0f, 0.0f, 1.0f}; // looking straight up or down: any level direction
    }
    const Vec3 side{-ahead.z, 0.0f, ahead.x}; // ahead x up: turning about it lifts `ahead`
    r.rotation = Normalized(Mul(AxisAngle({0.0f, 1.0f, 0.0f}, yaw_rad), AxisAngle(side, pitch_rad)));
    r.rotate = yaw_rad != 0.0f || pitch_rad != 0.0f;
    r.move = move;
    r.offset = {side.x * offset.x + ahead.x * offset.z, offset.y,
                side.z * offset.x + ahead.z * offset.z};
    r.no_target = no_target;
    return r;
}

/// The mailbox for this VBlank. No player: the eye and the shot off (their stubs pass through).
/// `keep_seconds` needs no player (it is about the enemies): not finite or negative counts as 0,
/// with which every passed flier vanishes as in the game.
inline Mailbox Fill(const Player& p, const EyeRequest& eye, const ShotRequest& shot,
                    uint32_t serial, float keep_seconds = 0.0f) {
    Mailbox mb;
    mb.serial = serial;
    mb.keep_seconds = std::isfinite(keep_seconds) && keep_seconds > 0.0f ? keep_seconds : 0.0f;
    if (!p.Ok()) {
        return mb;
    }
    mb.player = p.actor;
    mb.eye_node = p.eye_node;
    const Vec3 eye_offset =
        Rotate(Normalized(eye.own_axes ? eye.axes : p.world_q), eye.offset); // AzaharFrame kiufov
    if (eye.on && Finite(eye_offset)) {
        mb.eye_on = true;
        mb.eye_offset = eye_offset;
    }
    mb.shot_frame = ParentRotation(p);
    if (shot.rotate && Finite(shot.rotation)) {
        mb.shot_rotate = true;
        mb.shot_rotation = Normalized(shot.rotation);
    }
    if (shot.move && Finite(shot.offset)) {
        mb.shot_move = true;
        mb.shot_offset = shot.offset;
    }
    mb.shot_no_target = shot.no_target;
    mb.shot_absolute = mb.shot_rotate && shot.absolute;
    return mb;
}

// ---- Hiding the player (a data write, not a patch) ----

inline constexpr float kHiddenScale = 0.001f;

/// AZAHARFRAME_KIU_HIDE_PLAYER: what to write into the actor's local scale, if anything. On: 1.0
/// becomes 0.001 (any other value is the game's doing and stays). Off: exactly our 0.001 goes
/// back to 1.0.
inline std::optional<uint32_t> HideWrite(uint32_t scale_bits, bool want) {
    if (want && scale_bits == std::bit_cast<uint32_t>(1.0f)) {
        return std::bit_cast<uint32_t>(kHiddenScale);
    }
    if (!want && scale_bits == std::bit_cast<uint32_t>(kHiddenScale)) {
        return std::bit_cast<uint32_t>(1.0f);
    }
    return std::nullopt;
}

// ---- Switch values ----

/// "a,b,c" (commas or spaces) into `out`: how many numbers were read (at most `max`); 0 when a
/// number is malformed or not finite.
inline int ParseFloats(const char* text, float* out, int max) {
    if (!text) {
        return 0;
    }
    int n = 0;
    const char* p = text;
    while (*p != '\0') {
        while (*p == ' ' || *p == ',' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p || !std::isfinite(v) || n >= max) {
            return 0;
        }
        out[n++] = v;
        p = end;
    }
    return n;
}

// ---- Putting the patches in and taking them out ----

/// The game's memory with writes (the emulation thread's; a fake one in the tests).
class GameMemory : public Reader {
public:
    /// A code word: written, then the CPU cores' code caches are dropped for it. False: not
    /// mapped, nothing written.
    virtual bool WriteCode(uint32_t addr, uint32_t value) = 0;
    /// A data word (the mailbox, the actor's scale): no cache to drop.
    virtual bool WriteData(uint32_t addr, uint32_t value) = 0;
};

enum class GroupState : uint8_t {
    Out,        ///< not wanted; its sites hold the game's words (or were just put back)
    In,         ///< wanted and in place
    Different,  ///< wanted, but a site or a context word is neither the game's nor ours
    NoCave,     ///< wanted, but the cave holds something that is not ours (or cannot be read)
    Unreadable, ///< a site is not mapped
};

inline const char* GroupStateName(GroupState s) {
    switch (s) {
    case GroupState::Out:
        return "out";
    case GroupState::In:
        return "in";
    case GroupState::Different:
        return "off: the game's code differs";
    case GroupState::NoCave:
        return "off: the code cave is in use";
    case GroupState::Unreadable:
        return "off: not readable";
    }
    return "?";
}

/// What the cave's image area holds.
enum class Cave : uint8_t {
    NotLooked,
    Empty,   ///< the whole cave is zero
    Ours,    ///< this build's image, word for word
    Older,   ///< another version of our image (its tag): replaced
    Foreign, ///< something else: never written
    Unreadable,
};

struct SyncResult {
    std::array<GroupState, kGroupCount> state{};
    Cave cave = Cave::NotLooked;
    uint32_t code_writes = 0;  ///< code words written this time (sites and image)
    uint32_t image_writes = 0; ///< of them, the image's
    uint32_t in_mask = 0;      ///< the groups in place after this
    bool mailbox_written = false;
    /// The first word that kept a wanted group out (0: none).
    uint32_t different_addr = 0;
    uint32_t different_word = 0;
    GroupState Of(Group g) const {
        return state[static_cast<std::size_t>(g)];
    }
};

inline Cave LookAtCave(const Reader& m) {
    bool ours = true;
    for (std::size_t i = 0; i < Stubs::kImage.size(); ++i) {
        uint32_t v = 0;
        if (!m.Read32(kImageBase + static_cast<uint32_t>(i) * 4u, v)) {
            return Cave::Unreadable;
        }
        if (v != Stubs::kImage[i]) {
            ours = false;
            break;
        }
    }
    if (ours) {
        return Cave::Ours;
    }
    uint32_t tag = 0;
    if (!m.Read32(kImageBase, tag)) {
        return Cave::Unreadable;
    }
    if (tag == Stubs::kImageTag) {
        return Cave::Older;
    }
    // Nothing of ours: the whole cave must be untouched (the game leaves it zero).
    for (uint32_t addr = (kCaveStart + 3u) & ~3u; addr < kCaveEnd; addr += 4) {
        uint32_t v = 0;
        if (!m.Read32(addr, v)) {
            return Cave::Unreadable;
        }
        if (v != 0) {
            return Cave::Foreign;
        }
    }
    return Cave::Empty;
}

/// One VBlank's pass: reads every site, puts the wanted groups in (the image first, then the
/// mailbox, the site words last) and the others out. `wanted` is a mask of Bit(group). Writes
/// nothing when everything already is as wanted (apart from the mailbox while a stub group is
/// in). The caller has checked the title and the game's code (GameCodeMatches).
inline SyncResult Sync(GameMemory& m, uint32_t wanted, const Mailbox& mailbox) {
    SyncResult r;
    enum : uint8_t { kGame, kOurs, kSibling, kOther, kUnmapped };
    // What a site holds for patch `p`: the game's word, this patch's, the word of another patch
    // of ours at the same address (two groups may share a site; one of them is wanted at most),
    // or something else.
    const auto look = [&m](const Patch& p, uint32_t& v) -> uint8_t {
        if (!m.Read32(p.addr, v)) {
            return kUnmapped;
        }
        if (v == p.original) {
            return kGame;
        }
        if (v == p.patched) {
            return kOurs;
        }
        for (const Patch& q : kPatches) {
            if (q.addr == p.addr && q.patched == v) {
                return kSibling;
            }
        }
        return kOther;
    };
    std::array<bool, kGroupCount> usable{};
    usable.fill(true);
    std::array<bool, kGroupCount> readable{};
    readable.fill(true);
    const auto different = [&r](uint32_t addr, uint32_t word) {
        if (r.different_addr == 0) {
            r.different_addr = addr;
            r.different_word = word;
        }
    };
    for (const Patch& p : kPatches) {
        const auto g = static_cast<std::size_t>(p.group);
        uint32_t v = 0;
        const uint8_t state = look(p, v);
        if (state == kUnmapped) {
            usable[g] = false;
            readable[g] = false;
        } else if (state == kOther) {
            usable[g] = false;
            if ((wanted & Bit(p.group)) != 0) {
                different(p.addr, v);
            }
        }
    }
    for (const Context& c : kContext) {
        if ((wanted & Bit(c.group)) == 0) {
            continue; // only a group about to go in needs its surroundings checked
        }
        uint32_t v = 0;
        if (!m.Read32(c.addr, v) || v != c.word) {
            usable[static_cast<std::size_t>(c.group)] = false;
            different(c.addr, v);
        }
    }
    // The cave, when a stub group is to go in.
    bool stubs_wanted = false;
    for (std::size_t g = 0; g < kGroupCount; ++g) {
        const auto group = static_cast<Group>(g);
        stubs_wanted = stubs_wanted || (UsesStubs(group) && (wanted & Bit(group)) != 0 && usable[g]);
    }
    bool cave_ok = false;
    if (stubs_wanted) {
        r.cave = LookAtCave(m);
        if (r.cave == Cave::Empty || r.cave == Cave::Older) {
            bool wrote = true;
            for (std::size_t i = 0; i < Stubs::kImage.size() && wrote; ++i) {
                wrote = m.WriteCode(kImageBase + static_cast<uint32_t>(i) * 4u, Stubs::kImage[i]);
                r.code_writes += wrote ? 1 : 0;
                r.image_writes += wrote ? 1 : 0;
            }
            if (wrote) {
                r.cave = Cave::Ours;
            } else {
                r.cave = Cave::Unreadable;
            }
        }
        cave_ok = r.cave == Cave::Ours;
        if (cave_ok) {
            const auto words = mailbox.Words();
            bool wrote = true;
            for (uint32_t i = 0; i < kMailboxWords && wrote; ++i) {
                wrote = m.WriteData(kMailbox + i * 4u, words[i]);
            }
            r.mailbox_written = wrote;
            cave_ok = wrote;
        }
    }
    // The sites, last: first every group that goes out (its words back to the game's), then
    // every group that goes in, each site read again (a shared site may just have changed).
    std::array<bool, kGroupCount> go_in{};
    for (std::size_t g = 0; g < kGroupCount; ++g) {
        const auto group = static_cast<Group>(g);
        go_in[g] = (wanted & Bit(group)) != 0 && usable[g] && (!UsesStubs(group) || cave_ok);
    }
    for (const bool in : {false, true}) {
        for (const Patch& p : kPatches) {
            if (go_in[static_cast<std::size_t>(p.group)] != in) {
                continue;
            }
            uint32_t v = 0;
            const uint8_t state = look(p, v);
            if (in && (state == kGame || state == kSibling)) {
                r.code_writes += m.WriteCode(p.addr, p.patched) ? 1 : 0;
            } else if (!in && state == kOurs) {
                r.code_writes += m.WriteCode(p.addr, p.original) ? 1 : 0;
            }
        }
    }
    for (std::size_t g = 0; g < kGroupCount; ++g) {
        const auto group = static_cast<Group>(g);
        const bool want = (wanted & Bit(group)) != 0;
        bool all_in = true;
        for (const Patch& p : kPatches) {
            uint32_t v = 0;
            if (p.group == group) {
                all_in = all_in && look(p, v) == kOurs;
            }
        }
        GroupState state = GroupState::Out;
        if (go_in[g] && all_in) {
            state = GroupState::In;
            r.in_mask |= Bit(group);
        } else if (want && !readable[g]) {
            state = GroupState::Unreadable;
        } else if (want && !usable[g]) {
            state = GroupState::Different;
        } else if (want) {
            state = r.cave == Cave::Foreign ? GroupState::NoCave : GroupState::Unreadable;
        }
        r.state[g] = state;
    }
    return r;
}

/// True when a site still holds one of our words (a pass with nothing wanted puts them back).
inline bool AnyOurs(const Reader& m) {
    for (const Patch& p : kPatches) {
        uint32_t v = 0;
        if (m.Read32(p.addr, v) && v == p.patched) {
            return true;
        }
    }
    return false;
}

/// The game is the European original where the hooks reach: kiu_turn.h's nine words (the other
/// regions differ there; the C-Stick & Gyro mod's EUR builds do not), then each group's own
/// sites and context in Sync.
inline bool GameCodeMatches(const Reader& m) {
    return KiuTurn::CodeMatches(m);
}

} // namespace FrameXr::Input::KiuHooks
