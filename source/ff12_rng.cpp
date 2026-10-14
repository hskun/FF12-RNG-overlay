#include "ff12_rng.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace ff12 {

void Mt19937::Twist(std::array<uint32_t, MT_N>& state) {
    for (size_t i = 0; i < MT_N; ++i) {
        const uint32_t y = (state[i] & UPPER_MASK) | (state[(i + 1) % MT_N] & LOWER_MASK);
        state[i] = state[(i + 397) % MT_N] ^ (y >> 1) ^ ((y & 1U) ? MATRIX_A : 0U);
    }
}

uint32_t Mt19937::Temper(uint32_t y) {
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    y ^= (y >> 18);
    return y;
}

uint32_t Mt19937::Untemper(uint32_t value) {
    uint32_t c = value;
    for (int i = 0; i < 3; ++i)
        c = value ^ (c >> 18);

    uint32_t b = c;
    for (int i = 0; i < 3; ++i)
        b = c ^ ((b << 15) & 0xEFC60000U);

    uint32_t a = b;
    for (int i = 0; i < 5; ++i)
        a = b ^ ((a << 7) & 0x9D2C5680U);

    uint32_t s = a;
    for (int i = 0; i < 5; ++i)
        s = a ^ (s >> 11);
    return s;
}

Prediction Mt19937::Peek(const RngSnapshot& snapshot, size_t ahead) {
    std::array<uint32_t, MT_N> tmp = snapshot.state;
    uint32_t idx = snapshot.index;

    if (idx > MT_N)
        idx = MT_N;

    for (size_t n = 0; n <= ahead; ++n) {
        if (idx >= MT_N) {
            Twist(tmp);
            idx = 0;
        }
        const uint32_t raw = Temper(tmp[idx++]);
        if (n == ahead)
            return { raw, raw % 100U };
    }

    return {};
}

bool RngTracker::Configure(const Config& cfg) {
    m_cfg = cfg;
    m_snapshot = {};
    m_have_prev = false;
    m_error.clear();
    return true;
}

bool RngTracker::AddressInRange(uint64_t address, size_t size,
                                const DmntCheatProcessMetadata& meta) const {
    const uint64_t space_base = meta.address_space_extents.base;
    const uint64_t space_end = space_base + meta.address_space_extents.size;
    const uint64_t address_end = address + size;

    return address >= space_base && address_end > address &&
           address_end <= space_end;
}

Result RngTracker::ResolveAddresses(const DmntCheatProcessMetadata& meta,
                                    uint64_t& object_address) {
    object_address = 0;
    m_snapshot.pointer_slot = 0;
    m_snapshot.pointer_value = 0;
    m_snapshot.object_address = 0;

    uint64_t base_address = 0;
    bool dereference = false;
    if (m_cfg.rng_base == "main_pointer" || m_cfg.rng_base == "main") {
        base_address = meta.main_nso_extents.base;
        dereference = m_cfg.rng_base == "main_pointer";
    } else if (m_cfg.rng_base == "heap") {
        base_address = meta.heap_extents.base;
    } else if (m_cfg.rng_base == "absolute") {
        base_address = 0;
    } else {
        m_error = "invalid rng_base";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const uint64_t slot_address = base_address + m_cfg.rng_offset;
    const bool pointer_is_64bit =
        m_cfg.pointer_size == "64" || m_cfg.pointer_size == "auto";
    const size_t pointer_size = pointer_is_64bit ? sizeof(uint64_t) : sizeof(uint32_t);
    if (slot_address < base_address ||
        (dereference && !AddressInRange(slot_address, pointer_size, meta))) {
        m_error = "pointer slot outside process space";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    uint64_t resolved_address = slot_address;
    if (dereference) {
        uint8_t pointer_bytes[sizeof(uint64_t)]{};
        const Result rc = dmnt::Read(slot_address, pointer_bytes, pointer_size);
        if (R_FAILED(rc)) {
            m_error = "pointer read failed";
            return rc;
        }
        m_snapshot.pointer_slot = slot_address;

        uint64_t pointer_value = 0;
        if (pointer_is_64bit)
            std::memcpy(&pointer_value, pointer_bytes, sizeof(pointer_value));
        else {
            uint32_t value32 = 0;
            std::memcpy(&value32, pointer_bytes, sizeof(value32));
            pointer_value = value32;
        }
        if (pointer_value == 0) {
            m_error = "null RNG pointer";
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        if (!AddressInRange(pointer_value, 1, meta)) {
            m_error = "RNG pointer outside process space";
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        resolved_address = pointer_value;
        m_snapshot.pointer_value = pointer_value;
    }

    const uint64_t state_address = resolved_address + m_cfg.state_offset;
    const uint64_t index_address = resolved_address + m_cfg.index_offset;
    if (state_address < resolved_address || index_address < resolved_address) {
        m_error = "RNG offsets overflow";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    m_snapshot.state_address = state_address;
    m_snapshot.index_address = index_address;
    m_snapshot.object_address = resolved_address;
    m_snapshot.rng_address = resolved_address;
    object_address = resolved_address;

    if (!AddressInRange(state_address, MT_BYTES, meta) ||
        !AddressInRange(index_address, sizeof(uint32_t), meta)) {
        m_error = "RNG address outside process space";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    return 0;
}

Result RngTracker::ReadRng(const DmntCheatProcessMetadata& meta, uint64_t rng_address) {
    const RngSnapshot previous_snapshot = m_snapshot;
    const uint64_t state_end = static_cast<uint64_t>(m_cfg.state_offset) + MT_BYTES;
    const uint64_t index_end = static_cast<uint64_t>(m_cfg.index_offset) + sizeof(uint32_t);
    const uint64_t lo64 = std::min<uint64_t>(m_cfg.state_offset, m_cfg.index_offset);
    const uint64_t hi64 = std::max(state_end, index_end);

    if (hi64 <= lo64 || hi64 - lo64 > 0x10000ULL) {
        m_error = "invalid RNG field span";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const size_t span = static_cast<size_t>(hi64 - lo64);
    std::vector<uint8_t> data(span);
    Result rc = dmnt::Read(rng_address + lo64, data.data(), data.size());
    if (R_FAILED(rc)) {
        m_error = "dmnt read failed";
        return rc;
    }

    const size_t idx_pos = static_cast<size_t>(m_cfg.index_offset - lo64);
    uint32_t idx_word = 0;
    std::memcpy(&idx_word, data.data() + idx_pos, sizeof(idx_word));
    const int32_t mti = static_cast<int32_t>(idx_word);
    if (mti < 0 || mti > static_cast<int32_t>(MT_N + 1)) {
        m_error = "invalid MT19937 index";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    const uint32_t idx = static_cast<uint32_t>(mti);

    RngSnapshot next{};
    next.pointer_slot = previous_snapshot.pointer_slot;
    next.pointer_value = previous_snapshot.pointer_value;
    next.object_address = previous_snapshot.object_address;
    next.state_address = previous_snapshot.state_address;
    next.index_address = previous_snapshot.index_address;
    next.valid = true;
    next.process_id = meta.process_id;
    next.title_id = meta.title_id;
    next.main_base = meta.main_nso_extents.base;
    next.rng_address = rng_address;
    next.index = idx;

    const size_t state_pos = static_cast<size_t>(m_cfg.state_offset - lo64);
    std::memcpy(next.state.data(), data.data() + state_pos, MT_BYTES);

    const Prediction p0 = Mt19937::Peek(next, 0);
    next.next_raw = p0.raw;
    next.current_percent = p0.percent;

    if (m_have_prev && m_prev_rng_address == rng_address) {
        const uint32_t delta = (idx >= m_prev_index)
            ? (idx - m_prev_index)
            : static_cast<uint32_t>(MT_N - m_prev_index + idx);

        // With normal polling intervals the expected delta is small. If the
        // state array stayed identical, the cursor moved without twisting.
        // If it changed, a twist/wrap occurred (or the target is not the real
        // MT object). Mark the latter as a soft coherence warning rather than
        // dropping the sample.
        const bool state_same = std::memcmp(next.state.data(), m_prev_state.data(), MT_BYTES) == 0;
        const bool wrapped = idx < m_prev_index;
        next.coherent = state_same ? !wrapped : wrapped;
        next.observed_consumed = m_snapshot.observed_consumed + delta;
    }

    m_snapshot = next;
    m_prev_rng_address = rng_address;
    m_prev_index = idx;
    m_prev_state = next.state;
    m_have_prev = true;
    m_error.clear();
    return 0;
}

Result RngTracker::Refresh(const DmntCheatProcessMetadata& meta) {
    if (meta.title_id != m_cfg.title_id) {
        m_snapshot = {};
        m_error = "title mismatch";
        return 0;
    }

    uint64_t object_address = 0;
    const Result resolve_rc = ResolveAddresses(meta, object_address);
    if (R_FAILED(resolve_rc)) {
        m_snapshot = {};
        return resolve_rc;
    }

    return ReadRng(meta, object_address);
}

Result RngTracker::LockPercentage(uint32_t percentage) {
    m_lock_error.clear();
    if (percentage > 99) {
        m_lock_error = "percent must be 0..99";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    if (!m_snapshot.valid) {
        m_lock_error = "RNG not online";
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    uint32_t idx = m_snapshot.index;
    if (idx > MT_N)
        idx = MT_N;

    std::array<uint32_t, MT_N> state = m_snapshot.state;
    for (size_t i = 0; i < MT_N; ++i) {
        const size_t state_index = (idx + i) % MT_N;
        state[state_index] = Mt19937::Untemper(
            percentage + 100U * (Mt19937::Temper(state[state_index]) / 100U));
    }

    const Result state_rc = dmnt::Write(
        m_snapshot.state_address, state.data(), MT_BYTES);
    if (R_FAILED(state_rc)) {
        m_lock_error = "state write failed";
        return state_rc;
    }

    uint32_t index_value = idx == MT_N ? 0 : idx;
    const Result index_rc = dmnt::Write(
        m_snapshot.index_address, &index_value, sizeof(index_value));
    if (R_FAILED(index_rc)) {
        m_lock_error = "index write failed";
        Unlock();
        return index_rc;
    }

    m_lock_percentage = percentage;
    m_lock_active = true;
    return 0;
}

Result RngTracker::MaintainLock() {
    if (!m_lock_active)
        return 0;
    return LockPercentage(m_lock_percentage);
}

void RngTracker::Unlock() {
    m_lock_active = false;
    m_lock_percentage = 0;
    m_lock_error.clear();
}

double RngTracker::SuccessProbability() const {
    const uint32_t count = m_cfg.success_max - m_cfg.success_min + 1U;
    return static_cast<double>(count);
}

bool RngTracker::IsSuccessPercent(uint32_t percent) const {
    return percent >= m_cfg.success_min && percent <= m_cfg.success_max;
}

uint64_t RngTracker::GetNextMatch(size_t max_positions) const {
    if (!m_snapshot.valid) return INVALID_MATCH;

    for (size_t i = 0; i < max_positions; ++i) {
        const Prediction p = Mt19937::Peek(m_snapshot, i);
        if (IsSuccessPercent(p.percent)) return static_cast<uint64_t>(i);
    }
    return INVALID_MATCH;
}

} // namespace ff12
