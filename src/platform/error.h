// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/windows_config.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace thread_sim::platform
{

enum class ErrorDomain : uint8_t
{
    kNone = 0,
    kWin32,      // GetLastError / DWORD
    kHResult,    // HRESULT
    kNtStatus,   // ntstatus
    kCrt,        // errno
    kValidation, // rejected input, code is unused
    kFormat,     // malformed file or record
    kUnsupported,
    kInternal,
};

// capture codes at the failing call before another call changes GetLastError
struct Error
{
    ErrorDomain domain = ErrorDomain::kNone;
    int64_t code = 0;
    std::wstring operation; // operation, such as L"SetThreadGroupAffinity"
    std::wstring context;   // identity, such as L"thread 3 (TID 1234)"
    std::wstring detail;    // explanation

    std::wstring Describe() const;
    std::wstring CodeText() const;
    Error& WithContext(std::wstring_view text) &;
    Error&& WithContext(std::wstring_view text) &&;
};

std::wstring FormatWin32Error(DWORD code);

Error Win32Error(DWORD code, std::wstring_view operation);
// call immediately after the failing API
Error LastWin32Error(std::wstring_view operation);
Error HResultError(HRESULT hr, std::wstring_view operation);
Error NtStatusError(LONG status, std::wstring_view operation);
Error CrtError(int error_number, std::wstring_view operation);
Error ValidationError(std::wstring_view operation, std::wstring_view detail);
Error FormatError(std::wstring_view operation, std::wstring_view detail);
Error UnsupportedError(std::wstring_view operation, std::wstring_view detail);
Error InternalError(std::wstring_view operation, std::wstring_view detail);

template <typename T>
class [[nodiscard]] Result
{
  public:
    Result(T value)
        : storage_(std::in_place_index<0>, std::move(value))
    {
    }
    Result(Error error)
        : storage_(std::in_place_index<1>, std::move(error))
    {
    }

    bool ok() const noexcept
    {
        return storage_.index() == 0;
    }
    explicit operator bool() const noexcept
    {
        return ok();
    }
    T& value() &
    {
        return std::get<0>(storage_);
    }
    const T& value() const&
    {
        return std::get<0>(storage_);
    }
    T&& value() &&
    {
        return std::get<0>(std::move(storage_));
    }
    const Error& error() const&
    {
        return std::get<1>(storage_);
    }
    Error&& error() &&
    {
        return std::get<1>(std::move(storage_));
    }
    T* operator->()
    {
        return &value();
    }
    const T* operator->() const
    {
        return &value();
    }

  private:
    std::variant<T, Error> storage_;
};

class [[nodiscard]] Status
{
  public:
    Status() = default;
    Status(Error error)
        : error_(std::move(error))
    {
    }
    static Status Ok()
    {
        return {};
    }
    bool ok() const noexcept
    {
        return !error_.has_value();
    }
    explicit operator bool() const noexcept
    {
        return ok();
    }
    const Error& error() const
    {
        return *error_;
    }

  private:
    std::optional<Error> error_;
};

} // namespace thread_sim::platform
