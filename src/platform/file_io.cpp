// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/file_io.h"

#include "platform/text.h"

#include <win32ui/handle_owner.h>

#include <shlobj.h>

#include <cstdio>

namespace thread_sim::platform
{

Result<std::string> ReadFileBytes(const std::wstring& path, uint64_t max_bytes)
{
    win32ui::UniqueFile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        return LastWin32Error(L"CreateFileW").WithContext(path);
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file.get(), &size))
    {
        return LastWin32Error(L"GetFileSizeEx").WithContext(path);
    }
    if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > max_bytes)
    {
        return FormatError(L"Read file", L"The file is larger than the supported limit.").WithContext(path);
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1u << 20));
        DWORD read = 0;
        if (!ReadFile(file.get(), bytes.data() + offset, chunk, &read, nullptr))
        {
            return LastWin32Error(L"ReadFile").WithContext(path);
        }
        if (read == 0)
        {
            bytes.resize(offset);
            break;
        }
        offset += read;
    }
    return bytes;
}

Result<std::wstring> ReadUtf8File(const std::wstring& path, uint64_t max_bytes)
{
    auto bytes = ReadFileBytes(path, max_bytes);
    if (!bytes)
    {
        return std::move(bytes).error();
    }
    std::string_view view = bytes.value();
    if (view.size() >= 3 && static_cast<unsigned char>(view[0]) == 0xEF && static_cast<unsigned char>(view[1]) == 0xBB && static_cast<unsigned char>(view[2]) == 0xBF)
    {
        view.remove_prefix(3);
    }
    auto text = Utf8ToWide(view);
    if (!text)
    {
        return FormatError(L"Decode UTF-8", L"The file is not valid UTF-8 text.").WithContext(path);
    }
    return std::move(*text);
}

Status WriteFileAtomic(const std::wstring& path, std::string_view bytes)
{
    wchar_t suffix[40] = {};
    swprintf_s(suffix, L".%lu.%llu.tmp", GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring temporary = path + suffix;
    {
        win32ui::UniqueFile file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
        {
            return LastWin32Error(L"CreateFileW").WithContext(temporary);
        }
        size_t offset = 0;
        while (offset < bytes.size())
        {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1u << 20));
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
            {
                Error error = LastWin32Error(L"WriteFile").WithContext(temporary);
                file.reset();
                DeleteFileW(temporary.c_str());
                return error;
            }
            offset += written;
        }
        if (!FlushFileBuffers(file.get()))
        {
            Error error = LastWin32Error(L"FlushFileBuffers").WithContext(temporary);
            file.reset();
            DeleteFileW(temporary.c_str());
            return error;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        Error error = LastWin32Error(L"MoveFileExW").WithContext(path);
        DeleteFileW(temporary.c_str());
        return error;
    }
    return Status::Ok();
}

Status WriteUtf8FileAtomic(const std::wstring& path, std::wstring_view text)
{
    const std::string bytes = WideToUtf8(text);
    if (bytes.empty() && !text.empty())
    {
        return FormatError(L"Encode UTF-8", L"The text contains invalid UTF-16.").WithContext(path);
    }
    return WriteFileAtomic(path, bytes);
}

Status EnsureDirectory(const std::wstring& path)
{
    if (path.empty())
    {
        return ValidationError(L"Create directory", L"The directory path is empty.");
    }
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS)
    {
        return Win32Error(static_cast<DWORD>(result), L"SHCreateDirectoryExW").WithContext(path);
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return ValidationError(L"Create directory", L"The path exists but is not a directory.").WithContext(path);
    }
    return Status::Ok();
}

std::wstring JoinPath(std::wstring_view directory, std::wstring_view name)
{
    std::wstring path(directory);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
    {
        path.push_back(L'\\');
    }
    path.append(name);
    return path;
}

std::wstring DefaultDataDirectory()
{
    PWSTR folder = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder)) && folder)
    {
        result = JoinPath(folder, L"thread-sim");
    }
    CoTaskMemFree(folder);
    return result;
}

} // namespace thread_sim::platform
