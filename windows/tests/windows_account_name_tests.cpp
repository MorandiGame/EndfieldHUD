// Exercise the actual Win32 name adapters: the fake-account suite cannot
// catch a buffer being moved before GetUserNameW writes into it. No names
// are printed, saved, or applied to the profile.
#include "native/windows_account.hpp"
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <security.h>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        std::vector<wchar_t> buffer(4096, L'\0');
        DWORD length = static_cast<DWORD>(buffer.size());
        if (!GetUserNameW(buffer.data(), &length) || length < 2)
            throw std::runtime_error("Read reference Windows user name");
        const std::wstring expected(buffer.data(), length - 1);

        ULONG displayLength = static_cast<ULONG>(buffer.size());
        std::optional<std::wstring> display;
        if (GetUserNameExW(NameDisplay, buffer.data(), &displayLength))
            display = std::wstring(buffer.data());

        const auto calls = endfield::native::currentWindowsAccountCalls();
        for (unsigned i = 0; i < 64; ++i) {
            if (calls.userName() != std::optional(expected))
                throw std::runtime_error("Win32 user name adapter changed the result");
            if (calls.displayName() != display)
                throw std::runtime_error("Win32 display name adapter changed the result");
            if (!HeapValidate(GetProcessHeap(), 0, nullptr))
                throw std::runtime_error("Account name lookup damaged the process heap");
        }
        std::cout << "PASS: 64 Win32 account name reads; results match and heap remains valid\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
