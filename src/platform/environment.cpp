// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/environment.h"

#include "platform/text.h"

#include <win32ui/system_api.h>

#include <intrin.h>

#include <cstdio>

namespace thread_sim::platform
{

namespace
{

std::wstring RegistryString(HKEY root, const wchar_t* path, const wchar_t* name)
{
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
    {
        return {};
    }
    return Trim(buffer);
}

std::optional<DWORD> RegistryDword(HKEY root, const wchar_t* path, const wchar_t* name)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

Environment QueryEnvironment()
{
    Environment environment;
    const auto& version = win32ui::OsVersion();
    if (version.dwMajorVersion != 0)
    {
        const auto ubr = RegistryDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR");
        wchar_t buffer[64] = {};
        if (ubr)
        {
            swprintf_s(buffer, L"%lu.%lu.%lu.%lu", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber, *ubr);
        }
        else
        {
            swprintf_s(buffer, L"%lu.%lu.%lu", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
        }
        environment.windows_version = buffer;
        environment.windows_build = version.dwBuildNumber;
    }
    environment.windows_display = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    environment.processor_name = RegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");

    SYSTEM_INFO info = {};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        environment.architecture = L"x64";
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        environment.architecture = L"arm64";
        break;
    default:
        environment.architecture = L"other";
        break;
    }

    environment.process_id = GetCurrentProcessId();
    const DWORD priority_class = GetPriorityClass(GetCurrentProcess());
    if (priority_class != 0)
    {
        environment.priority_class = priority_class;
    }
    SYSTEM_POWER_STATUS power = {};
    if (GetSystemPowerStatus(&power) && power.ACLineStatus != 255)
    {
        environment.on_ac_power = power.ACLineStatus == 1;
    }
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
    {
        environment.process_affinity_mask = process_mask;
        environment.system_affinity_mask = system_mask;
    }
    USHORT groups[64] = {};
    USHORT group_count = static_cast<USHORT>(std::size(groups));
    if (GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups))
    {
        environment.process_group_count = group_count;
    }
    int registers[4] = {};
    __cpuid(registers, 1);
    environment.hypervisor_present_cpuid = (static_cast<unsigned>(registers[2]) & (1u << 31)) != 0;

    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD length = static_cast<DWORD>(std::size(name));
    if (GetComputerNameW(name, &length))
    {
        uint64_t hash = 1469598103934665603ull;
        for (DWORD index = 0; index < length; ++index)
        {
            hash = (hash ^ static_cast<uint16_t>(name[index])) * 1099511628211ull;
        }
        environment.computer_name_hash = FormatHex(hash, 16);
    }
    return environment;
}

const wchar_t* PriorityClassKey(uint32_t priority_class)
{
    switch (priority_class)
    {
    case IDLE_PRIORITY_CLASS:
        return L"idle";
    case BELOW_NORMAL_PRIORITY_CLASS:
        return L"below_normal";
    case NORMAL_PRIORITY_CLASS:
        return L"normal";
    case ABOVE_NORMAL_PRIORITY_CLASS:
        return L"above_normal";
    case HIGH_PRIORITY_CLASS:
        return L"high";
    case REALTIME_PRIORITY_CLASS:
        return L"realtime";
    default:
        return L"unknown";
    }
}

} // namespace thread_sim::platform
