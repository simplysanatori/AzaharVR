// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <neaacdec.h>

#include "Platform.h"
#include "SPI_Firmware.h"

#include "common/logging/log.h"
#include "frame_cores/nds_core.h"

namespace FrameCores {
namespace {
std::mutex g_local_dir_mutex;
std::string g_local_dir;
} // namespace

void SetMelonDSLocalDir(const std::string& dir) {
    std::lock_guard lock(g_local_dir_mutex);
    g_local_dir = dir;
}

static std::string GetMelonDSLocalDir() {
    std::lock_guard lock(g_local_dir_mutex);
    return g_local_dir;
}
} // namespace FrameCores

namespace melonDS::Platform {

struct FileHandle {
    FILE* fp = nullptr;
};

struct Thread {
    std::thread thread;
};

struct Semaphore {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;
};

struct Mutex {
    std::mutex mutex;
};

struct AACDecoder {
    bool inited = false;
    NeAACDecHandle handle = nullptr;
};

struct DynamicLibrary {};

namespace {

bool SeekFile64(FILE* fp, s64 offset, int whence) {
#ifdef _WIN32
    return _fseeki64(fp, offset, whence) == 0;
#else
    return fseeko(fp, static_cast<off_t>(offset), whence) == 0;
#endif
}

s64 TellFile64(FILE* fp) {
#ifdef _WIN32
    return static_cast<s64>(_ftelli64(fp));
#else
    return static_cast<s64>(ftello(fp));
#endif
}

std::string BuildOpenModeString(FileMode mode, bool exists) {
    const bool read = (mode & FileMode::Read) != 0;
    const bool write = (mode & FileMode::Write) != 0;
    const bool preserve = (mode & FileMode::Preserve) != 0;
    const bool no_create = (mode & FileMode::NoCreate) != 0;
    const bool text = (mode & FileMode::Text) != 0;
    const bool append = (mode & FileMode::Append) != 0;

    std::string out;
    if (append) {
        out = read ? "a+" : "a";
    } else if (!write) {
        out = "r";
    } else if (no_create || (preserve && exists)) {
        out = "r+";
    } else {
        out = read ? "w+" : "w";
    }

    if (!text) {
        out.push_back('b');
    }
    return out;
}

} // namespace

void SignalStop(StopReason reason, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnSignalStop(static_cast<int>(reason));
    }
}

std::string GetLocalFilePath(const std::string& filename) {
    if (filename.empty()) {
        return filename;
    }
    std::filesystem::path p(filename);
    if (p.is_absolute()) {
        return filename;
    }
    const std::string base = FrameCores::GetMelonDSLocalDir();
    if (base.empty()) {
        return filename;
    }
    return (std::filesystem::path(base) / p).string();
}

FileHandle* OpenFile(const std::string& path, FileMode mode) {
    if ((mode & (FileMode::ReadWrite | FileMode::Append)) == FileMode::None) {
        return nullptr;
    }
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if ((mode & FileMode::Write) && (mode & FileMode::NoCreate) && !exists) {
        return nullptr;
    }
    const std::string mode_str = BuildOpenModeString(mode, exists);
    FILE* fp = std::fopen(path.c_str(), mode_str.c_str());
    if (!fp) {
        return nullptr;
    }
    return new FileHandle{fp};
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) {
    return OpenFile(GetLocalFilePath(path), mode);
}

bool FileExists(const std::string& name) {
    std::error_code ec;
    return std::filesystem::is_regular_file(name, ec);
}

bool LocalFileExists(const std::string& name) {
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath) {
    if (FileHandle* f = OpenFile(filepath, static_cast<FileMode>(FileMode::Write | FileMode::Preserve))) {
        CloseFile(f);
        return true;
    }
    return false;
}

bool CheckLocalFileWritable(const std::string& filepath) {
    return CheckFileWritable(GetLocalFilePath(filepath));
}

bool CloseFile(FileHandle* file) {
    if (!file) {
        return false;
    }
    const bool ok = file->fp ? (std::fclose(file->fp) == 0) : true;
    delete file;
    return ok;
}

bool IsEndOfFile(FileHandle* file) {
    if (!file || !file->fp) {
        return true;
    }
    return std::feof(file->fp) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file) {
    if (!file || !file->fp || !str || count <= 0) {
        return false;
    }
    return std::fgets(str, count, file->fp) != nullptr;
}

u64 FilePosition(FileHandle* file) {
    if (!file || !file->fp) {
        return 0;
    }
    const s64 pos = TellFile64(file->fp);
    return pos > 0 ? static_cast<u64>(pos) : 0;
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) {
    if (!file || !file->fp) {
        return false;
    }
    int whence = SEEK_SET;
    switch (origin) {
    case FileSeekOrigin::Start:
        whence = SEEK_SET;
        break;
    case FileSeekOrigin::Current:
        whence = SEEK_CUR;
        break;
    case FileSeekOrigin::End:
        whence = SEEK_END;
        break;
    }
    return SeekFile64(file->fp, offset, whence);
}

void FileRewind(FileHandle* file) {
    if (file && file->fp) {
        std::rewind(file->fp);
    }
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) {
    if (!file || !file->fp || !data || size == 0 || count == 0) {
        return 0;
    }
    return static_cast<u64>(
        std::fread(data, static_cast<std::size_t>(size), static_cast<std::size_t>(count), file->fp));
}

bool FileFlush(FileHandle* file) {
    if (!file || !file->fp) {
        return false;
    }
    return std::fflush(file->fp) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) {
    if (!file || !file->fp || !data || size == 0 || count == 0) {
        return 0;
    }
    return static_cast<u64>(std::fwrite(data, static_cast<std::size_t>(size),
                                        static_cast<std::size_t>(count), file->fp));
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...) {
    if (!file || !file->fp || !fmt) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    const int written = std::vfprintf(file->fp, fmt, args);
    va_end(args);
    return written > 0 ? static_cast<u64>(written) : 0;
}

u64 FileLength(FileHandle* file) {
    if (!file || !file->fp) {
        return 0;
    }
    const s64 cur = TellFile64(file->fp);
    if (cur < 0) {
        return 0;
    }
    if (!SeekFile64(file->fp, 0, SEEK_END)) {
        return 0;
    }
    const s64 end = TellFile64(file->fp);
    SeekFile64(file->fp, cur, SEEK_SET);
    return end > 0 ? static_cast<u64>(end) : 0;
}

void Log(LogLevel level, const char* fmt, ...) {
    if (!fmt) {
        return;
    }
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::size_t len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    switch (level) {
    case LogLevel::Debug:
        LOG_DEBUG(Core, "melonDS: {}", buf);
        break;
    case LogLevel::Info:
        LOG_INFO(Core, "melonDS: {}", buf);
        break;
    case LogLevel::Warn:
        LOG_WARNING(Core, "melonDS: {}", buf);
        break;
    case LogLevel::Error:
        LOG_ERROR(Core, "melonDS: {}", buf);
        break;
    }
}

Thread* Thread_Create(std::function<void()> func) {
    auto* t = new Thread();
    t->thread = std::thread(std::move(func));
    return t;
}

void Thread_Free(Thread* thread) {
    if (!thread) {
        return;
    }
    if (thread->thread.joinable()) {
        thread->thread.join();
    }
    delete thread;
}

void Thread_Wait(Thread* thread) {
    if (thread && thread->thread.joinable()) {
        thread->thread.join();
    }
}

Semaphore* Semaphore_Create() {
    return new Semaphore();
}

void Semaphore_Free(Semaphore* sema) {
    delete sema;
}

void Semaphore_Reset(Semaphore* sema) {
    if (!sema) {
        return;
    }
    std::lock_guard lock(sema->mutex);
    sema->count = 0;
}

void Semaphore_Wait(Semaphore* sema) {
    if (!sema) {
        return;
    }
    std::unique_lock lock(sema->mutex);
    sema->cv.wait(lock, [sema] { return sema->count > 0; });
    --sema->count;
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms) {
    if (!sema) {
        return false;
    }
    std::unique_lock lock(sema->mutex);
    if (timeout_ms <= 0) {
        if (sema->count > 0) {
            --sema->count;
            return true;
        }
        return false;
    }
    if (sema->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [sema] { return sema->count > 0; })) {
        --sema->count;
        return true;
    }
    return false;
}

void Semaphore_Post(Semaphore* sema, int count) {
    if (!sema || count <= 0) {
        return;
    }
    {
        std::lock_guard lock(sema->mutex);
        sema->count += count;
    }
    if (count == 1) {
        sema->cv.notify_one();
    } else {
        sema->cv.notify_all();
    }
}

Mutex* Mutex_Create() {
    return new Mutex();
}

void Mutex_Free(Mutex* mutex) {
    delete mutex;
}

void Mutex_Lock(Mutex* mutex) {
    if (mutex) {
        mutex->mutex.lock();
    }
}

void Mutex_Unlock(Mutex* mutex) {
    if (mutex) {
        mutex->mutex.unlock();
    }
}

bool Mutex_TryLock(Mutex* mutex) {
    if (!mutex) {
        return false;
    }
    return mutex->mutex.try_lock();
}

void Sleep(u64 usecs) {
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

u64 GetUSCount() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnWriteNDSSave(savedata, savelen, writeoffset, writelen);
    }
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnWriteGBASave(savedata, savelen, writeoffset, writelen);
    }
}

void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnWriteFirmware(firmware.Buffer(), firmware.Length(), writeoffset, writelen);
    }
}

void WriteDateTime(int /*year*/, int /*month*/, int /*day*/, int /*hour*/, int /*minute*/,
                   int /*second*/, void* /*userdata*/) {}

// Local multiplayer stubs (not exposed in VR single-console mode)
void MP_Begin(void* /*userdata*/) {}
void MP_End(void* /*userdata*/) {}
int MP_SendPacket(u8* /*data*/, int /*len*/, u64 /*timestamp*/, void* /*userdata*/) {
    return 0;
}
int MP_RecvPacket(u8* /*data*/, u64* /*timestamp*/, void* /*userdata*/) {
    return 0;
}
int MP_SendCmd(u8* /*data*/, int /*len*/, u64 /*timestamp*/, void* /*userdata*/) {
    return 0;
}
int MP_SendReply(u8* /*data*/, int /*len*/, u64 /*timestamp*/, u16 /*aid*/, void* /*userdata*/) {
    return 0;
}
int MP_SendAck(u8* /*data*/, int /*len*/, u64 /*timestamp*/, void* /*userdata*/) {
    return 0;
}
int MP_RecvHostPacket(u8* /*data*/, u64* /*timestamp*/, void* /*userdata*/) {
    return 0;
}
u16 MP_RecvReplies(u8* /*data*/, u64 /*timestamp*/, u16 /*aidmask*/, void* /*userdata*/) {
    return 0;
}

// Network stubs
int Net_SendPacket(u8* /*data*/, int /*len*/, void* /*userdata*/) {
    return 0;
}
int Net_RecvPacket(u8* /*data*/, void* /*userdata*/) {
    return 0;
}

// DSi camera stubs (clean black frame)
void Camera_Start(int /*num*/, void* /*userdata*/) {}
void Camera_Stop(int /*num*/, void* /*userdata*/) {}
void Camera_CaptureFrame(int /*num*/, u32* frame, int width, int height, bool yuv,
                         void* /*userdata*/) {
    if (!frame || width <= 0 || height <= 0) {
        return;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (yuv) {
        // YUYV black: Y=0, U=128, V=128 -> 0x80008000 per pixel pair
        const std::size_t words = pixels / 2u;
        for (std::size_t i = 0; i < words; ++i) {
            frame[i] = 0x80008000u;
        }
    } else {
        std::memset(frame, 0, pixels * sizeof(u32));
    }
}

// Microphone callbacks
void Mic_Start(void* /*userdata*/) {}
void Mic_Stop(void* /*userdata*/) {}

int Mic_ReadInput(s16* data, int maxlength, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        return core->OnMicReadInput(data, maxlength);
    }
    return 0;
}

// AAC decoding for DSi DSP HLE backed by Azahar's bundled faad2
AACDecoder* AAC_Init() {
    return new AACDecoder();
}

void AAC_DeInit(AACDecoder* dec) {
    if (!dec) {
        return;
    }
    if (dec->handle) {
        NeAACDecClose(dec->handle);
        dec->handle = nullptr;
    }
    delete dec;
}

bool AAC_Configure(AACDecoder* dec, int frequency, int channels) {
    if (!dec) {
        return false;
    }
    if (dec->handle) {
        NeAACDecClose(dec->handle);
        dec->handle = nullptr;
        dec->inited = false;
    }

    dec->handle = NeAACDecOpen();
    if (!dec->handle) {
        return false;
    }

    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec->handle);
    cfg->defObjectType = LC;
    cfg->outputFormat = FAAD_FMT_16BIT;
    if (!NeAACDecSetConfiguration(dec->handle, cfg)) {
        return false;
    }

    static constexpr int kFreqList[9] = {48000, 44100, 32000, 24000, 22050,
                                         16000, 12000, 11025, 8000};
    u8 freq_idx = 3; // default 48000 Hz
    for (int i = 0; i < 9; ++i) {
        if (frequency == kFreqList[i]) {
            freq_idx = static_cast<u8>(3 + i);
            break;
        }
    }

    const u8 ch = static_cast<u8>(channels & 0x0F);
    u8 asc[5]{
        static_cast<u8>(0x10 | (freq_idx >> 1)),
        static_cast<u8>((freq_idx << 7) | (ch << 3)),
        0x56,
        0xE5,
        0x00,
    };

    unsigned long freq_out = 0;
    u8 chan_out = 0;
    if (NeAACDecInit2(dec->handle, asc, sizeof(asc), &freq_out, &chan_out) != 0) {
        return false;
    }

    dec->inited = true;
    return true;
}

bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output,
                     int outputlen) {
    if (!dec || !dec->inited || !input || !output || inputlen <= 0 || outputlen <= 0) {
        return false;
    }

    NeAACDecFrameInfo finfo{};
    void* out_ptr = output;
    NeAACDecDecode2(dec->handle, &finfo, const_cast<unsigned char*>(static_cast<const unsigned char*>(input)),
                    static_cast<unsigned long>(inputlen), &out_ptr,
                    static_cast<unsigned long>(outputlen));

    if (finfo.error != 0) {
        return false;
    }
    if (finfo.bytesconsumed != static_cast<unsigned long>(inputlen)) {
        return false;
    }
    return true;
}

// Addon inputs & Rumble Pak
bool Addon_KeyDown(KeyType /*type*/, void* /*userdata*/) {
    return false;
}

void Addon_RumbleStart(u32 len, void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnRumbleStart(len);
    }
}

void Addon_RumbleStop(void* userdata) {
    if (auto* core = static_cast<FrameCores::NdsCore*>(userdata)) {
        core->OnRumbleStop();
    }
}

float Addon_MotionQuery(MotionQueryType /*type*/, void* /*userdata*/) {
    return 0.0f;
}

// Dynamic library stubs
DynamicLibrary* DynamicLibrary_Load(const char* /*lib*/) {
    return nullptr;
}

void DynamicLibrary_Unload(DynamicLibrary* /*lib*/) {}

void* DynamicLibrary_LoadFunction(DynamicLibrary* /*lib*/, const char* /*name*/) {
    return nullptr;
}

} // namespace melonDS::Platform
