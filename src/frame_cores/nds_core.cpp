// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

#include "ARCodeFile.h"
#include "AREngine.h"
#include "Args.h"
#include "DSi.h"
#include "DSi_NAND.h"
#include "FreeBIOS.h"
#include "GBACart.h"
#include "GPU.h"
#include "NDS.h"
#include "NDSCart.h"
#include "Platform.h"
#include "RTC.h"
#include "SPI_Firmware.h"
#include "SPU.h"
#include "Savestate.h"

#include "common/logging/log.h"
#include "frame_cores/rom_info.h"

namespace FrameCores {
namespace {

constexpr uint32_t kDsWidth = 256;
constexpr uint32_t kDsHeight = 192;
constexpr double kDsAudioRate = 32768.0;

bool EnsureDir(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool IsZipPath(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (char& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".zip";
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    if (sz == 0) {
        return false;
    }
    out.resize(sz);
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz))) {
        out.clear();
        return false;
    }
    return true;
}

bool AtomicWriteFile(const std::string& path, const uint8_t* data, std::size_t size,
                     bool keep_backup) {
    if (path.empty() || (!data && size > 0)) {
        return false;
    }
    EnsureDir(std::filesystem::path(path).parent_path().string());
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        if (size > 0) {
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        out.flush();
        if (!out.good()) {
            return false;
        }
    }
    std::error_code ec;
    if (keep_backup && std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, path + ".bak", ec);
        ec.clear();
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

template <std::size_t N>
std::unique_ptr<std::array<melonDS::u8, N>> TryLoadExactBios(const std::filesystem::path& path) {
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path.string(), bytes) || bytes.size() != N) {
        return nullptr;
    }
    auto arr = std::make_unique<std::array<melonDS::u8, N>>();
    std::memcpy(arr->data(), bytes.data(), N);
    return arr;
}

std::optional<melonDS::Firmware> TryLoadFirmware(const std::filesystem::path& path) {
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path.string(), bytes) || bytes.size() < 0x20000) {
        return std::nullopt;
    }
    return melonDS::Firmware(bytes.data(), static_cast<melonDS::u32>(bytes.size()));
}

void SetSystemRtc(melonDS::NDS& nds) {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    nds.RTC.SetDateTime(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                        local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
}

std::vector<melonDS::u32> ParseArCodeHex(const std::string& text) {
    std::vector<melonDS::u32> words;
    std::string hex;
    hex.reserve(text.size());
    for (char ch : text) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            hex.push_back(ch);
        }
    }
    for (std::size_t i = 0; i + 8 <= hex.size(); i += 8) {
        const melonDS::u32 word = static_cast<melonDS::u32>(
            std::stoul(hex.substr(i, 8), nullptr, 16));
        words.push_back(word);
    }
    // Action Replay codes come in 64-bit (2-word) pairs
    if ((words.size() & 1u) != 0) {
        words.pop_back();
    }
    return words;
}

std::string FormatArCodeHex(const std::vector<melonDS::u32>& words) {
    std::ostringstream oss;
    for (std::size_t i = 0; i + 1 < words.size(); i += 2) {
        if (i > 0) {
            oss << '\n';
        }
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%08X %08X", words[i], words[i + 1]);
        oss << buf;
    }
    return oss.str();
}

} // namespace

struct NdsCore::Impl {
    std::unique_ptr<melonDS::NDS> nds;
    bool is_dsi = false;
    bool loaded = false;

    std::string rom_path;
    std::string rom_filename;
    std::string save_path;
    std::string gba_save_path;
    std::string cheat_file_path;
    CorePaths paths{};
    CoreBootOptions opts{};

    Common::AltCore::InputState input{};
    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForNDS(false);

    bool touch_enabled = true;
    int rotation_deg = 0;
    bool metrics_dirty = false;
    std::atomic<bool> mic_blow_toggle{false};
    std::atomic<bool> mic_blow_button{false};
    Microphone microphone;
    uint32_t mic_rng_state = 0x12345678u;
    int32_t mic_filter = 0;

    std::atomic<int64_t> rumble_until_ms{0};
    std::atomic<bool> stop_signalled{false};

    // Save write-back tracking (atomic temp + rename, flushed periodically & on unload)
    std::mutex save_mutex;
    std::vector<uint8_t> pending_nds_save;
    bool nds_save_dirty = false;
    int64_t nds_save_dirty_since_ms = 0;

    std::vector<uint8_t> pending_gba_save;
    bool gba_save_dirty = false;

    std::vector<int16_t> audio_scratch;
    std::unique_ptr<melonDS::ARCodeFile> ar_file;

    static int64_t NowMs() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    void FlushPendingSaves(bool force) {
        std::vector<uint8_t> nds_copy;
        std::vector<uint8_t> gba_copy;
        std::string nds_dst;
        std::string gba_dst;

        {
            std::lock_guard lock(save_mutex);
            const int64_t now = NowMs();
            if (nds_save_dirty && (force || (now - nds_save_dirty_since_ms) >= 500)) {
                nds_copy = pending_nds_save;
                nds_dst = save_path;
                nds_save_dirty = false;
            }
            if (gba_save_dirty && !gba_save_path.empty()) {
                gba_copy = pending_gba_save;
                gba_dst = gba_save_path;
                gba_save_dirty = false;
            }
        }

        if (!nds_copy.empty() && !nds_dst.empty()) {
            if (!AtomicWriteFile(nds_dst, nds_copy.data(), nds_copy.size(), false)) {
                LOG_ERROR(Core, "NdsCore: failed to flush NDS save to {}", nds_dst);
            }
        }
        if (!gba_copy.empty() && !gba_dst.empty()) {
            if (!AtomicWriteFile(gba_dst, gba_copy.data(), gba_copy.size(), false)) {
                LOG_ERROR(Core, "NdsCore: failed to flush GBA slot save to {}", gba_dst);
            }
        }
    }

    bool LoadGbaCartInternal(const std::string& gba_path, void* userdata) {
        if (!nds || is_dsi) {
            return false;
        }
        if (gba_path.empty()) {
            gba_save_path.clear();
            if (opts.rumble_pak) {
                auto addon = melonDS::GBACart::LoadAddon(melonDS::GBAAddon_RumblePak, userdata);
                nds->SetGBACart(std::move(addon));
            } else {
                nds->EjectGBACart();
            }
            return true;
        }

        std::vector<uint8_t> rom_bytes;
        if (IsZipPath(gba_path)) {
            if (!ExtractRomFromZip(gba_path, rom_bytes) || rom_bytes.empty()) {
                return false;
            }
        } else if (!ReadWholeFile(gba_path, rom_bytes)) {
            return false;
        }

        const std::string gba_stem = std::filesystem::path(gba_path).stem().string();
        std::filesystem::path sav_candidate =
            std::filesystem::path(gba_path).replace_extension(".sav");
        if (!std::filesystem::exists(sav_candidate) && !paths.data_dir.empty()) {
            if (const auto gba = ProbeRom(gba_path)) { // the save this GBA game has on its own
                const auto alt =
                    std::filesystem::path(GameFolder(paths.data_dir, *gba)) / (gba_stem + ".sav");
                if (std::filesystem::exists(alt)) {
                    sav_candidate = alt;
                }
            }
        }
        gba_save_path = sav_candidate.string();

        std::vector<uint8_t> sram_bytes;
        ReadWholeFile(gba_save_path, sram_bytes);

        auto cart = melonDS::GBACart::ParseROM(
            rom_bytes.data(), static_cast<melonDS::u32>(rom_bytes.size()),
            sram_bytes.empty() ? nullptr : sram_bytes.data(),
            static_cast<melonDS::u32>(sram_bytes.size()), userdata);
        if (!cart) {
            LOG_ERROR(Core, "NdsCore: failed to parse GBA slot ROM {}", gba_path);
            return false;
        }
        nds->SetGBACart(std::move(cart));
        LOG_INFO(Core, "NdsCore: inserted GBA Slot-2 ROM {}", gba_path);
        return true;
    }

    void LoadCheatsFromDisk() {
        if (!nds) {
            return;
        }
        nds->AREngine.Cheats.clear();
        ar_file = std::make_unique<melonDS::ARCodeFile>(cheat_file_path);
        if (std::filesystem::exists(cheat_file_path) && ar_file->Load()) {
            nds->AREngine.Cheats = ar_file->GetCodes();
        }
    }
};

std::string PrepareWorkingNand(const std::filesystem::path& bios_dir) {
    std::error_code ec;
    const auto original = bios_dir / "dsi_nand.bin";
    const auto size = std::filesystem::file_size(original, ec);
    if (ec) {
        return {};
    }
    const auto mtime = static_cast<long long>(
        std::filesystem::last_write_time(original, ec).time_since_epoch().count());
    const std::string stamp = fmt::format("{} {}", size, mtime);
    const auto copy = bios_dir / "dsi_nand_working.bin";
    const auto stamp_file = bios_dir / "dsi_nand_working.txt";
    std::string have;
    {
        std::ifstream in(stamp_file);
        std::getline(in, have);
    }
    if (have == stamp && std::filesystem::exists(copy, ec)) {
        return copy.string();
    }
    if (std::filesystem::exists(copy, ec)) {
        std::filesystem::rename(copy, copy.string() + ".bak", ec);
        if (ec) {
            LOG_ERROR(Core, "NdsCore: cannot keep the old DSi NAND copy: {}", ec.message());
            return {};
        }
    }
    const std::string tmp = copy.string() + ".tmp";
    std::filesystem::copy_file(original, tmp, std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (!ec) {
        std::filesystem::permissions(tmp, std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ec);
    }
    if (!ec) {
        std::filesystem::rename(tmp, copy, ec);
    }
    if (ec) {
        LOG_ERROR(Core, "NdsCore: cannot copy the DSi NAND: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return {};
    }
    std::ofstream(stamp_file, std::ios::trunc) << stamp;
    return copy.string();
}

NdsCore::NdsCore() : impl(std::make_unique<Impl>()) {}

NdsCore::~NdsCore() {
    Unload();
}

bool NdsCore::Load(const std::string& rom_path, const CorePaths& paths,
                   const CoreBootOptions& opts) {
    Unload();

    impl->rom_path = rom_path;
    impl->paths = paths;
    impl->opts = opts;
    impl->stop_signalled.store(false, std::memory_order_relaxed);
    impl->rumble_until_ms.store(0, std::memory_order_relaxed);
    impl->mic_blow_toggle.store(false, std::memory_order_relaxed);
    impl->mic_blow_button.store(false, std::memory_order_relaxed);
    impl->touch_enabled = true;
    impl->rotation_deg = 0;
    impl->metrics_dirty = false;

    EnsureDir(paths.game_dir);
    EnsureDir(paths.cheat_dir);
    EnsureDir(paths.bios_dir);
    SetMelonDSLocalDir(paths.bios_dir);

    std::vector<uint8_t> rom_bytes;
    std::string inner_name;
    if (IsZipPath(rom_path)) {
        if (!ExtractRomFromZip(rom_path, rom_bytes, &inner_name) || rom_bytes.size() < 0x200) {
            LOG_ERROR(Core, "NdsCore: failed to extract DS ROM from {}", rom_path);
            return false;
        }
        impl->rom_filename = inner_name;
    } else {
        if (!ReadWholeFile(rom_path, rom_bytes) || rom_bytes.size() < 0x200) {
            LOG_ERROR(Core, "NdsCore: failed to read DS ROM {}", rom_path);
            return false;
        }
        impl->rom_filename = std::filesystem::path(rom_path).filename().string();
    }

    const std::string stem = std::filesystem::path(rom_path).stem().string();
    impl->save_path = (std::filesystem::path(paths.game_dir) / (stem + ".sav")).string();
    impl->cheat_file_path = (std::filesystem::path(paths.game_dir) / (stem + ".mch")).string();
    {
        // The game's own cheats; a file the player put in the shared folder is the start of them.
        std::error_code ec;
        const auto shared = std::filesystem::path(paths.cheat_dir) / (stem + ".mch");
        if (!std::filesystem::exists(impl->cheat_file_path, ec) &&
            std::filesystem::exists(shared, ec)) {
            std::filesystem::copy_file(shared, impl->cheat_file_path, ec);
        }
    }

    // Detect DSi-enhanced / DSiWare header unit code (offset 0x12, bit 1)
    const uint8_t unit_code = rom_bytes[0x12];
    const bool rom_is_dsi_capable = (unit_code & 0x02) != 0;
    const bool rom_is_dsiware =
        (unit_code == 0x03) ||
        (rom_is_dsi_capable && rom_bytes.size() >= 0x238 &&
         (static_cast<uint32_t>(rom_bytes[0x234]) |
          (static_cast<uint32_t>(rom_bytes[0x235]) << 8) |
          (static_cast<uint32_t>(rom_bytes[0x236]) << 16) |
          (static_cast<uint32_t>(rom_bytes[0x237]) << 24)) == 0x00030004u);

    const bool use_dsi =
        opts.force_dsi || (!opts.force_nds && (rom_is_dsiware || rom_is_dsi_capable));
    impl->is_dsi = use_dsi;
    impl->caps = Common::AltCore::Capabilities::ForNDS(use_dsi);

    // Prepare battery save if already on disk
    melonDS::NDSCart::NDSCartArgs cart_args{};
    std::vector<uint8_t> existing_save;
    if (ReadWholeFile(impl->save_path, existing_save) && !existing_save.empty()) {
        cart_args.SRAM = std::make_unique<melonDS::u8[]>(existing_save.size());
        std::memcpy(cart_args.SRAM.get(), existing_save.data(), existing_save.size());
        cart_args.SRAMLength = static_cast<melonDS::u32>(existing_save.size());
    }

    auto cart = melonDS::NDSCart::ParseROM(
        rom_bytes.data(), static_cast<melonDS::u32>(rom_bytes.size()), this,
        std::move(cart_args));
    if (!cart) {
        LOG_ERROR(Core, "NdsCore: melonDS failed to parse ROM {}", rom_path);
        return false;
    }

    // Build NDSArgs / DSiArgs with bundled FreeBIOS by default + optional user BIOS/firmware/NAND
    const std::filesystem::path bios_dir(paths.bios_dir);
    melonDS::NDSArgs nds_args{};
    nds_args.OutputSampleRate = kDsAudioRate;
    nds_args.BitDepth = melonDS::AudioBitDepth::Auto;
    nds_args.Interpolation = melonDS::AudioInterpolation::Linear;

    if (opts.jit_enabled) {
        melonDS::JITArgs jit{};
        jit.MaxBlockSize = 32;
        jit.LiteralOptimizations = true;
        jit.BranchOptimizations = true;
        jit.FastMemory = opts.jit_fastmem;
        nds_args.JIT = jit;
    } else {
        nds_args.JIT = std::nullopt;
    }

    if (opts.use_bios && !paths.bios_dir.empty()) {
        if (auto b9 = TryLoadExactBios<melonDS::ARM9BIOSSize>(bios_dir / "bios9.bin")) {
            nds_args.ARM9BIOS = std::move(b9);
        }
        if (auto b7 = TryLoadExactBios<melonDS::ARM7BIOSSize>(bios_dir / "bios7.bin")) {
            nds_args.ARM7BIOS = std::move(b7);
        }
    }

    if (use_dsi) {
        melonDS::DSiArgs dsi_args{};
        static_cast<melonDS::NDSArgs&>(dsi_args) = std::move(nds_args);

        // Default to FreeBIOS TWL + generated DSi firmware so DSi mode boots without dumps
        dsi_args.ARM9iBIOS = std::make_unique<melonDS::DSiBIOSImage>(melonDS::FreeBIOSGetTwlArm9());
        dsi_args.ARM7iBIOS = std::make_unique<melonDS::DSiBIOSImage>(melonDS::FreeBIOSGetTwlArm7());
        dsi_args.Firmware = melonDS::Firmware(1);
        dsi_args.DSPHLE = true;

        bool has_user_arm7i = false;
        if (opts.use_bios && !paths.bios_dir.empty()) {
            if (auto b9i = TryLoadExactBios<melonDS::DSiBIOSSize>(bios_dir / "dsi_bios9.bin")) {
                dsi_args.ARM9iBIOS = std::move(b9i);
            }
            if (auto b7i = TryLoadExactBios<melonDS::DSiBIOSSize>(bios_dir / "dsi_bios7.bin")) {
                dsi_args.ARM7iBIOS = std::move(b7i);
                has_user_arm7i = true;
            }
            if (auto fw = TryLoadFirmware(bios_dir / "dsi_firmware.bin")) {
                dsi_args.Firmware = std::move(*fw);
            }
            const auto nand_path = has_user_arm7i ? PrepareWorkingNand(bios_dir) : std::string();
            if (!nand_path.empty()) {
                if (auto* nf = melonDS::Platform::OpenFile(
                        nand_path, melonDS::Platform::FileMode::ReadWriteExisting)) {
                    melonDS::DSi_NAND::NANDImage img(nf, &(*dsi_args.ARM7iBIOS)[0x8308]);
                    if (img) {
                        dsi_args.NANDImage = std::move(img);
                        LOG_INFO(Core, "NdsCore: mounted DSi NAND at {}", nand_path);
                    }
                }
            }
        }

        impl->nds = std::make_unique<melonDS::DSi>(std::move(dsi_args), this);
    } else {
        if (opts.use_bios && !paths.bios_dir.empty()) {
            if (auto fw = TryLoadFirmware(bios_dir / "firmware.bin")) {
                nds_args.Firmware = std::move(*fw);
            }
        }
        impl->nds = std::make_unique<melonDS::NDS>(std::move(nds_args), this);
    }

    // Configure threaded software 3D renderer
    melonDS::RendererSettings rsettings{};
    rsettings.ScaleFactor = 1;
    rsettings.Threaded = opts.threaded_3d;
    rsettings.HiresCoordinates = false;
    rsettings.BetterPolygons = false;
    impl->nds->GetRenderer().SetRenderSettings(rsettings);

    impl->nds->SetNDSCart(std::move(cart));
    if (!use_dsi) {
        impl->LoadGbaCartInternal(opts.gba_slot_rom, this);
    }

    impl->nds->Reset();
    SetSystemRtc(*impl->nds);
    impl->nds->SetupDirectBoot(impl->rom_filename);
    impl->nds->Start();

    impl->audio_scratch.resize(4096u * 2u, 0);
    impl->LoadCheatsFromDisk();
    impl->loaded = true;

    LOG_INFO(Core, "NdsCore: booted {} in {} mode (JIT={}, threaded3D={})", rom_path,
             use_dsi ? "DSi" : "DS", impl->nds->IsJITEnabled(), opts.threaded_3d);
    return true;
}

void NdsCore::Unload() {
    if (!impl) {
        return;
    }
    impl->FlushPendingSaves(true);
    if (impl->nds) {
        impl->nds->Stop(melonDS::Platform::StopReason::External);
        impl->nds.reset();
    }
    impl->microphone.Stop();
    impl->microphone.Close();
    impl->ar_file.reset();
    impl->loaded = false;
    impl->audio_scratch.clear();
}

void NdsCore::Reset() {
    if (!impl->loaded || !impl->nds) {
        return;
    }
    impl->FlushPendingSaves(true);
    impl->nds->Reset();
    SetSystemRtc(*impl->nds);
    impl->nds->SetupDirectBoot(impl->rom_filename);
    impl->nds->Start();
    impl->rumble_until_ms.store(0, std::memory_order_relaxed);
}

void NdsCore::SetInput(const Common::AltCore::InputState& input) {
    impl->input = input;
    impl->mic_blow_button.store((input.buttons & Common::AltCore::BtnMic) != 0,
                                std::memory_order_relaxed);
}

void NdsCore::RunFrame(FrameOutput& out) {
    out.primary = {};
    out.secondary = {};
    out.audio_pcm.clear();
    out.rumble = 0.0f;
    out.metrics_changed = false;

    if (!impl->loaded || !impl->nds) {
        return;
    }

    // melonDS key mask is 12-bit active-LOW (0 = pressed, 1 = released).
    // Common::AltCore::ButtonBit bits 0..11 map 1:1 to A,B,Sel,Start,R,L,U,D,Rbtn,Lbtn,X,Y.
    const uint32_t ds_mask = (~(impl->input.buttons & 0xFFFu)) & 0xFFFu;
    impl->nds->SetKeyMask(ds_mask);

    if (impl->touch_enabled && impl->input.touching) {
        const auto tx = static_cast<melonDS::u16>(std::min<uint16_t>(impl->input.touch_x, kDsWidth - 1));
        const auto ty = static_cast<melonDS::u16>(std::min<uint16_t>(impl->input.touch_y, kDsHeight - 1));
        impl->nds->TouchScreen(tx, ty);
    } else {
        impl->nds->ReleaseScreen();
    }

    impl->nds->RunFrame();

    void* top_fb = nullptr;
    void* bot_fb = nullptr;
    if (impl->nds->GPU.GetFramebuffers(&top_fb, &bot_fb)) {
        if (top_fb) {
            out.primary.pixels = static_cast<const uint32_t*>(top_fb);
            out.primary.width = kDsWidth;
            out.primary.height = kDsHeight;
            out.primary.stride_pixels = kDsWidth;
            out.primary.bgra = true;
        }
        if (bot_fb) {
            out.secondary.pixels = static_cast<const uint32_t*>(bot_fb);
            out.secondary.width = kDsWidth;
            out.secondary.height = kDsHeight;
            out.secondary.stride_pixels = kDsWidth;
            out.secondary.bgra = true;
        }
    }

    const int max_frames = static_cast<int>(impl->audio_scratch.size() / 2u);
    const int got = impl->nds->SPU.ReadOutput(impl->audio_scratch.data(), max_frames);
    if (got > 0) {
        out.audio_pcm.assign(impl->audio_scratch.data(),
                             impl->audio_scratch.data() + static_cast<std::size_t>(got) * 2u);
    }

    const int64_t until = impl->rumble_until_ms.load(std::memory_order_relaxed);
    if (until > 0 && Impl::NowMs() < until) {
        out.rumble = 1.0f;
    } else if (until > 0) {
        impl->rumble_until_ms.store(0, std::memory_order_relaxed);
    }

    if (impl->metrics_dirty) {
        impl->metrics_dirty = false;
        out.metrics_changed = true;
        out.updated_metrics = impl->caps.screens;
    }

    impl->microphone.EndFrame();
    impl->FlushPendingSaves(false);
}

Common::AltCore::Capabilities NdsCore::GetCapabilities() const {
    return impl->caps;
}

bool NdsCore::SaveStateToMemory(std::vector<uint8_t>& out) {
    if (!impl->loaded || !impl->nds) {
        return false;
    }
    melonDS::Savestate state(melonDS::Savestate::DEFAULT_SIZE);
    impl->nds->GetRenderer().PreSavestate();
    const bool ok = impl->nds->DoSavestate(&state) && !state.Error;
    impl->nds->GetRenderer().PostSavestate();
    if (!ok || state.Length() == 0) {
        out.clear();
        return false;
    }
    const auto* ptr = static_cast<const uint8_t*>(state.Buffer());
    out.assign(ptr, ptr + state.Length());
    return true;
}

bool NdsCore::LoadStateFromMemory(const uint8_t* data, std::size_t size) {
    if (!impl->loaded || !impl->nds || !data || size == 0) {
        return false;
    }
    impl->FlushPendingSaves(true); // what the game saved before the load
    std::vector<uint8_t> copy(data, data + size);
    melonDS::Savestate state(copy.data(), static_cast<melonDS::u32>(copy.size()), false);
    impl->nds->GetRenderer().PreSavestate();
    const bool ok = impl->nds->DoSavestate(&state) && !state.Error;
    impl->nds->GetRenderer().PostSavestate();
    {
        // melonDS reports the state's save data as a save. The save file changes only when the
        // game saves, as for the other consoles: a load never replaces a newer save.
        std::lock_guard lock(impl->save_mutex);
        impl->nds_save_dirty = false;
        impl->gba_save_dirty = false;
    }
    return ok;
}

bool NdsCore::SaveStateToFile(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!SaveStateToMemory(buf)) {
        return false;
    }
    return AtomicWriteFile(path, buf.data(), buf.size(), true);
}

bool NdsCore::LoadStateFromFile(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf)) {
        return false;
    }
    return LoadStateFromMemory(buf.data(), buf.size());
}

bool NdsCore::ImportBatterySave(const std::string& sav_path) {
    if (!impl->loaded || !impl->nds || sav_path.empty()) {
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(sav_path, bytes)) {
        return false;
    }
    if (!AtomicWriteFile(impl->save_path, bytes.data(), bytes.size(), true)) {
        return false;
    }
    impl->nds->SetNDSSave(bytes.data(), static_cast<melonDS::u32>(bytes.size()));
    Reset();
    return true;
}

void NdsCore::FlushBatterySave() {
    if (impl) {
        impl->FlushPendingSaves(true);
    }
}

std::vector<CheatEntry> NdsCore::ListCheats() const {
    std::vector<CheatEntry> list;
    if (!impl->nds) {
        return list;
    }
    list.reserve(impl->nds->AREngine.Cheats.size());
    for (const auto& ch : impl->nds->AREngine.Cheats) {
        list.push_back(CheatEntry{ch.Name, FormatArCodeHex(ch.Code), ch.Enabled});
    }
    return list;
}

bool NdsCore::SetCheatEnabled(std::size_t idx, bool on) {
    if (!impl->nds || idx >= impl->nds->AREngine.Cheats.size()) {
        return false;
    }
    impl->nds->AREngine.Cheats[idx].Enabled = on;
    return true;
}

bool NdsCore::AddCheat(const std::string& name, const std::string& code) {
    if (!impl->nds) {
        return false;
    }
    auto words = ParseArCodeHex(code);
    if (words.empty()) {
        return false;
    }
    melonDS::ARCode ar{};
    ar.Parent = nullptr;
    ar.Name = name.empty() ? "Action Replay Cheat" : name;
    ar.Enabled = true;
    ar.Code = std::move(words);
    impl->nds->AREngine.Cheats.push_back(std::move(ar));

    if (impl->ar_file) {
        impl->ar_file->RootCat.Children.emplace_back(impl->nds->AREngine.Cheats.back());
        impl->ar_file->Save();
    }
    return true;
}

void NdsCore::SetMicrophone(MicrophoneFactory factory) {
    impl->microphone.SetFactory(std::move(factory));
}

void NdsCore::ReloadMicrophone() {
    impl->microphone.Reload();
}

void NdsCore::CloseMicrophone() {
    impl->microphone.Close();
}

void NdsCore::SetMicBlow(bool on) {
    impl->mic_blow_toggle.store(on, std::memory_order_relaxed);
}

void NdsCore::SetLidClosed(bool closed) {
    if (impl->nds) {
        impl->nds->SetLidClosed(closed);
    }
}

void NdsCore::SetTouchEnabled(bool on) {
    impl->touch_enabled = on;
    if (!on && impl->nds) {
        impl->nds->ReleaseScreen();
    }
}

void NdsCore::SetScreenRotation(int deg) {
    int norm = ((deg % 360) + 360) % 360;
    if (norm != 90 && norm != 270) {
        norm = 0;
    }
    if (norm != impl->rotation_deg) {
        impl->rotation_deg = norm;
        impl->caps.screens.rotation_deg = norm;
        impl->metrics_dirty = true;
    }
}

bool NdsCore::SetGbaSlotRom(const std::string& path) {
    if (!impl->loaded || !impl->nds || impl->is_dsi) {
        return false;
    }
    impl->opts.gba_slot_rom = path;
    return impl->LoadGbaCartInternal(path, this);
}

void NdsCore::SetConsoleMode(bool dsi) {
    if (!impl->loaded || dsi == impl->is_dsi) {
        return;
    }
    const std::string rom = impl->rom_path;
    const CorePaths paths = impl->paths;
    CoreBootOptions opts = impl->opts;
    opts.force_dsi = dsi;
    opts.force_nds = !dsi;
    Load(rom, paths, opts);
}

void NdsCore::OnSignalStop(int reason) {
    impl->stop_signalled.store(true, std::memory_order_relaxed);
    LOG_INFO(Core, "NdsCore: console stop signalled (reason={})", reason);
}

void NdsCore::OnWriteNDSSave(const uint8_t* data, uint32_t len, uint32_t /*offset*/,
                             uint32_t /*write_len*/) {
    if (!data || len == 0) {
        return;
    }
    std::lock_guard lock(impl->save_mutex);
    impl->pending_nds_save.assign(data, data + len);
    if (!impl->nds_save_dirty) {
        impl->nds_save_dirty = true;
        impl->nds_save_dirty_since_ms = Impl::NowMs();
    }
}

void NdsCore::OnWriteGBASave(const uint8_t* data, uint32_t len, uint32_t /*offset*/,
                             uint32_t /*write_len*/) {
    if (!data || len == 0) {
        return;
    }
    std::lock_guard lock(impl->save_mutex);
    impl->pending_gba_save.assign(data, data + len);
    impl->gba_save_dirty = true;
}

void NdsCore::OnWriteFirmware(const uint8_t* /*data*/, uint32_t /*len*/, uint32_t /*offset*/,
                              uint32_t /*write_len*/) {
    // Generated/user firmware writes are kept in memory for the session.
}

void NdsCore::OnMicStart() {
    impl->microphone.Start();
}

void NdsCore::OnMicStop() {
    impl->microphone.Stop();
}

int NdsCore::OnMicReadInput(int16_t* data, int max_length) {
    if (!data || max_length <= 0) {
        return 0;
    }
    const bool blowing = impl->mic_blow_toggle.load(std::memory_order_relaxed) ||
                         impl->mic_blow_button.load(std::memory_order_relaxed);
    if (!blowing) {
        return impl->microphone.Read(data, max_length);
    }

    // Synthesize smooth band-limited blow noise via xorshift32 + 1-pole low-pass filter
    uint32_t x = impl->mic_rng_state;
    int32_t lp = impl->mic_filter;
    for (int i = 0; i < max_length; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const int32_t white = static_cast<int32_t>(static_cast<int16_t>(x & 0xFFFFu)) / 2;
        lp = (lp * 3 + white) / 4;
        data[i] = static_cast<int16_t>(std::clamp(lp, -32767, 32767));
    }
    impl->mic_rng_state = x;
    impl->mic_filter = lp;
    return max_length;
}

void NdsCore::OnRumbleStart(uint32_t duration_ms) {
    const int64_t dur = duration_ms > 0 ? static_cast<int64_t>(duration_ms) : 100;
    impl->rumble_until_ms.store(Impl::NowMs() + dur, std::memory_order_relaxed);
}

void NdsCore::OnRumbleStop() {
    impl->rumble_until_ms.store(0, std::memory_order_relaxed);
}

} // namespace FrameCores
