@ Kid Icarus: Uprising (EUR, title 0004000000030200, original code): the stubs the app puts in
@ the game's code cave (the unused tail of the text segment, 0xC95370-0xC96000) and reaches by
@ patching one instruction each (src/frame_xr/input/kiu_hooks.h; KIU_HOOKS.md beside it).
@
@ This file is the source of src/frame_xr/input/kiu_hooks_stubs.h (gen.sh writes it, check.sh
@ compares them and runs the stubs under qemu-arm). ARM state, ARMv6K + VFPv2, no relocations:
@ every address outside the image is a literal, so the words are the same wherever the image is.
@
@ The stubs read the mailbox (kiu_hooks.h, Mailbox) through a register, never pc-relative, so no
@ CPU core can take its values for constants. They never write the cave: what they change goes
@ through the pointers the game passed. They keep every register the game may still need.

        .syntax unified
        .arch   armv6k
        .fpu    vfpv2
        .arm
        .text

@ ---- The mailbox: offsets from its base (the app writes it at each VBlank) ----
        .equ    MB_MAGIC,          0x00    @ u32 'KIUM'
        .equ    MB_SERIAL,         0x04    @ u32 VBlanks written (for a log)
        .equ    MB_PLAYER,         0x08    @ u32 the local player's actor, 0: none
        .equ    MB_EYE_NODE,       0x0C    @ u32 the camera node handed to the sink for the player's view
        .equ    MB_EYE_FLAGS,      0x10    @ u32 bit 0: replace the eye
        .equ    MB_EYE_OFFSET,     0x14    @ f32 x3, world axes: eye = the player's world position + this
        .equ    MB_SHOT_FLAGS,     0x20    @ u32 bit 0: rotate, bit 1: move the start, bit 2: no lock target
        .equ    MB_SHOT_ROTATION,  0x24    @ f32 x4 quaternion x y z w: q' = rotation * q
        .equ    MB_SHOT_FRAME,     0x34    @ f32 x4 quaternion: the player's parent's rotation (identity on land)
        .equ    MB_SHOT_OFFSET,    0x44    @ f32 x3: start = frame * the player's local position + this
        .equ    MB_KEEP_SECONDS,   0x50    @ f32: a passed flier younger than this flies its path again
        .equ    MB_SIZE,           0x54

        .equ    EYE_ON,            1
        .equ    SHOT_ROTATE,       1
        .equ    SHOT_MOVE,         2
        .equ    SHOT_NO_TARGET,    4

@ ---- The game ----
        .equ    ACTOR_WORLD_POS,   0x54    @ f32 x3
        .equ    ACTOR_LOCAL_POS,   0x74    @ f32 x3, in the parent's frame
        .equ    TRANSFORM_POS,     0x14    @ the camera hand-over's transform: f32 x3
        .equ    PATH_STATE_TIME,   0x34    @ a flier's path state: f32 seconds since it entered it

        .global image_start, eye_stub, shot_stub, keep_stub, image_end
image_start:
        .ascii  "KIUH"                  @ the image's tag
        .word   IMAGE_VERSION

@ ---- The eye: entered by `b eye_stub` at 0x63409C, the first instruction of the function that
@ hands a camera's transform to the scene (r0 = camera node, r2 = transform: quaternion at +4,
@ position at +0x14; once per game frame, land and air). Replaces the position by the player's
@ world position plus the mailbox offset when the eye is on, the node is the player's and the
@ actor is still an actor. The orientation stays the game's.
@ Free here: r1, r12 and s0-s6 (the function writes them before it reads them), r4 and r5 after
@ the push (saved by it; written at 0x6340A4 and 0x6340BC before any read), the flags.
eye_stub:
        push    {r4, r5, r6, lr}        @ the instruction replaced at 0x63409C
        ldr     r12, lit_mailbox
        ldr     r1, [r12, #MB_EYE_FLAGS]
        tst     r1, #EYE_ON
        beq     1f
        ldr     r1, [r12, #MB_EYE_NODE]
        cmp     r1, r0
        bne     1f
        ldr     r1, [r12, #MB_PLAYER]
        cmp     r1, #0
        beq     1f
        ldr     r4, [r1]
        ldr     r5, lit_actor_vtable
        cmp     r4, r5
        bne     1f
        add     r1, r1, #ACTOR_WORLD_POS
        vldmia  r1, {s0-s2}             @ the player's world position, as of this game frame
        add     r1, r12, #MB_EYE_OFFSET
        vldmia  r1, {s3-s5}
        vadd.f32 s0, s0, s3
        vadd.f32 s1, s1, s4
        vadd.f32 s2, s2, s5
        add     r1, r2, #TRANSFORM_POS
        vstmia  r1, {s0-s2}
1:      ldr     pc, lit_sink_continue   @ 0x6340A0, the function's second instruction

@ ---- The shot: entered by `bl shot_stub` at 0x5043E4 in place of `bl 0x485638` (the bullet
@ spawn): r0 = result, r1 = actor manager, r2 = the owner's actor, r3 = 0x20, s0 = a float
@ argument, [sp+4] = quaternion* (x y z w; forward = it applied to +Z), [sp+8] = position*
@ (3 floats), [sp+0xC] = the lock target's handle, lr = 0x5043E8. For the local player's shots
@ only: turns the quaternion by the mailbox rotation (each bullet's own, so scatter and fans
@ stay), replaces the position by the player's own position in the spawn's space plus the mailbox
@ offset, optionally clears the target; then goes on to 0x485638 with every register, sp and lr
@ as the game set them. Free here: r12 and the flags only; everything else is saved.
        .equ    SHOT_SAVED,        72      @ push {r4, r5} + vpush {d0-d7}
shot_stub:
        ldr     r12, lit_mailbox
        push    {r4, r5}
        ldr     r4, [r12, #MB_SHOT_FLAGS]
        cmp     r4, #0
        beq     9f
        ldr     r5, [r12, #MB_PLAYER]
        cmp     r5, r2
        bne     9f
        vpush   {d0-d7}
        tst     r4, #SHOT_ROTATE
        beq     2f
        @ q' = a * b, a = the mailbox rotation (s0-s3 = x y z w), b = the game's (s4-s7)
        add     r5, r12, #MB_SHOT_ROTATION
        vldmia  r5, {s0-s3}
        ldr     r5, [sp, #SHOT_SAVED + 4]
        vldmia  r5, {s4-s7}
        vmul.f32 s8, s3, s4             @ x = aw bx + ax bw + ay bz - az by
        vmla.f32 s8, s0, s7
        vmla.f32 s8, s1, s6
        vmls.f32 s8, s2, s5
        vmul.f32 s9, s3, s5             @ y = aw by - ax bz + ay bw + az bx
        vmls.f32 s9, s0, s6
        vmla.f32 s9, s1, s7
        vmla.f32 s9, s2, s4
        vmul.f32 s10, s3, s6            @ z = aw bz + ax by - ay bx + az bw
        vmla.f32 s10, s0, s5
        vmls.f32 s10, s1, s4
        vmla.f32 s10, s2, s7
        vmul.f32 s11, s3, s7            @ w = aw bw - ax bx - ay by - az bz
        vmls.f32 s11, s0, s4
        vmls.f32 s11, s1, s5
        vmls.f32 s11, s2, s6
        vstmia  r5, {s8-s11}
2:      tst     r4, #SHOT_MOVE
        beq     3f
        @ p = the frame (u = s0-s2, w = s3) applied to the player's local position (v = s4-s6),
        @ plus the offset: t = 2 (u x v); v' = v + w t + u x t
        add     r5, r12, #MB_SHOT_FRAME
        vldmia  r5, {s0-s3}
        add     r5, r2, #ACTOR_LOCAL_POS
        vldmia  r5, {s4-s6}
        vmul.f32 s7, s1, s6             @ tx = uy vz - uz vy
        vmls.f32 s7, s2, s5
        vmul.f32 s8, s2, s4             @ ty = uz vx - ux vz
        vmls.f32 s8, s0, s6
        vmul.f32 s9, s0, s5             @ tz = ux vy - uy vx
        vmls.f32 s9, s1, s4
        vadd.f32 s7, s7, s7
        vadd.f32 s8, s8, s8
        vadd.f32 s9, s9, s9
        vmla.f32 s4, s3, s7             @ v += w t
        vmla.f32 s5, s3, s8
        vmla.f32 s6, s3, s9
        vmla.f32 s4, s1, s9             @ v += u x t
        vmls.f32 s4, s2, s8
        vmla.f32 s5, s2, s7
        vmls.f32 s5, s0, s9
        vmla.f32 s6, s0, s8
        vmls.f32 s6, s1, s7
        add     r5, r12, #MB_SHOT_OFFSET
        vldmia  r5, {s0-s2}
        vadd.f32 s4, s4, s0
        vadd.f32 s5, s5, s1
        vadd.f32 s6, s6, s2
        ldr     r5, [sp, #SHOT_SAVED + 8]
        vstmia  r5, {s4-s6}
3:      tst     r4, #SHOT_NO_TARGET
        ldrne   r5, lit_no_target
        strne   r5, [sp, #SHOT_SAVED + 12]
        vpop    {d0-d7}
9:      pop     {r4, r5}
        ldr     pc, lit_spawn           @ 0x485638

@ ---- A passed flier: entered by `beq keep_stub` at 0x3E8C9C in place of `beq 0x3E8D00`, inside
@ the function that ends a flying enemy's scripted path (0x3E8C34), where the enemy's placement
@ has no "fly it again" flag and the game goes on to make it vanish. r4 = the path state, the
@ function's frame is pushed, r6 = 1. Younger than the mailbox's limit (seconds since the enemy
@ entered its path state; the game keeps counting across restarts): the function's own restart
@ code (0x3E8CDC); else its vanish code (0x3E8D00), as the game. Uses r12, s0, s1 and the flags
@ (both targets write s0 or call a function before reading any of them); r0-r3 are kept.
keep_stub:
        ldr     r12, lit_mailbox
        vldr    s0, [r4, #PATH_STATE_TIME]
        vldr    s1, [r12, #MB_KEEP_SECONDS]
        vcmpe.f32 s0, s1
        vmrs    APSR_nzcv, fpscr
        ldrmi   pc, lit_path_restart    @ less than the limit (not for a NaN): 0x3E8CDC
        ldr     pc, lit_path_vanish     @ 0x3E8D00

@ ---- Literals (the only words that name an address) ----
lit_mailbox:        .word   MAILBOX
lit_actor_vtable:   .word   ACTOR_VTABLE
lit_sink_continue:  .word   SINK_CONTINUE
lit_spawn:          .word   SPAWN
lit_no_target:      .word   0xFFFFFE00
lit_path_restart:   .word   PATH_RESTART
lit_path_vanish:    .word   PATH_VANISH
image_end:
