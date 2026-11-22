// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhook: the app's own small patches to Kid Icarus: Uprising's ORIGINAL code, in
// the game's memory only (nothing is shipped as game files), the pure part: the patch table, the
// code cave's stubs and their mailbox, and the rules for putting them in and taking them out.
// No emulator or OpenXR types here, so tests/frame_xr/kiu_hooks.cpp checks it over a fake memory.
// frame_xr/kiu_hooks.h runs it on the emulation thread at each VBlank; KIU_HOOKS.md beside this
// file has every address and why. Angel's Aim (the stored mode 4) turns these groups on for the European game; the
// test switches add to them (no setting, no row, no text in the app).
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
//     before the first site that jumps to it and not cleared after (a guest thread may be stopped
//     inside it). An image of an older version (a save state from another build) is rewritten,
//     and first every site that still branches into the cave with a word that is not ours goes
//     back to the game's word (BranchesIntoImage), so no site jumps into a moved entry. The stubs read the mailbox, the cave's last bytes, which the
//     app writes at each VBlank; the cave is read-only and executable for the game, so nothing
//     of the game can spoil it, and the stubs never write there.
//   - every code word written is followed by a code cache drop for it (the emulator part).

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
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
inline constexpr uint32_t kSiteNodeFov = 0x45E04C;      ///< a camera node's angle setter: its entry
inline constexpr uint32_t kEyeStub = kImageBase + Stubs::kEyeStub;
inline constexpr uint32_t kKeepStub = kImageBase + Stubs::kKeepStub;
inline constexpr uint32_t kShotStub = kImageBase + Stubs::kShotStub;
inline constexpr uint32_t kShadowStub = kImageBase + Stubs::kShadowStub;
inline constexpr uint32_t kFovStub = kImageBase + Stubs::kFovStub;

inline constexpr uint32_t kSceneEye = 0xE14B90;    ///< the scene camera's eye, 3 floats
inline constexpr uint32_t kSceneLookAt = 0xE14B9C; ///< and its look-at point
inline constexpr uint32_t kSceneUp = 0xE14BA8;     ///< and its up vector
inline constexpr uint32_t kSceneFovY = 0xE14BC4;   ///< the scene projection's angle, radians
inline constexpr uint32_t kSceneAspect = 0xE14BC8; ///< and its width over its height
/// AzaharFrame kiuwide: the scene camera's node (kSceneEye is its +8, kSceneFovY its +0x3C,
/// kSceneAspect its +0x40); its flags at +0x6C, whose bits kNodeDirty the angle setter clears so
/// that the projection (and what is made from it) is built again.
inline constexpr uint32_t kSceneNode = 0xE14B88;
inline constexpr uint32_t kNodeFlags = 0x6C;
inline constexpr uint32_t kNodeDirty = 0x5C;
static_assert(kSceneFovY == kSceneNode + 0x3C && kSceneAspect == kSceneNode + 0x40 &&
              kSceneEye == kSceneNode + 8);

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
static_assert(Stubs::kImage[Stubs::kLitFovContinue / 4] == kSiteNodeFov + 4); // AzaharFrame kiuwide
static_assert(Stubs::kImage[Stubs::kFovPass / 4] == 0xEDD00A0F,
              "the fov stub's pass path runs the instruction it replaces");
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
    /// AzaharFrame kiuwide: the scene camera's angle and shape are the app's (the mailbox's), so
    /// the game's own frame is as wide as the view in first person; the game's zooms do nothing
    /// (a stub).
    WideCamera,
};
inline constexpr std::size_t kGroupCount = 12;

constexpr uint32_t Bit(Group g) {
    return 1u << static_cast<uint32_t>(g);
}
constexpr bool UsesStubs(Group g) {
    return g == Group::Eye || g == Group::Shot || g == Group::EnemyKeepTimed ||
           g == Group::PlayerShadow || g == Group::WideCamera;
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
    case Group::WideCamera:
        return "wide camera";
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
inline constexpr std::array<Patch, 14> kPatches{{
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
    // AzaharFrame kiuwide. vldr s1, [r0, #0x3C] (the first instruction of a camera node's angle
    // setter: r0 the node, s0 the angle) -> b fov_stub, which for the mailbox's node makes the
    // angle and shape the mailbox's and otherwise runs the vldr and goes on.
    {Group::WideCamera, kSiteNodeFov, 0xEDD00A0F, B(kSiteNodeFov, kFovStub)},
}};

/// Words a group relies on without changing them (where it jumps to, what the stub assumes of the
/// code after its site): they must be the game's too.
struct Context {
    Group group;
    uint32_t addr;
    uint32_t word;
};
inline constexpr std::array<Context, 75> kContext{{
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
    // AzaharFrame kiuwide: the angle setter after its first word, as the stub stands in for it
    // (the angle at the node's +0x3C, the dirty bits 0x5C of its +0x6C, nothing else written);
    // the node's shape at +0x40 (its constructor's store) and the projection builder's reads of
    // the angle and the shape (the projection at the node's +0x2C: its +0x10 and +0x14).
    {Group::WideCamera, 0x45E050, 0xEEF40A40},     // vcmp.f32 s1, s0
    {Group::WideCamera, 0x45E054, 0xEEF1FA10},     // vmrs APSR_nzcv, fpscr
    {Group::WideCamera, 0x45E058, 0x0A000003},     // beq 0x45E06C
    {Group::WideCamera, 0x45E05C, 0xED800A0F},     // vstr s0, [r0, #0x3C]
    {Group::WideCamera, 0x45E060, 0xE590106C},     // ldr r1, [r0, #0x6C]
    {Group::WideCamera, 0x45E064, 0xE3C1105C},     // bic r1, r1, #0x5C
    {Group::WideCamera, 0x45E068, 0xE580106C},     // str r1, [r0, #0x6C]
    {Group::WideCamera, 0x45E06C, 0xE12FFF1E},     // bx lr
    {Group::WideCamera, 0x45DFF4, 0xED841A10},     // vstr s2, [r4, #0x40] (the shape)
    {Group::WideCamera, 0xB4FB38, 0xED910A04},     // vldr s0, [r1, #0x10] (the angle)
    {Group::WideCamera, 0xB4FB40, 0xED919A05},     // vldr s18, [r1, #0x14] (the shape)
}};

// ---- USA Rev 1 (AzaharFrame kiuusa; KIU_HOOKS.md, "The builds") ----
//
// The same code as the European game's, relocated: every site and context word below is the
// European row's, at the place the USA Rev 1 code holds the same instructions (found by matching
// the two games' text segments, every branch's target mapped the same way; reports/96/kiuusa).
// The words are the same but for relative branches whose two ends moved apart. The code cave
// stays where it is: USA Rev 1's text ends at 0xC950F0, so 0xC95370-0xC96000 lies in its unused
// tail too (all zero, nothing refers to it), and the image and the mailbox keep their addresses.

/// The stubs' image for other code: kImage with the literals that name the game's code replaced
/// (the stubs reach everything outside the image through these, so nothing else changes).
struct StubTargets {
    uint32_t sink_continue, spawn, path_restart, path_vanish, shadow_continue, fov_continue;
};
constexpr std::array<uint32_t, Stubs::kImage.size()> ImageFor(const StubTargets& t) {
    std::array<uint32_t, Stubs::kImage.size()> image = Stubs::kImage;
    image[Stubs::kLitSinkContinue / 4] = t.sink_continue;
    image[Stubs::kLitSpawn / 4] = t.spawn;
    image[Stubs::kLitPathRestart / 4] = t.path_restart;
    image[Stubs::kLitPathVanish / 4] = t.path_vanish;
    image[Stubs::kLitShadowContinue / 4] = t.shadow_continue;
    image[Stubs::kLitFovContinue / 4] = t.fov_continue;
    return image;
}
inline constexpr StubTargets kStubTargetsEur{Stubs::kSinkContinue, Stubs::kSpawn,
                                              Stubs::kPathRestart,   Stubs::kPathVanish,
                                              Stubs::kShadowContinue, Stubs::kFovContinue};
static_assert(ImageFor(kStubTargetsEur) == Stubs::kImage, "the European image is the generated one");
inline constexpr StubTargets kStubTargetsUsaRev1{0x633F54, 0x4855A0, 0x3E8C44,
                                                  0x3E8C68, 0x57AF88, 0x45DFB8};
inline constexpr std::array<uint32_t, Stubs::kImage.size()> kImageUsaRev1 =
    ImageFor(kStubTargetsUsaRev1);

/// kPatches' rows, in order.
inline constexpr std::array<Patch, 14> kPatchesUsaRev1{{
    {Group::NoCull, 0x77D9AC, 0xE20100FF, B(0x77D9AC, 0x77DAB4)},
    {Group::NoCull, 0x7730FC, 0xE1A01004, B(0x7730FC, 0x7730E8)},
    {Group::BulletKeep, 0x41FE84, 0xEB0331B9, kMovR0One}, // bl 0x4EC570
    {Group::BulletKeep, 0x40BCD4, 0xEB038225, kMovR0One}, // bl 0x4EC570
    {Group::Eye, 0x633F50, 0xE92D4070, B(0x633F50, kEyeStub)},
    {Group::Shot, 0x504314, 0xEBFE04A1, Bl(0x504314, kShotStub)}, // bl 0x4855A0
    {Group::EnemyKeep, 0x3E8C04, 0x0A000017, Beq(0x3E8C04, 0x3E8C44)},
    {Group::EnemyKeepTimed, 0x3E8C04, 0x0A000017, Beq(0x3E8C04, kKeepStub)},
    {Group::GroupKeep, 0xA42E7C, 0xE92D4FF8, kBxLr},
    {Group::NoFollow, 0x703E84, 0xEB031897, kNop}, // bl 0x7CA0E8
    {Group::AimHold, 0x6D2010, 0x0A000002, B(0x6D2010, 0x6D2020)},
    {Group::AimBox, 0x6D1534, 0x0A00001F, B(0x6D1534, 0x6D15B8)},
    {Group::PlayerShadow, 0x57AF84, 0xE92D4030, B(0x57AF84, kShadowStub)},
    {Group::WideCamera, 0x45DFB4, 0xEDD00A0F, B(0x45DFB4, kFovStub)},
}};
/// kContext's rows, in order (the same instructions; see kContext for what each is).
inline constexpr std::array<Context, 75> kContextUsaRev1{{
    {Group::NoCull, 0x77D98C, 0xE24DD00C},
    {Group::NoCull, 0x77DAB4, 0xE1C401D8},
    {Group::NoCull, 0x77DAB8, 0xE28DD00C},
    {Group::NoCull, 0x77DABC, 0xE3A020FF},
    {Group::NoCull, 0x7730D8, 0xE5940014},
    {Group::NoCull, 0x7730E8, 0xE3C00010},
    {Group::NoCull, 0x7730EC, 0xE5840014},
    {Group::NoCull, 0x7730F4, 0xE3A00001},
    {Group::BulletKeep, 0x41FE88, 0xE3500000},
    {Group::BulletKeep, 0x41FE8C, 0xE5D60020},
    {Group::BulletKeep, 0x40BCD8, 0xE3500000},
    {Group::BulletKeep, 0x40BCDC, 0xE5D50000},
    {Group::Eye, 0x633F54, 0xE2821004},
    {Group::Eye, 0x633F58, 0xE1A04000},
    {Group::Eye, 0x633F5C, 0xEC910A07},
    {Group::Eye, 0x633F68, 0xE2821014},
    {Group::Eye, 0x633F70, 0xE1A0500D},
    {Group::Shot, 0x5042F8, 0xE8830404},
    {Group::Shot, 0x504304, 0xE88D1040},
    {Group::Shot, 0x50430C, 0xE1A02007},
    {Group::Shot, 0x4855A0, 0xE92D4FFF},
    {Group::Shot, 0x5042F0, 0xE28D204C},
    {Group::Shot, 0x503EC8, 0xE24DD08C},
    {Group::Shot, 0x503ECC, 0xE28D3070},
    {Group::Shot, 0x503F44, 0xE12FFF3C},
    {Group::Shot, 0x5042FC, 0xE28DC058},
    {Group::EnemyKeep, 0x3E8B9C, 0xE92D40F8},
    {Group::EnemyKeep, 0x3E8BA0, 0xE1A04000},
    {Group::EnemyKeep, 0x3E8C00, 0xE1B01A21},
    {Group::EnemyKeep, 0x3E8C44, 0xE3A05000},
    {Group::EnemyKeep, 0x3E8C64, 0xE8BD80F8},
    {Group::EnemyKeepTimed, 0x3E8B9C, 0xE92D40F8},
    {Group::EnemyKeepTimed, 0x3E8BA0, 0xE1A04000},
    {Group::EnemyKeepTimed, 0x3E8C00, 0xE1B01A21},
    {Group::EnemyKeepTimed, 0x3E8C44, 0xE3A05000},
    {Group::EnemyKeepTimed, 0x3E8C68, 0xE5945030},
    {Group::GroupKeep, 0xA42E80, 0xE280106C},
    {Group::NoFollow, 0x703E80, 0xE58D0050},
    {Group::NoFollow, 0x704024, 0xE59D3050},
    {Group::NoFollow, 0x704038, 0xEB031B7C},
    {Group::AimHold, 0x6D200C, 0xE24DD0B0},
    {Group::AimHold, 0x6D2014, 0xE5D400D5},
    {Group::AimHold, 0x6D2020, 0xE5C45208},
    {Group::AimHold, 0x6D2024, 0xE28DD0B0},
    {Group::AimBox, 0x6D1530, 0xE3510000},
    {Group::AimBox, 0x6D15B8, 0xED9F0A6D},
    {Group::AimBox, 0x6D15BC, 0xEDDF0A6D},
    {Group::AimBox, 0x6D15C0, 0xE28400F0},
    {Group::AimBox, 0x6D15C4, 0xEC800A02},
    {Group::AimBox, 0x6D15C8, 0xEA00001D},
    {Group::AimBox, 0x6D1774, 0x3F800000},
    {Group::AimBox, 0x6D1778, 0xBF800000},
    {Group::AimHold, 0x6D2050, 0xEB055118}, // bl 0x8264B8 (EUR EB05512B)
    {Group::AimHold, 0x6D26C0, 0xEB10FED4}, // bl 0xB12218 (EUR EB10FF21)
    {Group::AimHold, 0x6D2E30, 0xE594008C},
    {Group::PlayerShadow, 0x57AF88, 0xE1A04000},
    {Group::PlayerShadow, 0x57AF8C, 0xED2D8B02},
    {Group::PlayerShadow, 0x57AF90, 0xE24DD02C},
    {Group::PlayerShadow, 0x57AF94, 0xE59020AC},
    {Group::PlayerShadow, 0x57AFBC, 0xE5945030},
    {Group::PlayerShadow, 0x57B004, 0xEA00DD71}, // b 0x5B25D0 (EUR EA00DD74)
    {Group::PlayerShadow, 0x57B0C0, 0xEBFE2869},
    {Group::PlayerShadow, 0x57B0C8, 0xECBD8B02},
    {Group::PlayerShadow, 0x57B0CC, 0xE8BD8030},
    {Group::WideCamera, 0x45DFB8, 0xEEF40A40},
    {Group::WideCamera, 0x45DFBC, 0xEEF1FA10},
    {Group::WideCamera, 0x45DFC0, 0x0A000003},
    {Group::WideCamera, 0x45DFC4, 0xED800A0F},
    {Group::WideCamera, 0x45DFC8, 0xE590106C},
    {Group::WideCamera, 0x45DFCC, 0xE3C1105C},
    {Group::WideCamera, 0x45DFD0, 0xE580106C},
    {Group::WideCamera, 0x45DFD4, 0xE12FFF1E},
    {Group::WideCamera, 0x45DF5C, 0xED841A10},
    {Group::WideCamera, 0xB4F8B8, 0xED910A04},
    {Group::WideCamera, 0xB4F8C0, 0xED919A05},
}};
// The game's own words, from the encoders (the USA Rev 1 places).
static_assert(Bl(0x504314, kStubTargetsUsaRev1.spawn) == 0xEBFE04A1);
static_assert(Bl(0x41FE84, 0x4EC570) == 0xEB0331B9 && Bl(0x40BCD4, 0x4EC570) == 0xEB038225);
static_assert(B(0x77D9AC, 0x77DAB4) == 0xEA000040 && B(0x7730FC, 0x7730E8) == 0xEAFFFFF9);
static_assert(Bl(0x703E84, 0x7CA0E8) == 0xEB031897);
static_assert(Beq(0x6D2010, 0x6D2020) == 0x0A000002 && Beq(0x6D1534, 0x6D15B8) == 0x0A00001F);
static_assert(Beq(0x3E8C04, kStubTargetsUsaRev1.path_vanish) == 0x0A000017);
static_assert(Beq(0x3E8C04, kStubTargetsUsaRev1.path_restart) == 0x0A00000E);
static_assert(B(0x57B004, 0x5B25D0) == 0xEA00DD71 && Bl(0x57B0C0, 0x50526C) == 0xEBFE2869);
static_assert(kStubTargetsUsaRev1.sink_continue == 0x633F50 + 4 &&
              kStubTargetsUsaRev1.shadow_continue == 0x57AF84 + 4 &&
              kStubTargetsUsaRev1.fov_continue == 0x45DFB4 + 4);

/// One build's code tables (its addresses in the data segments: KiuTurn::Build).
struct Code {
    const KiuTurn::Build* build;
    const std::array<Patch, 14>* patches;
    const std::array<Context, 75>* context;
    const std::array<uint32_t, Stubs::kImage.size()>* image;
};
inline constexpr Code kCodeEur{&KiuTurn::kBuildEur, &kPatches, &kContext, &Stubs::kImage};
inline constexpr Code kCodeUsaRev1{&KiuTurn::kBuildUsaRev1, &kPatchesUsaRev1, &kContextUsaRev1,
                                    &kImageUsaRev1};
/// The code tables of a build (EUR's for nullptr: the caller has checked the build).
constexpr const Code& CodeFor(const KiuTurn::Build* build) {
    return build == &KiuTurn::kBuildUsaRev1 ? kCodeUsaRev1 : kCodeEur;
}
static_assert(KiuTurn::kBuildEur.owner_vtable == kOwnerVtable &&
              KiuTurn::kBuildEur.actor_vtable == kActorVtable &&
              KiuTurn::kBuildUsaRev1.actor_vtable == Stubs::kActorVtable);

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
    /// AzaharFrame kiuwide: the scene camera node whose angle and shape are these (0: none, the
    /// game's own), and the game's own shape for it (what the app puts back; no stub reads it).
    uint32_t wide_node = 0;
    float wide_fov_y = 0.0f; ///< radians, up and down
    float wide_aspect = 0.0f;
    float wide_game_aspect = 0.0f;

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
        w[Stubs::kMbWideNode / 4] = wide_node;
        f(Stubs::kMbWideFov, wide_fov_y);
        f(Stubs::kMbWideAspect, wide_aspect);
        f(Stubs::kMbWideGameAspect, wide_game_aspect);
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
inline Player FindPlayer(const Reader& m, const KiuTurn::Build& build = KiuTurn::kBuildEur) {
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
    if (!m.Read32(aim, vtable) ||
        (vtable != build.aim_vtable_land && vtable != build.aim_vtable_air)) {
        p.guard = PlayerGuard::NoAim;
        return p;
    }
    p.aim = aim;
    p.land = vtable == build.aim_vtable_land;
    uint32_t owner = 0;
    if (!m.Read32(aim + KT::kAimOwner, owner) || owner == 0 || (owner & 3u) != 0 ||
        !m.Read32(owner, vtable) || vtable != build.owner_vtable) {
        p.guard = PlayerGuard::NoOwner;
        return p;
    }
    p.owner = owner;
    uint32_t actor = 0, name = 0;
    if (!m.Read32(owner + kOwnerActor, actor) || actor == 0 || (actor & 3u) != 0 ||
        !m.Read32(actor, vtable) || vtable != build.actor_vtable ||
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
    /// AzaharFrame kiuview: added after the offset is turned, world axes (the over-the-shoulder
    /// view, AZAHARFRAME_KIU_SHOULDER).
    Vec3 extra{0.0f, 0.0f, 0.0f};
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

/// AzaharFrame aa6dof2: first person's shots along the hand's ray (Angel's Aim with Direct Aim, or
/// AZAHARFRAME_KIU_SHOTS_6DOF): `aim` is the game's kind of aim quaternion for the ray
/// (KiuRoom::ShotQuat), taken as the aim itself (`absolute`: each bullet keeps the game's own
/// scatter and tilt about it); each bullet starts at the eye offset plus `offset` (the ray's
/// origin). No lock target unless `keep_lock` (AZAHARFRAME_KIU_SHOT_LOCK=1): with the game's
/// lock, measured on the desktop, a charged shot fired along a ray 8, 15 or 25 degrees beside
/// an enemy bent all the way onto it, so the shots left the reticle for whatever was near it.
inline ShotRequest ShotAlongRay(const Quat& aim, const Vec3& offset, bool keep_lock) {
    ShotRequest r;
    r.rotate = true;
    r.absolute = true;
    r.rotation = aim;
    r.move = true;
    r.offset = offset;
    r.no_target = !keep_lock;
    return r;
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
    const Vec3 turned =
        Rotate(Normalized(eye.own_axes ? eye.axes : p.world_q), eye.offset); // AzaharFrame kiufov
    const Vec3 eye_offset{turned.x + eye.extra.x, turned.y + eye.extra.y,
                          turned.z + eye.extra.z}; // AzaharFrame kiuview
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

/// AzaharFrame kiupit: whether first person hides the player's model by itself (HideWrite, as
/// AZAHARFRAME_KIU_HIDE_PLAYER; that switch at "1" or "0" wins: GroupWanted). The game draws him
/// see-through when its camera is near him, and in first person the eye is inside him: his head
/// and body show as a pale shape about the eye (KIU_HOOKS.md, "Pit's own model in first
/// person"). Only where nothing against the write was seen: on land (in the air it washes the
/// picture out and he takes less damage; on land his hurt boxes were never checked), with the
/// shots from the hand (their start is the mailbox's; where the game's own start, his weapon's
/// muzzle, goes with him shrunk was never looked at) and with the eye at his middle (an eye moved
/// behind him, over the shoulder or by EYE_OFFSET, is there to see him).
inline bool HideInFirstPerson(bool first_person, bool land, bool shots_from_hand,
                              bool eye_moved) {
    return first_person && land && shots_from_hand && !eye_moved;
}

/// AzaharFrame kiu131: the player's model hidden by its draw, not its size. The actor's model
/// node ([actor + 0x34C], class 0xD1E990) has a "drawn" byte at +0xB5 (1 drawn, 0 not; the word
/// at +0xB4 read 0x14010101 on land, the game never wrote it back in the desktop runs). Cleared,
/// nothing of him is drawn: body, wings and the weapon he holds, also while he fires (his
/// weapon's node then takes its bone's place without his scale: the scale write left a
/// full-size see-through weapon from his feet to the reticle on land; KIU_HOOKS.md, "The doubled
/// weapon"). His size, hurt boxes and bones are untouched, so it is used in the air too.
inline constexpr uint32_t kActorModel = 0x34C;
inline constexpr uint32_t kModelVtable = 0xD1E990;
static_assert(KiuTurn::kBuildEur.model_vtable == kModelVtable);
inline constexpr uint32_t kModelDrawnWord = 0xB4;
inline constexpr uint32_t kModelDrawnShift = 8;
/// The word to write at the model's +0xB4, if anything: hide clears a drawn byte of 1; show
/// sets a byte of 0 back to 1 (only what this app cleared: `ours`). Any other byte value is the
/// game's and stays.
inline std::optional<uint32_t> ModelDrawWrite(uint32_t word, bool hide, bool ours) {
    const uint32_t drawn = (word >> kModelDrawnShift) & 0xFFu;
    if (hide && drawn == 1u) {
        return word & ~(0xFFu << kModelDrawnShift);
    }
    if (!hide && ours && drawn == 0u) {
        return word | (1u << kModelDrawnShift);
    }
    return std::nullopt;
}
/// AZAHARFRAME_KIU_HIDE_BY=scale: first person hides him the kiupit way (the scale write, land
/// only, with the shots from the hand), for an A/B; anything else (default): by his draw.
enum class HideBy : uint8_t { Draw, Scale };

/// AzaharFrame kiu131: Reticle Size. The game's reticle (its ring and diamond, drawn on the GPU
/// with the game's own shaders while aiming) is the draws with this texture (texture 0's
/// physical address: VRAM, 64 x 64; KIU_HOOKS.md, "The reticle's size"). Those draws are shaded
/// on the CPU instead and scaled about their middle on the screen.
inline constexpr uint32_t kReticleTexture = 0x1810AF00;
static_assert(KiuTurn::kBuildEur.reticle_texture == kReticleTexture);
/// The scale: the switch's percent when set (30 to 150), else the setting's.
inline float ReticleScaleFor(const char* text, uint32_t setting_percent) {
    long percent = static_cast<long>(setting_percent);
    if (text && *text) {
        char* end = nullptr;
        const long v = std::strtol(text, &end, 10);
        if (end != text) {
            percent = v;
        }
    }
    return static_cast<float>(std::clamp(percent, 30L, 150L)) / 100.0f;
}
/// The reticle's texture: the switch's hex address when set, else the build's (kReticleTexture).
inline uint32_t ReticleTextureFor(const char* text, uint32_t build_texture = kReticleTexture) {
    if (text && *text) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(text, &end, 16);
        if (end != text) {
            return static_cast<uint32_t>(v);
        }
    }
    return build_texture;
}
inline HideBy HideByFrom(const char* text) {
    return text && std::string_view{text} == "scale" ? HideBy::Scale : HideBy::Draw;
}
/// First person hides his model by its draw on land and in the air, whatever the aiming (the
/// game's own shot start, his weapon's muzzle, does not move: his bones are untouched), with the
/// eye at his middle (not over the shoulder or moved by EYE_OFFSET: there he is to be seen).
inline bool HideByDrawInFirstPerson(bool first_person, bool eye_moved) {
    return first_person && !eye_moved;
}

/// AzaharFrame kiupit: the circle-shadow renderer's singleton (KIU_HOOKS.md, "The player's round
/// shadow") and, in it, the number of round shadows in this frame's list; room for 64.
inline constexpr uint32_t kRoundShadows = 0xD49490;
inline constexpr uint32_t kRoundShadowCount = 0x14;
inline constexpr uint32_t kRoundShadowMax = 64;
/// The round shadows in this frame's list (for the trace), or -1: no renderer, or a count the
/// list cannot hold.
inline int RoundShadowCount(const Reader& m) {
    uint32_t renderer = 0, count = 0;
    if (!m.Read32(kRoundShadows, renderer) || renderer == 0 || (renderer & 3u) != 0 ||
        !m.Read32(renderer + kRoundShadowCount, count) || count > kRoundShadowMax) {
        return -1;
    }
    return static_cast<int>(count);
}

// ---- The scene camera's angle and shape (AzaharFrame kiuwide) ----

/// What the app asks of the scene camera: its angle up and down (radians) and its shape (width
/// over height, the field the game's projection is built from).
struct WideRequest {
    bool on = false;
    float fov_y = 0.0f;
    float aspect = 0.0f;
};
/// Values the stub may be given: within what the projection's builder takes without a guard of
/// its own (it divides by tan(fov / 2) and by the shape).
inline bool WideValuesOk(float fov_y, float aspect) {
    return std::isfinite(fov_y) && std::isfinite(aspect) && fov_y >= 0.2f && fov_y <= 2.8f &&
           aspect >= 0.25f && aspect <= 8.0f;
}
/// A shape the game may have of its own (to keep and put back).
inline bool GameAspectOk(float aspect) {
    return std::isfinite(aspect) && aspect >= 0.25f && aspect <= 8.0f;
}

/// What the mailbox in the game's memory says was asked of the scene camera (it is in save
/// states too, with the code cave): the node, the shape it was given and the game's own.
struct WideRecord {
    uint32_t node = 0;
    uint32_t aspect_bits = 0;
    uint32_t game_aspect_bits = 0;
};
inline WideRecord ReadWideRecord(const Reader& m) {
    WideRecord r;
    uint32_t magic = 0;
    if (!m.Read32(kMailbox + Stubs::kMbMagic, magic) || magic != kMailboxMagic ||
        !m.Read32(kMailbox + Stubs::kMbWideNode, r.node) ||
        !m.Read32(kMailbox + Stubs::kMbWideAspect, r.aspect_bits) ||
        !m.Read32(kMailbox + Stubs::kMbWideGameAspect, r.game_aspect_bits)) {
        return {};
    }
    return r;
}
/// The shape in the scene camera is one the app gave it: the record's, or `last_asked_bits` (the
/// shape the app asked for last; the stub may not have run since the record changed).
inline bool WideAspectIsOurs(uint32_t node_aspect_bits, const WideRecord& rec,
                             uint32_t last_asked_bits = 0) {
    return rec.node == kSceneNode &&
           (node_aspect_bits == rec.aspect_bits ||
            (last_asked_bits != 0 && node_aspect_bits == last_asked_bits));
}
/// The game's own shape for the scene camera: the record's when the node holds a shape the app
/// gave it (it was widened, perhaps in a save state), else the node's now.
inline float WideGameAspect(uint32_t node_aspect_bits, const WideRecord& rec,
                            uint32_t last_asked_bits = 0) {
    const float game = std::bit_cast<float>(rec.game_aspect_bits);
    if (WideAspectIsOurs(node_aspect_bits, rec, last_asked_bits) && GameAspectOk(game)) {
        return game;
    }
    return std::bit_cast<float>(node_aspect_bits);
}
/// Not widening: what to write into the scene camera's shape, if anything. Only a shape the app
/// gave it goes back to the game's own as the record has it (as HideWrite: nothing else is
/// ever touched).
inline std::optional<uint32_t> WideRestore(uint32_t node_aspect_bits, const WideRecord& rec,
                                           uint32_t last_asked_bits = 0) {
    if (!WideAspectIsOurs(node_aspect_bits, rec, last_asked_bits) ||
        node_aspect_bits == rec.game_aspect_bits ||
        !GameAspectOk(std::bit_cast<float>(rec.game_aspect_bits))) {
        return std::nullopt;
    }
    return rec.game_aspect_bits;
}
/// The mailbox's wide fields for a request (`game_aspect`: WideGameAspect). Left at none when
/// the request is off or its values are not ones the stub may be given.
inline void FillWide(Mailbox& mb, const WideRequest& wide, float game_aspect) {
    mb.wide_node = 0;
    mb.wide_fov_y = mb.wide_aspect = mb.wide_game_aspect = 0.0f;
    if (!wide.on || !WideValuesOk(wide.fov_y, wide.aspect) || !GameAspectOk(game_aspect)) {
        return;
    }
    mb.wide_node = kSceneNode;
    mb.wide_fov_y = wide.fov_y;
    mb.wide_aspect = wide.aspect;
    mb.wide_game_aspect = game_aspect;
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

/// AzaharFrame kiu130: how far along the hand's ray, past its origin (the weapon in hand's
/// muzzle), first person's shots start, in game units (AZAHARFRAME_KIU_SHOT_AHEAD, live). The
/// game draws each new bullet where it starts, at the world's life size: started at the muzzle,
/// the rapid-fire bullet (a blade's is a silver arrowhead) sits on the weapon in hand's tip and
/// reads as a second weapon in front of it. Started this far out it is clear of the tip from its
/// first frame; it still flies along the ray. "0" starts them at the muzzle (as before), for an
/// A/B; a value past kShotAheadMax, or one that is not a number, gives the default.
inline constexpr float kShotAheadDefault = 6.0f;
inline constexpr float kShotAheadMax = 30.0f;
inline float ShotAheadFor(const char* text) {
    float v = 0.0f;
    if (!text || ParseFloats(text, &v, 1) != 1 || v < 0.0f || v > kShotAheadMax) {
        return kShotAheadDefault;
    }
    return v;
}

/// AzaharFrame kiuperf: a group that first person (Angel's Aim) puts in by itself, with its test
/// switch: "1" puts it in in any mode, "0" keeps it out even in first person (an A/B), anything
/// else (or no line) leaves the mode's choice `by_mode`.
inline bool GroupWanted(bool by_mode, const char* value) {
    if (value && value[0] == '1' && value[1] == '\0') {
        return true;
    }
    if (value && value[0] == '0' && value[1] == '\0') {
        return false;
    }
    return by_mode;
}

/// AzaharFrame kiuperf: which enemy keep goes in. The timed one (`seconds` > 0) and the plain one
/// share a site, so at most one of them.
struct EnemyKeepChoice {
    float seconds = 0.0f; ///< > 0: the timed keep (EnemyKeepTimed), that many seconds
    bool plain = false;   ///< the keep for good (EnemyKeep)
};
/// `mode_seconds`: the mode's own (Angel's Aim's 12 s, else 0). AZAHARFRAME_KIU_ENEMY_KEEP_S
/// (`timed`): seconds > 0 replace it, "0" takes the timed keep out. AZAHARFRAME_KIU_ENEMY_KEEP
/// (`plain`): "1" the keep for good when no timed one is in, "0" no enemy keep at all.
inline EnemyKeepChoice ChooseEnemyKeep(float mode_seconds, const char* plain, const char* timed) {
    EnemyKeepChoice c;
    c.seconds = mode_seconds > 0.0f ? mode_seconds : 0.0f;
    float v = 0.0f;
    if (timed && ParseFloats(timed, &v, 1) == 1) {
        if (v > 0.0f) {
            c.seconds = v;
        } else if (v == 0.0f) {
            c.seconds = 0.0f;
        }
    }
    if (plain && plain[0] == '0' && plain[1] == '\0') {
        return EnemyKeepChoice{};
    }
    c.plain = !(c.seconds > 0.0f) && plain && plain[0] == '1' && plain[1] == '\0';
    return c;
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

inline Cave LookAtCave(const Reader& m, const Code& code = kCodeEur) {
    const auto& image = *code.image;
    bool ours = true;
    for (std::size_t i = 0; i < image.size(); ++i) {
        uint32_t v = 0;
        if (!m.Read32(kImageBase + static_cast<uint32_t>(i) * 4u, v)) {
            return Cave::Unreadable;
        }
        if (v != image[i]) {
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

/// True when `word`, placed at `addr`, is a b or bl into the image's area (from the first image
/// word up to the mailbox): the word of a patch of an older image version.
constexpr bool BranchesIntoImage(uint32_t addr, uint32_t word) {
    if ((word & 0x0E000000u) != 0x0A000000u) {
        return false;
    }
    const uint32_t imm = word & 0x00FFFFFFu;
    const uint32_t offset = (imm << 2) | ((imm & 0x00800000u) != 0 ? 0xFC000000u : 0u);
    const uint32_t target = addr + 8u + offset;
    return target >= kImageBase && target < kMailbox;
}

/// One VBlank's pass: reads every site, puts the wanted groups in (the image first, then the
/// mailbox, the site words last) and the others out. `wanted` is a mask of Bit(group). Writes
/// nothing when everything already is as wanted (apart from the mailbox while a stub group is
/// in). The caller has checked the title and the game's code (GameCodeMatches).
inline SyncResult Sync(GameMemory& m, uint32_t wanted, const Mailbox& mailbox,
                       const Code& code = kCodeEur) {
    const auto& patches = *code.patches;
    SyncResult r;
    enum : uint8_t { kGame, kOurs, kSibling, kOther, kUnmapped };
    // What a site holds for patch `p`: the game's word, this patch's, the word of another patch
    // of ours at the same address (two groups may share a site; one of them is wanted at most),
    // or something else.
    const auto look = [&m, &patches](const Patch& p, uint32_t& v) -> uint8_t {
        if (!m.Read32(p.addr, v)) {
            return kUnmapped;
        }
        if (v == p.original) {
            return kGame;
        }
        if (v == p.patched) {
            return kOurs;
        }
        for (const Patch& q : patches) {
            if (q.addr == p.addr && q.patched == v) {
                return kSibling;
            }
        }
        return kOther;
    };
    // A site that jumps into the image but is none of our words is a patch of an older image
    // version (a state made by another build): its stub entry may have moved, so before the
    // image is rewritten it goes back to the game's word and goes in again as this build's.
    for (const Patch& p : patches) {
        uint32_t v = 0;
        if (look(p, v) == kOther && BranchesIntoImage(p.addr, v)) {
            r.code_writes += m.WriteCode(p.addr, p.original) ? 1 : 0;
        }
    }
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
    for (const Patch& p : patches) {
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
    for (const Context& c : *code.context) {
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
        r.cave = LookAtCave(m, code);
        if (r.cave == Cave::Empty || r.cave == Cave::Older) {
            bool wrote = true;
            const auto& image = *code.image;
            for (std::size_t i = 0; i < image.size() && wrote; ++i) {
                wrote = m.WriteCode(kImageBase + static_cast<uint32_t>(i) * 4u, image[i]);
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
        for (const Patch& p : patches) {
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
        for (const Patch& p : patches) {
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
inline bool AnyOurs(const Reader& m, const Code& code = kCodeEur) {
    for (const Patch& p : *code.patches) {
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
