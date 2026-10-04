#pragma once

#include <switch.h>
#include <cstddef>
#include <cstdint>
#include <string>

// Minimal client-side view of Atmosphere's dmnt:cht service.
// API IDs follow Atmosphere's documented CheatService interface.

namespace ff12 {

struct DmntMemoryRegionExtents {
    u64 base;
    u64 size;
};

struct DmntCheatProcessMetadata {
    u64 process_id;
    u64 title_id;
    DmntMemoryRegionExtents main_nso_extents;
    DmntMemoryRegionExtents heap_extents;
    DmntMemoryRegionExtents alias_extents;
    DmntMemoryRegionExtents address_space_extents;
    u8 main_nso_build_id[0x20];
};

std::string FormatBuildId(const u8 build_id[0x20]);

namespace dmnt {

Result Initialize();
void Exit();
bool IsInitialized();

Result HasCheatProcess(bool& out);
Result ForceOpenCheatProcess();
Result GetMetadata(DmntCheatProcessMetadata& out);
Result Read(u64 address, void* buffer, size_t size);
Result Write(u64 address, const void* buffer, size_t size);

} // namespace dmnt

} // namespace ff12
