/* AzaharVR: the POSIX names gba-ds's and PokeDaisy's files use, on Windows. Forced into each of them
 * (/FI); the headers beside it only include it. Not for anything else: it renames stdio's functions.
 *
 * Paths are UTF-8 here, as everywhere in the program, and files are never in text mode, so what is
 * written is the same as on Linux. */
#ifndef AZAHARVR_WIN32_POSIX_H
#define AZAHARVR_WIN32_POSIX_H

#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define typeof __typeof__ /* (not in clang-cl's C11) */

#ifndef SSIZE_T_DEFINED
#define SSIZE_T_DEFINED
typedef intptr_t ssize_t; /* the same guard and type as mGBA's common.h */
#endif
typedef unsigned long id_t;

/* mGBA's common.h gives MSVC _snprintf under snprintf's name, which neither ends a cut string nor
 * returns the length it needed. */
#include "mgba_shim.h"

/* ... and <Windows.h>, with these in it. (FAR is far, so it goes too.) */
#undef near
#undef far
#undef small
#undef FAR
#define FAR
#undef NEAR
#define NEAR

/* ---- strings */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
/* Lists of folders are split at ':', which a drive's letter has after it: "C:/a:D:/b" is two. */
char* wp_strtok_r(char* s, const char* delim, char** save);
#define strtok_r wp_strtok_r

/* ---- files */
struct wp_stat {
    int st_mode;
    int64_t st_size;
    time_t st_mtime;
};
int wp_stat(const char* path, struct wp_stat* out);
int wp_fstat(int fd, struct wp_stat* out);
FILE* wp_fopen(const char* path, const char* mode);
int wp_open(const char* path, int flags, ...);
int wp_rename(const char* from, const char* to); /* replaces `to`, as POSIX's does */
int wp_remove(const char* path);
int wp_mkdir(const char* path, int mode);
int wp_access(const char* path, int mode);
int wp_fsync(int fd);
int futimens(int fd, const struct timespec times[2]);            /* only NULL: now */
ssize_t readlink(const char* path, char* out, size_t size); /* there are none */

#define stat wp_stat
#define fstat wp_fstat
#define fopen wp_fopen
#define open wp_open
#define rename wp_rename
#define remove wp_remove
#define unlink wp_remove
#define mkdir wp_mkdir
#define access wp_access
#define fsync wp_fsync

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT
#endif
#define F_OK 0
#define W_OK 2
#define R_OK 4

struct dirent {
    char d_name[260 * 3 + 1];
};
typedef struct wp_DIR DIR;
DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);

/* ---- time */
enum { CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_THREAD_CPUTIME_ID };
int clock_gettime(int clock, struct timespec* out);
int nanosleep(const struct timespec* want, struct timespec* left);
int usleep(unsigned us);
struct tm* wp_localtime_r(const time_t* t, struct tm* out);
#define localtime_r wp_localtime_r

/* ---- threads */
typedef unsigned long pthread_t; /* the thread's id: never 0, and not another's until it is joined */
typedef struct pthread_attr_t {
    int unused;
} pthread_attr_t;
typedef struct pthread_mutexattr_t {
    int unused;
} pthread_mutexattr_t;
typedef struct pthread_condattr_t {
    int unused;
} pthread_condattr_t;
typedef struct pthread_mutex_t {
    void* lock; /* SRWLOCK: like a default mutex, not recursive */
} pthread_mutex_t;
typedef struct pthread_cond_t {
    void* cond; /* CONDITION_VARIABLE */
} pthread_cond_t;
typedef struct pthread_once_t {
    void* once; /* INIT_ONCE */
} pthread_once_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }
#define PTHREAD_ONCE_INIT { 0 }

int pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg);
int pthread_join(pthread_t t, void** result); /* result: always NULL */
pthread_t pthread_self(void);                 /* of any thread, whoever made it */
int pthread_equal(pthread_t a, pthread_t b);
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* until);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_once(pthread_once_t* once, void (*fn)(void));

/* A thread's nice value, for syscall(SYS_gettid)'s thread, which is always the calling one */
enum { PRIO_PROCESS };
enum { SYS_gettid };
long syscall(long number);
int setpriority(int which, id_t who, int nice);

/* ---- uname, for a report nobody can ask for here */
struct utsname {
    char sysname[32], nodename[32], release[32], version[32], machine[32];
};
int uname(struct utsname* out);

#endif
