#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ff12 {

struct Config {
    uint64_t title_id = 0x0100EB100AB42000ULL;
    std::string rng_base = "main_pointer";
    std::string pointer_size = "64";
    uint64_t rng_offset = 0x0178F1A0ULL;
    uint32_t state_offset = 0x00000258U;
    uint32_t index_offset = 0x00000C18U;
    uint32_t poll_ms = 100;
    uint32_t preview_count = 24;
    uint32_t success_min = 0;
    uint32_t success_max = 0;
};

bool LoadConfig(const char* path, Config& cfg, std::string& error);

} // namespace ff12
