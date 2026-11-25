/* AzaharVR: win32_posix.h */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <errno.h>
#include <process.h>
#include <sys/utime.h>

#include "win32_posix.h"
#undef stat
#undef fstat
#undef fopen
#undef open
#undef rename
#undef remove
#undef mkdir
#undef access

_Static_assert(sizeof(pthread_mutex_t) == sizeof(SRWLOCK), "SRWLOCK");
_Static_assert(sizeof(pthread_cond_t) == sizeof(CONDITION_VARIABLE), "CONDITION_VARIABLE");
_Static_assert(sizeof(pthread_once_t) == sizeof(INIT_ONCE), "INIT_ONCE");

/* ---------------------------------------------------------------- strings */
static bool IsLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char* wp_strtok_r(char* s, const char* delim, char** save) {
    if (!s) s = *save;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return NULL;
    }
    char* end = s + strcspn(s, delim);
    if (*end == ':' && end == s + 1 && IsLetter(*s) && (end[1] == '/' || end[1] == '\\'))
        end += 1 + strcspn(end + 1, delim);
    *save = *end ? end + 1 : end;
    *end = 0;
    return s;
}

/* ---------------------------------------------------------------- paths */
/* To free(); NULL with errno set. Long paths are the program's to ask for (its manifest's). */
static wchar_t* Wide(const char* utf8) {
    int n = utf8 ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0) : 0;
    wchar_t* w = n > 0 ? malloc((size_t) n * sizeof(*w)) : NULL;
    if (!w || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, w, n) != n) {
        errno = n > 0 && !w ? ENOMEM : ENOENT;
        free(w);
        return NULL;
    }
    return w;
}

static int Errno(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        return ENOENT;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_DISK_FULL:
        return ENOSPC;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    default:
        return EACCES;
    }
}

int wp_stat(const char* path, struct wp_stat* out) {
    wchar_t* w = Wide(path);
    if (!w) return -1;
    struct _stat64 st;
    /* (a trailing slash names the directory on POSIX, and nothing here) */
    size_t l = wcslen(w);
    while (l > 1 && (w[l - 1] == L'/' || w[l - 1] == L'\\') && w[l - 2] != L':') w[--l] = 0;
    int rc = _wstat64(w, &st);
    free(w);
    if (rc) return -1;
    out->st_mode = st.st_mode;
    out->st_size = st.st_size;
    out->st_mtime = st.st_mtime;
    return 0;
}

int wp_fstat(int fd, struct wp_stat* out) {
    struct _stat64 st;
    if (_fstat64(fd, &st)) return -1;
    out->st_mode = st.st_mode;
    out->st_size = st.st_size;
    out->st_mtime = st.st_mtime;
    return 0;
}

int futimens(int fd, const struct timespec times[2]) {
    (void) times;
    return _futime64(fd, NULL);
}

ssize_t readlink(const char* path, char* out, size_t size) {
    (void) path, (void) out, (void) size;
    errno = EINVAL;
    return -1;
}

FILE* wp_fopen(const char* path, const char* mode) {
    wchar_t m[16];
    size_t n = 0;
    bool binary = false;
    for (; *mode && n < 12; ++mode) {
        if (*mode == 't' || *mode == 'e') continue; /* ('e' is O_CLOEXEC) */
        binary |= *mode == 'b';
        m[n++] = (wchar_t) *mode;
    }
    if (!binary) m[n++] = L'b';
    m[n++] = L'N'; /* not inherited */
    m[n] = 0;
    wchar_t* w = Wide(path);
    if (!w) return NULL;
    FILE* f = _wfopen(w, m);
    free(w);
    return f;
}

int wp_open(const char* path, int flags, ...) {
    int mode = 0;
    if (flags & _O_CREAT) {
        va_list a;
        va_start(a, flags);
        mode = va_arg(a, int);
        va_end(a);
    }
    wchar_t* w = Wide(path);
    if (!w) return -1;
    int fd = _wopen(w, flags | _O_BINARY, mode & 0200 ? _S_IREAD | _S_IWRITE : _S_IREAD);
    free(w);
    return fd;
}

int wp_rename(const char* from, const char* to) {
    wchar_t* a = Wide(from);
    wchar_t* b = Wide(to);
    bool ok = false;
    if (a && b) {
        ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) != 0;
        if (!ok) errno = Errno(GetLastError());
    }
    free(a);
    free(b);
    return ok ? 0 : -1;
}

int wp_remove(const char* path) {
    wchar_t* w = Wide(path);
    if (!w) return -1;
    DWORD attr = GetFileAttributesW(w);
    BOOL ok = attr != INVALID_FILE_ATTRIBUTES && attr & FILE_ATTRIBUTE_DIRECTORY ? RemoveDirectoryW(w) : DeleteFileW(w);
    if (!ok) errno = Errno(GetLastError());
    free(w);
    return ok ? 0 : -1;
}

int wp_mkdir(const char* path, int mode) {
    (void) mode;
    wchar_t* w = Wide(path);
    if (!w) return -1;
    BOOL ok = CreateDirectoryW(w, NULL);
    if (!ok) errno = Errno(GetLastError());
    free(w);
    return ok ? 0 : -1;
}

int wp_access(const char* path, int mode) {
    wchar_t* w = Wide(path);
    if (!w) return -1;
    int rc = _waccess(w, mode);
    free(w);
    return rc;
}

/* A file opened for reading only has nothing to flush, and FlushFileBuffers says no to it. */
int wp_fsync(int fd) {
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (FlushFileBuffers(h) || GetLastError() == ERROR_ACCESS_DENIED) return 0;
    errno = EIO;
    return -1;
}

struct wp_DIR {
    HANDLE find;
    bool have;
    WIN32_FIND_DATAW data;
    struct dirent entry;
};

DIR* opendir(const char* path) {
    wchar_t* w = Wide(path);
    if (!w) return NULL;
    size_t n = wcslen(w);
    wchar_t* all = malloc((n + 3) * sizeof(*all));
    DIR* d = calloc(1, sizeof(*d));
    if (all && d) {
        memcpy(all, w, n * sizeof(*w));
        if (n && all[n - 1] != L'/' && all[n - 1] != L'\\') all[n++] = L'\\';
        all[n++] = L'*';
        all[n] = 0;
        d->find = FindFirstFileW(all, &d->data);
        d->have = d->find != INVALID_HANDLE_VALUE;
        if (!d->have) errno = Errno(GetLastError());
    }
    free(w);
    free(all);
    if (d && d->have) return d;
    free(d);
    return NULL;
}

struct dirent* readdir(DIR* d) {
    while (d->have) {
        bool ok = WideCharToMultiByte(CP_UTF8, 0, d->data.cFileName, -1, d->entry.d_name, sizeof(d->entry.d_name), NULL, NULL) > 0;
        d->have = FindNextFileW(d->find, &d->data) != 0;
        if (ok) return &d->entry;
    }
    return NULL;
}

int closedir(DIR* d) {
    FindClose(d->find);
    free(d);
    return 0;
}

/* ---------------------------------------------------------------- time */
int clock_gettime(int clock, struct timespec* out) {
    if (clock == CLOCK_MONOTONIC) {
        LARGE_INTEGER now, rate;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&rate);
        out->tv_sec = now.QuadPart / rate.QuadPart;
        out->tv_nsec = (long) (now.QuadPart % rate.QuadPart * 1000000000 / rate.QuadPart);
        return 0;
    }
    if (clock == CLOCK_THREAD_CPUTIME_ID) {
        FILETIME a, b, kernel, user;
        if (!GetThreadTimes(GetCurrentThread(), &a, &b, &kernel, &user)) return -1;
        uint64_t t = ((uint64_t) kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) +
                     ((uint64_t) user.dwHighDateTime << 32 | user.dwLowDateTime);
        out->tv_sec = (time_t) (t / 10000000);
        out->tv_nsec = (long) (t % 10000000 * 100);
        return 0;
    }
    return timespec_get(out, TIME_UTC) == TIME_UTC ? 0 : -1;
}

int nanosleep(const struct timespec* want, struct timespec* left) {
    (void) left;
    Sleep((DWORD) (want->tv_sec * 1000 + (want->tv_nsec + 999999) / 1000000));
    return 0;
}

int usleep(unsigned us) {
    Sleep((us + 999) / 1000);
    return 0;
}

struct tm* wp_localtime_r(const time_t* t, struct tm* out) {
    return localtime_s(out, t) ? NULL : out;
}

/* ---------------------------------------------------------------- threads */
/* Linux's default: these threads' frames were written with it */
enum { kStack = 8 * 1024 * 1024 };

/* The threads that were made and not yet joined. Their handles keep their ids from being given again. */
static SRWLOCK madeLock = SRWLOCK_INIT;
static struct Made {
    struct Made* next;
    HANDLE handle;
    DWORD id;
}* made;

struct Start {
    void* (*fn)(void*);
    void* arg;
};

static unsigned __stdcall Run(void* p) {
    struct Start s = *(struct Start*) p;
    free(p);
    s.fn(s.arg);
    return 0;
}

int pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    (void) attr;
    struct Start* s = malloc(sizeof(*s));
    struct Made* m = malloc(sizeof(*m));
    unsigned id = 0;
    uintptr_t h = 0;
    if (s && m) {
        s->fn = fn;
        s->arg = arg;
        h = _beginthreadex(NULL, kStack, Run, s, CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
    }
    if (!h) {
        free(s);
        free(m);
        return EAGAIN;
    }
    m->handle = (HANDLE) h;
    m->id = id;
    AcquireSRWLockExclusive(&madeLock);
    m->next = made;
    made = m;
    ReleaseSRWLockExclusive(&madeLock);
    *t = id; /* before it runs, as it may look */
    ResumeThread((HANDLE) h);
    return 0;
}

int pthread_join(pthread_t t, void** result) {
    if (t == GetCurrentThreadId()) return EDEADLK;
    AcquireSRWLockExclusive(&madeLock);
    struct Made** at = &made;
    while (*at && (*at)->id != t) at = &(*at)->next;
    struct Made* m = *at;
    if (m) *at = m->next;
    ReleaseSRWLockExclusive(&madeLock);
    if (!m) return ESRCH;
    WaitForSingleObject(m->handle, INFINITE);
    CloseHandle(m->handle);
    free(m);
    if (result) *result = NULL;
    return 0;
}

pthread_t pthread_self(void) {
    return GetCurrentThreadId();
}

int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr) {
    (void) attr;
    InitializeSRWLock((PSRWLOCK) m);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* m) {
    (void) m;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m) {
    AcquireSRWLockExclusive((PSRWLOCK) m);
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
    ReleaseSRWLockExclusive((PSRWLOCK) m);
    return 0;
}

int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* attr) {
    (void) attr;
    InitializeConditionVariable((PCONDITION_VARIABLE) c);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* c) {
    (void) c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
    return SleepConditionVariableSRW((PCONDITION_VARIABLE) c, (PSRWLOCK) m, INFINITE, 0) ? 0 : EINVAL;
}

int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* until) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t ms = (int64_t) (until->tv_sec - now.tv_sec) * 1000 + (until->tv_nsec - now.tv_nsec + 999999) / 1000000;
    if (ms < 0) ms = 0;
    if (ms > 0x7FFFFFFF) ms = 0x7FFFFFFF;
    if (SleepConditionVariableSRW((PCONDITION_VARIABLE) c, (PSRWLOCK) m, (DWORD) ms, 0)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

int pthread_cond_signal(pthread_cond_t* c) {
    WakeConditionVariable((PCONDITION_VARIABLE) c);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
    WakeAllConditionVariable((PCONDITION_VARIABLE) c);
    return 0;
}

static BOOL CALLBACK Once(PINIT_ONCE once, PVOID fn, PVOID* context) {
    (void) once, (void) context;
    ((void (*)(void)) fn)();
    return TRUE;
}

int pthread_once(pthread_once_t* once, void (*fn)(void)) {
    InitOnceExecuteOnce((PINIT_ONCE) once, Once, (PVOID) fn, NULL);
    return 0;
}

long syscall(long number) {
    (void) number;
    return (long) GetCurrentThreadId();
}

int setpriority(int which, id_t who, int nice) {
    (void) which;
    if ((DWORD) who != GetCurrentThreadId()) return -1;
    int p = nice >= 15 ? THREAD_PRIORITY_LOWEST : nice > 0 ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL;
    return SetThreadPriority(GetCurrentThread(), p) ? 0 : -1;
}

int uname(struct utsname* out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->sysname, "Windows");
    strcpy(out->machine, "x86_64");
    return 0;
}
