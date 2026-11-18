@ check.sh's test bench for stubs.s, run under qemu-arm: the callers set every register to a known
@ value and enter a stub the way the game does; the stand-ins for the game's functions record what
@ arrives. harness.c compares.
        .syntax unified
        .arch   armv6k
        .fpu    vfpv2
        .arm
        .text
        .global call_shot, call_eye, call_keep, call_shadow, call_fov, SPAWN, SINK_CONTINUE, PATH_RESTART, PATH_VANISH, SHADOW_CONTINUE, FOV_CONTINUE

@ struct Regs { u32 r[13]; u32 sp; u32 lr; u32 stack[4]; float s[16]; }   (r at 0, sp 52, lr 56, stack 60, s 76)
        .macro  capture reg
        str     r0, [\reg, #0]
        str     r1, [\reg, #4]
        str     r2, [\reg, #8]
        str     r3, [\reg, #12]
        str     r4, [\reg, #16]
        str     r5, [\reg, #20]
        str     r6, [\reg, #24]
        str     r7, [\reg, #28]
        str     r8, [\reg, #32]
        str     r9, [\reg, #36]
        str     r10, [\reg, #40]
        str     r11, [\reg, #44]
        .endm

@ void call_shot(const struct Regs* in): r0-r11 and s0-s15 from `in`, in->stack[0..3] at
@ [sp+0..+12], shot_base at [sp+0x70..+0x7C], then `bl shot_stub`.
call_shot:
        push    {r4-r11, lr}
        vpush   {d8-d15}
        sub     sp, sp, #0x80           @ the caller's frame: the four stack arguments, and at
        mov     r12, r0                 @ +0x70 its aim quaternion before scatter (shot_base)
        ldr     r0, =shot_base
        ldr     r1, [r0, #0]
        str     r1, [sp, #0x70]
        ldr     r1, [r0, #4]
        str     r1, [sp, #0x74]
        ldr     r1, [r0, #8]
        str     r1, [sp, #0x78]
        ldr     r1, [r0, #12]
        str     r1, [sp, #0x7C]
        ldr     r0, [r12, #60]
        str     r0, [sp, #0]
        ldr     r0, [r12, #64]
        str     r0, [sp, #4]
        ldr     r0, [r12, #68]
        str     r0, [sp, #8]
        ldr     r0, [r12, #72]
        str     r0, [sp, #12]
        add     r0, r12, #76
        vldmia  r0, {s0-s15}
        ldr     r0, =shot_sp
        str     sp, [r0]
        ldm     r12, {r0-r11}
        bl      shot_stub
shot_return:
        add     sp, sp, #0x80
        vpop    {d8-d15}
        pop     {r4-r11, pc}

@ The bullet spawn's stand-in: records everything the stub passed on, returns.
SPAWN:
        ldr     r12, =spawn_seen
        capture r12
        str     sp, [r12, #52]
        str     lr, [r12, #56]
        add     r12, r12, #60
        ldr     r0, [sp, #0]
        str     r0, [r12, #0]
        ldr     r0, [sp, #4]
        str     r0, [r12, #4]
        ldr     r0, [sp, #8]
        str     r0, [r12, #8]
        ldr     r0, [sp, #12]
        str     r0, [r12, #12]
        add     r12, r12, #16
        vstmia  r12, {s0-s15}
        ldr     r12, =spawn_calls
        ldr     r0, [r12]
        add     r0, r0, #1
        str     r0, [r12]
        bx      lr

@ void call_eye(const struct Regs* in): r0-r11 from `in`, then `bl` to the patched site's stand-in,
@ which is the stub itself (the site holds `b eye_stub`).
call_eye:
        push    {r4-r11, lr}
        mov     r12, r0
        ldr     r0, =eye_sp
        str     sp, [r0]
        ldm     r12, {r0-r11}
        bl      eye_stub
        pop     {r4-r11, pc}

@ The rest of the camera hand-over (0x6340A0 on): records, then returns as the function would
@ (the stub ran its first instruction, push {r4, r5, r6, lr}).
SINK_CONTINUE:
        ldr     r12, =sink_seen
        str     r0, [r12, #0]
        str     r2, [r12, #8]
        str     r3, [r12, #12]
        str     r6, [r12, #24]
        str     r7, [r12, #28]
        str     r8, [r12, #32]
        str     r9, [r12, #36]
        str     r10, [r12, #40]
        str     r11, [r12, #44]
        str     sp, [r12, #52]
        ldr     r0, [sp, #0]            @ the pushed r4, r5, r6, lr
        str     r0, [r12, #60]
        ldr     r0, [sp, #4]
        str     r0, [r12, #64]
        ldr     r0, [sp, #8]
        str     r0, [r12, #68]
        ldr     r0, [sp, #12]
        str     r0, [r12, #72]
        ldr     r12, =sink_calls
        ldr     r0, [r12]
        add     r0, r0, #1
        str     r0, [r12]
        pop     {r4, r5, r6, pc}

@ u32 call_keep(const struct Regs* in): r0-r11 from `in`, then `bl keep_stub` (the game enters it
@ by a branch; the stand-ins return here). Returns 1 when the restart code was reached, 2 for
@ the vanish code; keep_seen gets r0-r11 as they arrived there.
call_keep:
        push    {r4-r11, lr}
        mov     r12, r0
        ldm     r12, {r0-r11}
        bl      keep_stub
        pop     {r4-r11, pc}
PATH_RESTART:
        ldr     r12, =keep_seen
        capture r12
        mov     r0, #1
        bx      lr
PATH_VANISH:
        ldr     r12, =keep_seen
        capture r12
        mov     r0, #2
        bx      lr

@ void call_shadow(const struct Regs* in): r0-r11 from `in`, then `bl` to the patched site's
@ stand-in, the stub itself (the site holds `b shadow_stub`); shadow_back gets r0-r11 and sp as
@ they come back to the caller (either way).
call_shadow:
        push    {r4-r11, lr}
        mov     r12, r0
        ldr     r0, =shadow_sp
        str     sp, [r0]
        ldm     r12, {r0-r11}
        bl      shadow_stub
        ldr     r12, =shadow_back
        capture r12
        str     sp, [r12, #52]
        pop     {r4-r11, pc}

@ The rest of the round shadow (0x57B058 on): records, then returns as the function would (the
@ stub ran its first instruction, push {r4, r5, lr}), r0-r3 as they arrived.
SHADOW_CONTINUE:
        push    {r0}
        ldr     r0, =shadow_seen
        str     r1, [r0, #4]
        str     r2, [r0, #8]
        str     r3, [r0, #12]
        str     r4, [r0, #16]
        str     r5, [r0, #20]
        str     r6, [r0, #24]
        str     r7, [r0, #28]
        str     r8, [r0, #32]
        str     r9, [r0, #36]
        str     r10, [r0, #40]
        str     r11, [r0, #44]
        add     r1, sp, #4
        str     r1, [r0, #52]
        ldr     r1, [sp, #4]            @ the pushed r4, r5, lr
        str     r1, [r0, #60]
        ldr     r1, [sp, #8]
        str     r1, [r0, #64]
        ldr     r1, [sp, #12]
        str     r1, [r0, #68]
        pop     {r1}
        str     r1, [r0, #0]
        ldr     r1, =shadow_calls
        ldr     r2, [r1]
        add     r2, r2, #1
        str     r2, [r1]
        ldr     r1, [r0, #4]
        ldr     r2, [r0, #8]
        ldr     r0, [r0, #0]
        pop     {r4, r5, pc}

@ AzaharFrame kiuwide. void call_fov(const struct Regs* in): r0-r11 and s0-s15 from `in`, then
@ `bl` to the patched site's stand-in, the stub itself (the site holds `b fov_stub`); fov_back
@ gets r0-r11, sp and s0-s15 as they come back to the caller (either way).
call_fov:
        push    {r4-r11, lr}
        mov     r12, r0
        ldr     r0, =fov_sp
        str     sp, [r0]
        add     r0, r12, #76
        vldmia  r0, {s0-s15}
        ldm     r12, {r0-r11}
        bl      fov_stub
        ldr     r12, =fov_back
        capture r12
        str     sp, [r12, #52]
        add     r0, r12, #76
        vstmia  r0, {s0-s15}
        pop     {r4-r11, pc}

@ The rest of the angle setter (0x45E050 on): records r0-r11, sp and s0-s15 (the stub ran its
@ first instruction, vldr s1, [r0, #0x3C]), then returns to the caller as the setter would.
FOV_CONTINUE:
        push    {r0}
        ldr     r0, =fov_seen
        str     r1, [r0, #4]
        str     r2, [r0, #8]
        str     r3, [r0, #12]
        str     r4, [r0, #16]
        str     r5, [r0, #20]
        str     r6, [r0, #24]
        str     r7, [r0, #28]
        str     r8, [r0, #32]
        str     r9, [r0, #36]
        str     r10, [r0, #40]
        str     r11, [r0, #44]
        add     r1, sp, #4
        str     r1, [r0, #52]
        add     r1, r0, #76
        vstmia  r1, {s0-s15}
        pop     {r1}
        str     r1, [r0, #0]
        ldr     r1, =fov_calls
        ldr     r2, [r1]
        add     r2, r2, #1
        str     r2, [r1]
        ldr     r1, [r0, #4]
        ldr     r2, [r0, #8]
        ldr     r0, [r0, #0]
        bx      lr

        .global shot_return
        .ltorg
