#pragma once

// Small text helpers for the DLL's own files and the RL arguments.

#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oyr::text {

// True if all of `text` is a number of type T, which is then in `value`.
template <class T>
bool parse_number(std::string_view text, T& value) {
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

template <class T>
std::optional<T> number(std::string_view text) {
    T value{};
    return parse_number(text, value) ? std::optional<T>(value) : std::nullopt;
}

// The fields between the separators, empty ones included; one field for text without a separator.
inline std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> fields;
    for (size_t start = 0;;) {
        size_t at = text.find(separator, start);
        fields.push_back(text.substr(start, at == std::string_view::npos ? at : at - start));
        if (at == std::string_view::npos) {
            return fields;
        }
        start = at + 1;
    }
}

// The file's lines without their line endings ("\n" or "\r\n"); empty if it cannot be opened.
inline std::optional<std::vector<std::string>> read_lines(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

}  // namespace oyr::text
