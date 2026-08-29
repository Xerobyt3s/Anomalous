#include "core/config.h"
#include "core/arena.h"
#include "core/log.h"

#include <charconv>
#include <cstring>

namespace anom {
namespace {

constexpr std::string_view kSpace = " \t\r";

std::string_view trim(std::string_view s)
{
    const std::size_t first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = s.find_last_not_of(kSpace);
    return s.substr(first, last - first + 1);
}

bool parse_f32(std::string_view text, f32& out)
{
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result r = std::from_chars(begin, end, out);
    return r.ec == std::errc{} && r.ptr != begin;
}

bool parse_i32(std::string_view text, i32& out)
{
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result r = std::from_chars(begin, end, out);
    return r.ec == std::errc{} && r.ptr != begin;
}

u32 parse_f32_sequence(std::string_view text, std::span<f32> out)
{
    u32 count = 0;
    std::size_t cursor = 0;
    while (count < out.size() && cursor < text.size()) {
        while (cursor < text.size() && kSpace.find(text[cursor]) != std::string_view::npos) {
            cursor++;
        }
        if (cursor >= text.size()) {
            break;
        }
        const char* begin = text.data() + cursor;
        const char* end = text.data() + text.size();
        f32 value = 0.0f;
        const std::from_chars_result r = std::from_chars(begin, end, value);
        if (r.ec != std::errc{} || r.ptr == begin) {
            break;
        }
        out[count++] = value;
        cursor = static_cast<std::size_t>(r.ptr - text.data());
    }
    return count;
}

} // namespace

bool Config::parse(Arena& arena, std::string_view text)
{
    count_ = 0;

    char* buf = arena.push_array<char>(text.size() + 1);
    if (!buf) {
        return false;
    }
    std::memcpy(buf, text.data(), text.size());
    buf[text.size()] = '\0';
    const std::string_view body(buf, text.size());

    std::string_view section;
    std::size_t cursor = 0;

    while (cursor <= body.size()) {
        const std::size_t newline = body.find('\n', cursor);
        std::string_view line = newline == std::string_view::npos
                                    ? body.substr(cursor)
                                    : body.substr(cursor, newline - cursor);
        cursor = newline == std::string_view::npos ? body.size() + 1 : newline + 1;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos) {
                log_warn("config: malformed section header: %.*s",
                         static_cast<int>(line.size()), line.data());
                continue;
            }
            section = trim(line.substr(1, close - 1));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            log_warn("config: malformed line: %.*s",
                     static_cast<int>(line.size()), line.data());
            continue;
        }

        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key.empty()) {
            continue;
        }
        if (count_ >= kMaxEntries) {
            log_error("config: too many entries (max %u)", kMaxEntries);
            return false;
        }

        std::string_view full_key = key;
        if (!section.empty()) {
            const std::size_t len = section.size() + 1 + key.size();
            char* joined = arena.push_array<char>(len);
            if (!joined) {
                return false;
            }
            std::memcpy(joined, section.data(), section.size());
            joined[section.size()] = '.';
            std::memcpy(joined + section.size() + 1, key.data(), key.size());
            full_key = std::string_view(joined, len);
        }

        entries_[count_].key = full_key;
        entries_[count_].value = value;
        count_++;
    }
    return true;
}

const Config::Entry* Config::find(std::string_view key) const
{
    for (u32 i = 0; i < count_; i++) {
        if (entries_[i].key == key) {
            return &entries_[i];
        }
    }
    return nullptr;
}

bool Config::has(std::string_view key) const
{
    return find(key) != nullptr;
}

std::string_view Config::get_str(std::string_view key, std::string_view fallback) const
{
    const Entry* entry = find(key);
    return entry ? entry->value : fallback;
}

f32 Config::get_f32(std::string_view key, f32 fallback) const
{
    const Entry* entry = find(key);
    f32 result = fallback;
    if (entry && parse_f32(entry->value, result)) {
        return result;
    }
    return fallback;
}

i32 Config::get_i32(std::string_view key, i32 fallback) const
{
    const Entry* entry = find(key);
    i32 result = fallback;
    if (entry && parse_i32(entry->value, result)) {
        return result;
    }
    return fallback;
}

Vec3 Config::get_vec3(std::string_view key, Vec3 fallback) const
{
    const Entry* entry = find(key);
    if (!entry) {
        return fallback;
    }
    f32 values[3] = {fallback.x, fallback.y, fallback.z};
    parse_f32_sequence(entry->value, values);
    return Vec3{values[0], values[1], values[2]};
}

u32 Config::get_f32_list(std::string_view key, std::span<f32> out) const
{
    const Entry* entry = find(key);
    if (!entry) {
        return 0;
    }
    return parse_f32_sequence(entry->value, out);
}

} // namespace anom
