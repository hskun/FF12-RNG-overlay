#include "dmnt_cht.hpp"

#include <cstring>
#include <cstdio>

namespace ff12 {
namespace dmnt {
namespace {

Service g_service{};
bool g_initialized = false;

constexpr u32 CMD_HAS_PROCESS       = 65000;
constexpr u32 CMD_GET_METADATA      = 65002;
constexpr u32 CMD_FORCE_OPEN        = 65003;
constexpr u32 CMD_READ_MEMORY       = 65102;
constexpr u32 CMD_WRITE_MEMORY      = 65103;

} // namespace

Result Initialize() {
    if (g_initialized)
        return 0;

    Result rc = smGetService(&g_service, "dmnt:cht");
    if (R_SUCCEEDED(rc))
        g_initialized = true;
    return rc;
}

void Exit() {
    if (!g_initialized)
        return;
    serviceClose(&g_service);
    std::memset(&g_service, 0, sizeof(g_service));
    g_initialized = false;
}

bool IsInitialized() {
    return g_initialized;
}

Result HasCheatProcess(bool& out) {
    if (!g_initialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    u8 value = 0;
    Result rc = serviceDispatchOut(&g_service, CMD_HAS_PROCESS, value);
    if (R_SUCCEEDED(rc))
        out = (value != 0);
    return rc;
}

Result ForceOpenCheatProcess() {
    if (!g_initialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_service, CMD_FORCE_OPEN);
}

Result GetMetadata(DmntCheatProcessMetadata& out) {
    if (!g_initialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatchOut(&g_service, CMD_GET_METADATA, out);
}

Result Read(u64 address, void* buffer, size_t size) {
    if (!g_initialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (buffer == nullptr || size == 0)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    struct In {
        u64 address;
        u64 out_size;
    } in{address, static_cast<u64>(size)};

    // ReadCheatProcessMemory(address, out_size) -> OutBuffer.
    return serviceDispatchIn(&g_service, CMD_READ_MEMORY, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { buffer, size } }
    );
}

Result Write(u64 address, const void* buffer, size_t size) {
    if (!g_initialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (buffer == nullptr || size == 0)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    struct In {
        u64 address;
        u64 in_size;
    } in{address, static_cast<u64>(size)};

    // WriteCheatProcessMemory(address, in_size) -> InBuffer.
    return serviceDispatchIn(&g_service, CMD_WRITE_MEMORY, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { const_cast<void*>(buffer), size } }
    );
}

} // namespace dmnt

std::string FormatBuildId(const u8 build_id[0x20]) {
    std::string out;
    out.reserve(0x20 * 2);
    for (size_t i = 0; i < 0x20; ++i) {
        char byte[3];
        std::snprintf(byte, sizeof(byte), "%02X", build_id[i]);
        out += byte;
    }
    return out;
}

} // namespace ff12
