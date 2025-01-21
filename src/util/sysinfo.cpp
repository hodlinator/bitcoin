// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h>
#include <cstdlib>
#include <optional>
#include <util/sysinfo.h>

#if defined(__linux__)

#include <sys/sysinfo.h>

std::optional<RAMInfo> QueryRAMInfo()
{
    struct sysinfo info;
    if (sysinfo(&info) != EXIT_SUCCESS) {
    	return std::nullopt;
    }

    return RAMInfo{.total = info.totalram, .free = info.freeram };
}

#elif defined(__APPLE__)

#include <mach/mach.h>
#include <mach/mach_vm.h>

std::optional<RAMInfo> QueryRAMInfo()
{
    mach_port_t host_port = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm;

    vm_size_t page_size = 0;
    if (host_page_size(host_port, &page_size) != KERN_SUCCESS) {
        return std::nullopt;
    }

    if (host_statistics64(host_port, HOST_VM_INFO, (host_info64_t)&vm, &count) != KERN_SUCCESS) {
        return std::nullopt;
    }

    return RAMInfo{.total = (vm.wire_count + vm.active_count + vm.inactive_count + vm.free_count) * page_size, .free = vm.free_count * page_size };
}

#elif defined(HAVE_SYSCTL)

#include <sys/sysctl.h>

#if defined(CTL_VM)

std::optional<RAMInfo> QueryRAMInfo()
{
    int mib[2];
    vm_stat vm;
    const size_t len = sizeof(vm);
    mib[0] = CTL_VM;
    mib[1] = VM_STAT;
    if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &vm, &len, nullptr, 0) != EXIT_SUCCESS) {
        return std::nullopt;
    }

    return RAMInfo{.total = vm.v_page_count * vm.v_page_size, .free = (vm.v_free_count + vm.v_cache_count) * vm.v_page_size };
}

#else
#error "sysctl but CTL_VM not implemented for platform"
#endif
#elif defined(WIN32)

#include <windows.h>
#include <psapi.h>

std::optional<RAMInfo> QueryRAMInfo()
{
    PERFORMANCE_INFORMATION info;
    if (!GetPerformanceInfo(&info, sizeof(info))) {
        return std::nullopt;
    }

    return RAMInfo{.total = info.PhysicalTotal * info.PageSize, .free = info.PhysicalAvailable * info.PageSize };
}

#else
#error "Unimplemented platform"
#endif
