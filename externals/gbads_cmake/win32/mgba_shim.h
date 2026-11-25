/* AzaharVR: forced into mGBA's files on Windows, and part of win32_posix.h. mGBA's common.h defines
 * snprintf as _snprintf for MSVC, which neither ends a cut string nor returns the length it needed;
 * the C runtime's own has been the standard's for years. */
#pragma once
#include <mgba-util/common.h>
#undef snprintf

/* For MSVC its atomics are Interlocked*, which take four bytes of whatever they are given: bools and
 * enums too. Clang has the ones it uses everywhere else. */
#undef ATOMIC_STORE
#undef ATOMIC_LOAD
#undef ATOMIC_ADD
#undef ATOMIC_SUB
#undef ATOMIC_OR
#undef ATOMIC_AND
#undef ATOMIC_CMPXCHG
#undef ATOMIC_STORE_PTR
#undef ATOMIC_LOAD_PTR
#define ATOMIC_STORE(DST, SRC) __atomic_store_n(&DST, SRC, __ATOMIC_RELEASE)
#define ATOMIC_LOAD(DST, SRC) DST = __atomic_load_n(&SRC, __ATOMIC_ACQUIRE)
#define ATOMIC_ADD(DST, OP) __atomic_add_fetch(&DST, OP, __ATOMIC_RELEASE)
#define ATOMIC_SUB(DST, OP) __atomic_sub_fetch(&DST, OP, __ATOMIC_RELEASE)
#define ATOMIC_OR(DST, OP) __atomic_or_fetch(&DST, OP, __ATOMIC_RELEASE)
#define ATOMIC_AND(DST, OP) __atomic_and_fetch(&DST, OP, __ATOMIC_RELEASE)
#define ATOMIC_CMPXCHG(DST, EXPECTED, SRC)                                                         \
    __atomic_compare_exchange_n(&DST, &EXPECTED, SRC, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
#define ATOMIC_STORE_PTR(DST, SRC) ATOMIC_STORE(DST, SRC)
#define ATOMIC_LOAD_PTR(DST, SRC) ATOMIC_LOAD(DST, SRC)
