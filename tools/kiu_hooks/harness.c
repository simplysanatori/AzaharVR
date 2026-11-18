// check.sh's test bench for stubs.s (freestanding, run under qemu-arm): enters each stub the way
// the game does and compares what reaches the game's functions with the same maths in C.
typedef unsigned int u32;

struct Regs {
    u32 r[13];
    u32 sp;
    u32 lr;
    u32 stack[4];
    float s[16];
};

extern void call_shot(const struct Regs* in);
extern void call_eye(const struct Regs* in);
extern u32 call_keep(const struct Regs* in);
extern char shot_return[];

// What the stubs' literals name (stubs.s without stubs-eur.inc).
u32 MAILBOX[0x54 / 4];
char ACTOR_VTABLE[4];

struct Regs spawn_seen, sink_seen, keep_seen;
float shot_base[4]; // the caller's aim quaternion before scatter, put at its [sp + 0x70]
u32 spawn_calls, sink_calls, shot_sp, eye_sp;

enum { MB_PLAYER = 0x08, MB_EYE_NODE = 0x0C, MB_EYE_FLAGS = 0x10, MB_EYE_OFFSET = 0x14,
       MB_SHOT_FLAGS = 0x20, MB_SHOT_ROTATION = 0x24, MB_SHOT_FRAME = 0x34, MB_SHOT_OFFSET = 0x44,
       MB_KEEP_SECONDS = 0x50 };

static long sys(long n, long a, long b, long c) {
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}
static void say(const char* s) {
    u32 n = 0;
    while (s[n]) n++;
    sys(4, 1, (long)s, n);
}
static int failures;
static void check(int ok, const char* what) {
    if (!ok) {
        failures++;
        say("FAIL: ");
        say(what);
        say("\n");
    }
}
static float absf(float v) { return v < 0 ? -v : v; }
static int near(float a, float b) { return absf(a - b) <= 1e-4f * (1.0f + absf(b)); }
static void put_f(u32 off, const float* v, int n) {
    for (int i = 0; i < n; i++) ((float*)MAILBOX)[off / 4 + i] = v[i];
}

static void qmul(const float* a, const float* b, float* o) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void qrot(const float* q, const float* v, float* o) {
    // q v q*, through the product (not the stub's shortcut)
    float vq[4] = {v[0], v[1], v[2], 0}, c[4] = {-q[0], -q[1], -q[2], q[3]}, t[4], r[4];
    qmul(q, vq, t);
    qmul(t, c, r);
    o[0] = r[0], o[1] = r[1], o[2] = r[2];
}

static u32 actor[0x200 / 4];
static u32 other_actor[0x200 / 4];

static void fill(struct Regs* in) {
    for (int i = 0; i < 13; i++) in->r[i] = 0x1000u * (u32)(i + 1) + 0x11u;
    for (int i = 0; i < 16; i++) in->s[i] = 100.0f + (float)i;
}
static int same_regs(const struct Regs* in, const struct Regs* seen, int from, int to) {
    int ok = 1;
    for (int i = from; i <= to; i++) ok = ok && in->r[i] == seen->r[i];
    return ok;
}
static int same_floats(const struct Regs* in, const struct Regs* seen) {
    int ok = 1;
    for (int i = 0; i < 16; i++) ok = ok && in->s[i] == seen->s[i];
    return ok;
}

static void shot_case(u32 flags, int owner_is_player, const char* name) {
    // A unit rotation (about a skewed axis), a frame (another), the game's quaternion (a third).
    const float rot[4] = {0.1825742f, 0.3651484f, 0.5477226f, 0.7302967f};
    const float frame[4] = {-0.2672612f, 0.5345225f, 0.0f, 0.8017837f};
    const float offset[3] = {1.5f, -2.25f, 3.0f};
    const float base[4] = {0.4082483f, -0.4082483f, 0.0f, 0.8164966f};
    for (int i = 0; i < 4; i++) shot_base[i] = base[i];
    float quat[4] = {0.5f, -0.5f, 0.5f, 0.5f};
    float pos[3] = {7.0f, 8.0f, 9.0f};
    const float quat0[4] = {quat[0], quat[1], quat[2], quat[3]};
    const float pos0[3] = {pos[0], pos[1], pos[2]};
    float* local = (float*)&actor[0x74 / 4];
    local[0] = 10.0f, local[1] = -4.0f, local[2] = 2.5f;
    for (int i = 0; i < 0x54 / 4; i++) MAILBOX[i] = 0;
    MAILBOX[MB_PLAYER / 4] = (u32)actor;
    MAILBOX[MB_SHOT_FLAGS / 4] = flags;
    put_f(MB_SHOT_ROTATION, rot, 4);
    put_f(MB_SHOT_FRAME, frame, 4);
    put_f(MB_SHOT_OFFSET, offset, 3);

    struct Regs in;
    fill(&in);
    in.r[2] = owner_is_player ? (u32)actor : (u32)other_actor;
    in.stack[0] = 0xAAAA0001u;
    in.stack[1] = (u32)quat;
    in.stack[2] = (u32)pos;
    in.stack[3] = 0x21F50000u;
    const u32 calls = spawn_calls;
    call_shot(&in);
    say(name);
    say("\n");
    check(spawn_calls == calls + 1, "the spawn is reached once");
    check(same_regs(&in, &spawn_seen, 0, 11), "r0-r11 as the game set them");
    check(same_floats(&in, &spawn_seen), "s0-s15 as the game set them");
    check(spawn_seen.sp == shot_sp, "sp as at the call");
    check(spawn_seen.lr == (u32)shot_return, "lr is the call's return address");
    check(spawn_seen.stack[0] == in.stack[0] && spawn_seen.stack[1] == (u32)quat &&
              spawn_seen.stack[2] == (u32)pos,
          "the stack arguments' pointers");
    const int active = owner_is_player && flags != 0;
    float want_q[4] = {quat0[0], quat0[1], quat0[2], quat0[3]};
    float want_p[3] = {pos0[0], pos0[1], pos0[2]};
    u32 want_target = in.stack[3];
    if (active && (flags & 1)) {
        if (flags & 8) { // the rotation is the aim itself: rot * conj(base) * q
            const float cb[4] = {-base[0], -base[1], -base[2], base[3]};
            float t[4];
            qmul(cb, quat0, t);
            qmul(rot, t, want_q);
        } else {
            qmul(rot, quat0, want_q);
        }
    }
    check(shot_base[0] == base[0] && shot_base[3] == base[3], "the caller's aim untouched");
    if (active && (flags & 2)) {
        qrot(frame, local, want_p);
        for (int i = 0; i < 3; i++) want_p[i] += offset[i];
    }
    if (active && (flags & 4)) want_target = 0xFFFFFE00u;
    check(near(quat[0], want_q[0]) && near(quat[1], want_q[1]) && near(quat[2], want_q[2]) &&
              near(quat[3], want_q[3]),
          "the quaternion");
    check(near(pos[0], want_p[0]) && near(pos[1], want_p[1]) && near(pos[2], want_p[2]),
          "the position");
    if (!(active && (flags & 1))) check(quat[0] == quat0[0] && quat[3] == quat0[3], "the quaternion untouched (exact)");
    if (!(active && (flags & 2))) check(pos[0] == pos0[0] && pos[2] == pos0[2], "the position untouched (exact)");
    check(spawn_seen.stack[3] == want_target, "the lock target");
}

static void eye_case(u32 flags, int node_matches, int actor_ok, int has_player, const char* name) {
    const float offset[3] = {0.5f, 12.3f, -1.0f};
    float transform[8] = {1.0f, 0.1f, 0.2f, 0.3f, 0.9f, 40.0f, 50.0f, 60.0f};
    float* world = (float*)&actor[0x54 / 4];
    world[0] = -47.3f, world[1] = 148.6f, world[2] = -1441.4f;
    actor[0] = actor_ok ? (u32)ACTOR_VTABLE : 0x12345678u;
    for (int i = 0; i < 0x54 / 4; i++) MAILBOX[i] = 0;
    MAILBOX[MB_PLAYER / 4] = has_player ? (u32)actor : 0;
    MAILBOX[MB_EYE_NODE / 4] = 0x00777000u;
    MAILBOX[MB_EYE_FLAGS / 4] = flags;
    put_f(MB_EYE_OFFSET, offset, 3);
    struct Regs in;
    fill(&in);
    in.r[0] = node_matches ? 0x00777000u : 0x00888000u;
    in.r[2] = (u32)transform;
    const u32 calls = sink_calls;
    call_eye(&in);
    say(name);
    say("\n");
    check(sink_calls == calls + 1, "the hand-over goes on once");
    check(sink_seen.r[0] == in.r[0] && sink_seen.r[2] == in.r[2] && sink_seen.r[3] == in.r[3],
          "r0, r2, r3 as the caller set them");
    check(same_regs(&in, &sink_seen, 6, 11), "r6-r11 as the caller set them");
    check(sink_seen.sp == eye_sp - 16, "sp after the function's own push");
    check(sink_seen.stack[0] == in.r[4] && sink_seen.stack[1] == in.r[5] &&
              sink_seen.stack[2] == in.r[6],
          "the pushed r4, r5, r6 are the caller's");
    const int active = (flags & 1) && node_matches && actor_ok && has_player;
    for (int i = 0; i < 5; i++) {
        static const float t0[5] = {1.0f, 0.1f, 0.2f, 0.3f, 0.9f};
        check(transform[i] == t0[i], "the scale and quaternion untouched");
    }
    if (active) {
        check(near(transform[5], world[0] + offset[0]) && near(transform[6], world[1] + offset[1]) &&
                  near(transform[7], world[2] + offset[2]),
              "the eye = the player's world position + the offset");
    } else {
        check(transform[5] == 40.0f && transform[6] == 50.0f && transform[7] == 60.0f,
              "the position untouched");
    }
    actor[0] = (u32)ACTOR_VTABLE;
}

static void keep_case(float age, float limit, u32 want, const char* name) {
    static u32 state[0x40 / 4];
    ((float*)state)[0x34 / 4] = age;
    for (int i = 0; i < 0x54 / 4; i++) MAILBOX[i] = 0;
    put_f(MB_KEEP_SECONDS, &limit, 1);
    struct Regs in;
    fill(&in);
    in.r[4] = (u32)state;
    const u32 where = call_keep(&in);
    say(name);
    say("\n");
    check(where == want, "the right code is reached (1 restart, 2 vanish)");
    check(same_regs(&in, &keep_seen, 0, 11), "r0-r11 as the function had them");
}

void _start(void) {
    actor[0] = (u32)ACTOR_VTABLE;
    other_actor[0] = (u32)ACTOR_VTABLE;
    shot_case(0, 1, "shot: nothing armed");
    shot_case(7, 0, "shot: another owner's shot");
    shot_case(1, 1, "shot: rotate");
    shot_case(2, 1, "shot: move");
    shot_case(4, 1, "shot: no lock target");
    shot_case(3, 1, "shot: rotate and move");
    shot_case(7, 1, "shot: rotate, move, no lock target");
    shot_case(9, 1, "shot: the aim replaced");
    shot_case(8, 1, "shot: the aim flag without rotate");
    shot_case(15, 1, "shot: the aim replaced, move, no lock target");
    shot_case(15, 0, "shot: another owner's shot, everything armed");
    eye_case(0, 1, 1, 1, "eye: off");
    eye_case(1, 0, 1, 1, "eye: another camera node");
    eye_case(1, 1, 0, 1, "eye: the actor is gone");
    eye_case(1, 1, 1, 0, "eye: no player");
    eye_case(1, 1, 1, 1, "eye: on");
    keep_case(3.5f, 20.0f, 1, "keep: younger than the limit: restart");
    keep_case(20.0f, 20.0f, 2, "keep: at the limit: vanish");
    keep_case(31.0f, 20.0f, 2, "keep: older: vanish");
    keep_case(3.5f, 0.0f, 2, "keep: a zero limit: vanish (the game's own)");
    keep_case(0.0f / 0.0f * 0.0f + (float)__builtin_nanf(""), 20.0f, 2, "keep: a NaN age: vanish");
    say(failures ? "stubs: FAILED\n" : "stubs: all checks passed\n");
    sys(1, failures ? 1 : 0, 0, 0);
    for (;;) {
    }
}
