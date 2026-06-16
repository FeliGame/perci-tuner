// @Author: Guocheng - NUDT
// Usage: Match plain strings and glob patterns with '*' and '?'.

#ifndef INCLUDE_GLOB_MATCH_HPP_
#define INCLUDE_GLOB_MATCH_HPP_

#include <string>
#include <vector>

inline bool matchGlobPattern(const std::string &pattern, const std::string &value) {
    size_t p = 0, v = 0;
    size_t star = std::string::npos, starValue = 0;

    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v])) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            starValue = v;
        } else if (star != std::string::npos) {
            p = star + 1;
            v = ++starValue;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

inline bool matchPattern(const std::string &pattern, const std::string &value) {
    if (pattern.empty()) return false;

    if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos)
        return matchGlobPattern(pattern, value);

    return value == pattern || value.find(pattern) != std::string::npos || pattern.find(value) != std::string::npos;
}

inline bool matchAnyPattern(const std::vector<std::string> &patterns, const std::vector<std::string> &values) {
    for (const auto &pattern : patterns)
        for (const auto &value : values)
            if (matchPattern(pattern, value))
                return true;
    return false;
}

#endif // INCLUDE_GLOB_MATCH_HPP_
