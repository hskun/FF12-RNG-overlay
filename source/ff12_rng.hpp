#pragma once

#include "config.hpp"
#include "dmnt_cht.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ff12 {

constexpr size_t MT_N = 624;
constexpr size_t MT_BYTES = MT_N * sizeof(uint32_t);
constexpr uint64_t INVALID_MATCH = UINT64_MAX;

struct RngSnapshot {
    bool valid = false;
    uint64_t process_id = 0;
    uint64_t title_id = 0;
    uint64_t main_base = 0;
    uint64_t pointer_slot = 0;
    uint64_t pointer_value = 0;
    uint64_t object_address = 0;
    uint64_t state_address = 0;
    uint64_t index_address = 0;
    uint64_t rng_address = 0;
    uint32_t index = 0;
    std::array<uint32_t, MT_N> state{};
    uint32_t next_raw = 0;
    uint32_t current_percent = 0;
    uint64_t observed_consumed = 0;
    bool coherent = true;
};

struct Prediction {
    uint32_t raw = 0;
    uint32_t percent = 0;
};

class Mt19937 {
public:
    static constexpr uint32_t MATRIX_A = 0x9908B0DFU;
    static constexpr uint32_t UPPER_MASK = 0x80000000U;
    static constexpr uint32_t LOWER_MASK = 0x7FFFFFFFU;

    static void Twist(std::array<uint32_t, MT_N>& state);
    static uint32_t Temper(uint32_t y);
    static uint32_t Untemper(uint32_t value);
    static Prediction Peek(const RngSnapshot& snapshot, size_t ahead);
};

class RngTracker {
public:
    bool Configure(const Config& cfg);
    Result Refresh(const DmntCheatProcessMetadata& meta);

    const RngSnapshot& Snapshot() const { return m_snapshot; }
    const std::string& Error() const { return m_error; }

    double SuccessProbability() const;
    bool IsSuccessPercent(uint32_t percent) const;
    uint64_t GetNextMatch(size_t max_positions) const;
    Result LockPercentages(const std::vector<uint32_t>& percentages);
    Result MaintainLock();
    void Unlock();
    bool LockActive() const { return m_lock_active; }
    const std::vector<uint32_t>& LockPattern() const { return m_lock_pattern; }
    const std::string& LockError() const { return m_lock_error; }

private:
    bool AddressInRange(uint64_t address, size_t size,
                        const DmntCheatProcessMetadata& meta) const;
    Result ResolveAddresses(const DmntCheatProcessMetadata& meta,
                            uint64_t& object_address);
    Result ReadRng(const DmntCheatProcessMetadata& meta, uint64_t rng_address);

    Config m_cfg{};
    RngSnapshot m_snapshot{};
    std::string m_error;
    uint64_t m_prev_rng_address = 0;
    uint32_t m_prev_index = 0;
    std::array<uint32_t, MT_N> m_prev_state{};
    bool m_have_prev = false;
    bool m_lock_active = false;
    std::vector<uint32_t> m_lock_pattern;
    std::string m_lock_error;
};

} // namespace ff12
