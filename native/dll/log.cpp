#include "log.hpp"

#include <share.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace oyr::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::atomic<Sink> g_sink = nullptr;

// The current start plus the ones before it.
constexpr int kKeptLogs = 5;

std::wstring log_path(const std::wstring& folder, int age) {
    return folder + (age == 0 ? L"open_yoku_rando.log" : L"open_yoku_rando." + std::to_wstring(age) + L".log");
}

}  // namespace

void open(HMODULE module) {
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    std::wstring folder = path.substr(0, path.find_last_of(L"\\/") + 1);

    std::lock_guard lock(g_mutex);
    if (!g_file) {
        for (int age = kKeptLogs - 1; age > 0; --age) {
            MoveFileExW(log_path(folder, age - 1).c_str(), log_path(folder, age).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        g_file = _wfsopen(log_path(folder, 0).c_str(), L"w", _SH_DENYWR);
    }
}

void write(std::string_view message) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    std::string line =
        std::format("{:02}:{:02}:{:02}.{:03} [{}] {}\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId(), message);
    {
        std::lock_guard lock(g_mutex);
        if (g_file) {
            std::fwrite(line.data(), 1, line.size(), g_file);
            std::fflush(g_file);
        }
    }
    if (Sink sink = g_sink.load()) {
        sink(message);
    }
}

void set_sink(Sink sink) {
    g_sink = sink;
}

}  // namespace oyr::log
