// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "records/json.h"

#include <cwchar>

namespace thread_sim::json
{

namespace
{

void ReportError(void* context, const win32ui::JsonDiagnostic& diagnostic, std::wstring_view custom_text)
{
    auto* destination = static_cast<std::wstring*>(context);
    if (!destination || !destination->empty() || !diagnostic.first_failure)
    {
        return;
    }
    switch (diagnostic.code)
    {
    case win32ui::JsonErrorCode::kNone:
        return;
    case win32ui::JsonErrorCode::kExpectedCharacter:
        {
            wchar_t message[48] = {};
            swprintf_s(message, L"Expected '%c'.", diagnostic.expected);
            *destination = message;
            break;
        }
    case win32ui::JsonErrorCode::kTooDeep:
        *destination = L"The document is nested too deeply.";
        break;
    case win32ui::JsonErrorCode::kUnterminatedComment:
        *destination = L"An unterminated block comment is not allowed.";
        break;
    case win32ui::JsonErrorCode::kUnexpectedToken:
        *destination = L"Unexpected token.";
        break;
    case win32ui::JsonErrorCode::kStringTooLong:
        *destination = L"A string is longer than the format allows.";
        break;
    case win32ui::JsonErrorCode::kUnterminatedString:
        *destination = L"The document ends inside a string.";
        break;
    case win32ui::JsonErrorCode::kUnescapedControl:
        *destination = L"A string contains an unescaped control character.";
        break;
    case win32ui::JsonErrorCode::kMalformedEscape:
        *destination = L"A string contains a malformed escape.";
        break;
    case win32ui::JsonErrorCode::kUnsupportedEscape:
        *destination = L"A string contains an unsupported escape.";
        break;
    case win32ui::JsonErrorCode::kUnsignedExpected:
        *destination = L"An unsigned integer was expected.";
        break;
    case win32ui::JsonErrorCode::kLeadingZero:
        *destination = L"A number has a leading zero.";
        break;
    case win32ui::JsonErrorCode::kNumberOutOfRange:
        *destination = L"A number is out of range.";
        break;
    case win32ui::JsonErrorCode::kIntegersOnly:
        *destination = L"Only integers are supported.";
        break;
    case win32ui::JsonErrorCode::kBooleanExpected:
        *destination = L"A boolean was expected.";
        break;
    case win32ui::JsonErrorCode::kEmptyFraction:
        *destination = L"A number has an empty fraction.";
        break;
    case win32ui::JsonErrorCode::kEmptyExponent:
        *destination = L"A number has an empty exponent.";
        break;
    case win32ui::JsonErrorCode::kTrailingContent:
        *destination = L"The document contains trailing content.";
        break;
    case win32ui::JsonErrorCode::kCustom:
        *destination = custom_text;
        break;
    }
    wchar_t position[64] = {};
    swprintf_s(position, L" (line %zu, column %zu)", diagnostic.position.line, diagnostic.position.column);
    *destination += position;
}

} // namespace

win32ui::JsonErrorSink Errors(std::wstring* destination)
{
    return {ReportError, destination};
}

} // namespace thread_sim::json
