// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiurange: "Shot Range" for Kid Icarus: Uprising, the pure logic. The game parses the
// equipped weapon's shot tables (player/m*.zrc json 01..04) into heap objects of one class and reads
// the range from them when a shot flies. This finds those objects in guest memory, remembers their
// original values and writes original * factor, so every shot keeps its ratio to the others. No
// emulator or OpenXR types: the caller gives a Memory (guest reads and writes). Nothing here is
// hooked into the frame loop yet. Unit-tested over a fake memory (src/tests/frame_xr/kiu_range.cpp).
//
// Layout of one shot table (code.bin md5 b107c9b1, EUR original; offsets from the object):
//   +0x00 vtable (kTableVtable)       +0x18 u8 exit condition (1 = length)
//   +0x2C speed (float; +0x30 = x60)  +0x40 distance_ (float, plain)
// Rules: only tables whose exit condition is "length" with a distance > 0 are scaled (distance
// * factor). Time and no-exit rules are off until their enum values are verified (Rules).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FrameXr::KiuRange {

/// Guest memory as the app sees it. Both return false when any byte is unmapped.
class Memory {
public:
    virtual ~Memory() = default;
    virtual bool Read(std::uint32_t addr, void* out, std::size_t size) const = 0;
    virtual bool Write(std::uint32_t addr, const void* in, std::size_t size) = 0;
};

constexpr float kMinFactor = 1.0f;
constexpr float kMaxFactor = 3.0f;
constexpr float kDefaultFactor = 2.0f;

/// Where things are (EUR original). The addresses are per game version: another region needs its own.
struct Layout {
    std::uint32_t root_slot = 0xE704C0;     ///< global word: the loaded shot-table document's address
    std::uint32_t table_vtable = 0xCFEDD8;  ///< vtable of the shot-table class
    std::uint32_t scan_bytes = 0x20000;     ///< the document's arena, read from the root pointer
    std::uint32_t heap_lo = 0x14000000;     ///< the root pointer must lie in the game's linear heap
    std::uint32_t heap_hi = 0x18000000;
    std::uint32_t off_exit = 0x18;
    std::uint32_t off_exist_frame = 0x20;   ///< raw value; +4 is the derived copy
    std::uint32_t off_speed = 0x2C;
    std::uint32_t off_distance = 0x40;
};

/// Which exit conditions are scaled. -1 = not scaled. Only "length" (1) is verified.
struct Rules {
    int exit_length = 1;
    int exit_time = -1; ///< existFrame_ * factor when speed > 0 (enum value not verified: off)
    int exit_none = -1; ///< distance_ * factor with speed 0 (enum value not verified: off)
};

struct Table {
    std::uint32_t addr = 0;
    std::uint8_t exit = 0;
    float orig_distance = 0, orig_exist = 0, orig_exist2 = 0; ///< as the game parsed them
    std::uint32_t written_distance = 0, written_exist = 0, written_exist2 = 0; ///< bits we last wrote
    bool scales_distance = false, scales_exist = false;
};

inline std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline float FromBits(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

class Range {
public:
    explicit Range(Layout layout = {}, Rules rules = {}) : l_(layout), r_(rules) {}

    /// Finds the tables from the root pointer (guards: root in the heap, vtable, sane values). Keeps
    /// the originals of tables already known (a table found again keeps its remembered original
    /// unless the game rewrote it, see Apply). Returns the number of tables found.
    int Rescan(const Memory& mem) {
        std::vector<Table> found;
        std::uint32_t root = 0;
        if (!ReadU32(mem, l_.root_slot, root) || root < l_.heap_lo || root >= l_.heap_hi || (root & 3)) {
            tables_.clear();
            root_ = 0;
            return 0;
        }
        std::vector<std::uint8_t> arena(l_.scan_bytes);
        if (!mem.Read(root, arena.data(), arena.size())) {
            // the arena may end at the heap's edge: read what is mapped, a page at a time
            arena.clear();
            for (std::uint32_t o = 0; o < l_.scan_bytes; o += 0x1000) {
                std::uint8_t page[0x1000];
                if (!mem.Read(root + o, page, sizeof page)) {
                    break;
                }
                arena.insert(arena.end(), page, page + sizeof page);
            }
        }
        for (std::size_t o = 0; o + 4 <= arena.size(); o += 4) {
            std::uint32_t v;
            std::memcpy(&v, &arena[o], 4);
            if (v != l_.table_vtable) {
                continue;
            }
            Table t;
            if (Probe(mem, root + static_cast<std::uint32_t>(o), t)) {
                found.push_back(t);
            }
        }
        for (Table& n : found) {
            for (const Table& old : tables_) {
                if (old.addr == n.addr) {
                    n = Merge(old, n);
                    break;
                }
            }
        }
        tables_ = std::move(found);
        root_ = root;
        return static_cast<int>(tables_.size());
    }

    /// Writes original * factor into every known table. Idempotent. Call it on a battle load, when
    /// the factor changes, and now and then in a battle: a table the game parsed again (its value
    /// is neither the original we hold nor what we wrote) becomes the new original, so a reload
    /// never compounds. Rescans by itself when the root pointer moved or a table is gone (not when
    /// none were ever found under an unchanged root: call Rescan on a battle load for that).
    /// Returns the number of tables that carry the factor now.
    int Apply(Memory& mem, float factor) {
        factor = std::isfinite(factor) ? std::clamp(factor, kMinFactor, kMaxFactor) : kDefaultFactor;
        std::uint32_t root = 0;
        const bool root_ok = ReadU32(mem, l_.root_slot, root);
        if (!root_ok || root != root_) {
            Rescan(mem);
        }
        int applied = Pass(mem, factor);
        if (dropped_) {
            Rescan(mem); // a table vanished (a reload moved things): look again once
            applied = Pass(mem, factor);
        }
        factor_ = factor;
        return applied;
    }

    /// Puts the original values back (factor off, leaving a battle, shutting down) and forgets
    /// the tables whose values were restored.
    void Restore(Memory& mem) {
        for (Table& t : tables_) {
            Table cur;
            if (!Probe(mem, t.addr, cur)) {
                continue;
            }
            t = Merge(t, cur);
            if (t.scales_distance) {
                WriteF(mem, t.addr + l_.off_distance, t.orig_distance, t.written_distance);
            }
            if (t.scales_exist) {
                WriteF(mem, t.addr + l_.off_exist_frame, t.orig_exist, t.written_exist);
                WriteF(mem, t.addr + l_.off_exist_frame + 4, t.orig_exist2, t.written_exist2);
            }
        }
        tables_.clear();
        root_ = 0;
    }

    const std::vector<Table>& Tables() const { return tables_; }
    float Factor() const { return factor_; }

private:
    int Pass(Memory& mem, float factor) {
        int applied = 0;
        dropped_ = false;
        std::vector<Table> kept;
        for (Table& t : tables_) {
            Table cur;
            if (!Probe(mem, t.addr, cur)) {
                dropped_ = true; // gone (freed or rewritten): dropped, nothing written
                continue;
            }
            t = Merge(t, cur); // the game's current values that are not ours become the new originals
            bool ok = true;
            if (t.scales_distance) {
                ok = WriteF(mem, t.addr + l_.off_distance, t.orig_distance * factor, t.written_distance) && ok;
            }
            if (t.scales_exist) {
                ok = WriteF(mem, t.addr + l_.off_exist_frame, t.orig_exist * factor, t.written_exist) && ok;
                ok = WriteF(mem, t.addr + l_.off_exist_frame + 4, t.orig_exist2 * factor, t.written_exist2) && ok;
            }
            if (ok && (t.scales_distance || t.scales_exist)) {
                ++applied;
            }
            kept.push_back(t);
        }
        tables_ = std::move(kept);
        return applied;
    }

    static bool ReadU32(const Memory& m, std::uint32_t a, std::uint32_t& v) { return m.Read(a, &v, 4); }

    /// Reads and checks one table at addr; fills t with the values as they are in memory now.
    bool Probe(const Memory& mem, std::uint32_t addr, Table& t) const {
        std::uint32_t vt = 0;
        std::uint8_t exit = 0;
        std::uint32_t d = 0, e = 0, e2 = 0, s = 0;
        if (!ReadU32(mem, addr, vt) || vt != l_.table_vtable || !mem.Read(addr + l_.off_exit, &exit, 1) ||
            !ReadU32(mem, addr + l_.off_distance, d) || !ReadU32(mem, addr + l_.off_speed, s) ||
            !ReadU32(mem, addr + l_.off_exist_frame, e) || !ReadU32(mem, addr + l_.off_exist_frame + 4, e2)) {
            return false;
        }
        const float dist = FromBits(d), speed = FromBits(s), ex = FromBits(e);
        if (exit > 8 || !std::isfinite(dist) || !std::isfinite(speed) || !std::isfinite(ex) || dist < 0 ||
            dist > 20000.f || speed < 0 || speed > 2000.f || ex < 0 || ex > 100000.f) {
            return false;
        }
        t = Table{};
        t.addr = addr;
        t.exit = exit;
        t.orig_distance = dist;
        t.orig_exist = ex;
        t.orig_exist2 = FromBits(e2);
        t.scales_distance = (r_.exit_length >= 0 && exit == r_.exit_length && dist > 0) ||
                            (r_.exit_none >= 0 && exit == r_.exit_none && dist > 0 && speed == 0);
        t.scales_exist = r_.exit_time >= 0 && exit == r_.exit_time && speed > 0 && ex > 0;
        return true;
    }

    /// old = what we remember, now = what memory holds. A value equal to what we wrote is ours
    /// (keep the remembered original); anything else is the game's (a fresh parse): it is the new original.
    static Table Merge(const Table& old, const Table& now) {
        Table t = now;
        t.written_distance = old.written_distance;
        t.written_exist = old.written_exist;
        t.written_exist2 = old.written_exist2;
        if (old.written_distance != 0 && Bits(now.orig_distance) == old.written_distance) {
            t.orig_distance = old.orig_distance;
        }
        if (old.written_exist != 0 && Bits(now.orig_exist) == old.written_exist) {
            t.orig_exist = old.orig_exist;
        }
        if (old.written_exist2 != 0 && Bits(now.orig_exist2) == old.written_exist2) {
            t.orig_exist2 = old.orig_exist2;
        }
        return t;
    }

    static bool WriteF(Memory& mem, std::uint32_t addr, float v, std::uint32_t& written) {
        const std::uint32_t b = Bits(v);
        std::uint32_t cur = 0;
        if (!mem.Read(addr, &cur, 4)) {
            return false;
        }
        if (cur != b && !mem.Write(addr, &b, 4)) {
            return false;
        }
        written = b;
        return true;
    }

    Layout l_;
    Rules r_;
    std::vector<Table> tables_;
    std::uint32_t root_ = 0;
    float factor_ = kDefaultFactor;
    bool dropped_ = false;
};

} // namespace FrameXr::KiuRange
