#pragma once

#include "core/types.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace anom {

template<std::size_t N>
class FixedString {
public:
    static_assert(N > 1);

    FixedString() { buf_[0] = '\0'; }

    FixedString(std::string_view text) { assign(text); }
    FixedString(const char* text) { assign(text ? std::string_view(text) : std::string_view()); }

    void clear() { buf_[0] = '\0'; }

    bool assign(std::string_view text)
    {
        const std::size_t n = text.size() < N - 1 ? text.size() : N - 1;
        std::memcpy(buf_, text.data(), n);
        buf_[n] = '\0';
        return n == text.size();
    }

    bool append(std::string_view text)
    {
        const std::size_t have = size();
        const std::size_t room = N - 1 - have;
        const std::size_t n = text.size() < room ? text.size() : room;
        std::memcpy(buf_ + have, text.data(), n);
        buf_[have + n] = '\0';
        return n == text.size();
    }

    bool format(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        const int written = std::vsnprintf(buf_, N, fmt, args);
        va_end(args);
        return written >= 0 && static_cast<std::size_t>(written) < N;
    }

    std::string_view view() const { return std::string_view(buf_, size()); }
    const char* c_str() const { return buf_; }
    char* data() { return buf_; }

    std::size_t size() const { return std::strlen(buf_); }
    bool empty() const { return buf_[0] == '\0'; }
    static constexpr std::size_t capacity() { return N - 1; }

    friend bool operator==(const FixedString& a, std::string_view b) { return a.view() == b; }
    friend bool operator==(const FixedString& a, const char* b)
    {
        return a.view() == std::string_view(b ? b : "");
    }
    friend bool operator==(const FixedString& a, const FixedString& b)
    {
        return a.view() == b.view();
    }

private:
    char buf_[N];
};

} // namespace anom
