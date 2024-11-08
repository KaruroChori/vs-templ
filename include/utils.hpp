#pragma once

#include <cstring>
#include <variant>
#include <vector>
#include <string>

namespace vs{
namespace templ{

template <typename T, typename... Args> struct concatenator;

template <typename... Args0, typename... Args1>
struct concatenator<std::variant<Args0...>, Args1...> {
    using type = std::variant<Args0..., Args1...>;
};

template<typename T>
const T& get_or(const auto& ref, const T& defval) noexcept{
    if(std::holds_alternative<T>(ref))return std::get<T>(ref);
    else return defval;
}

std::vector<std::string_view> split_string (const char* str, char delim);

inline constexpr std::size_t cexpr_strlen(const char* s){return std::char_traits<char>::length(s);}
inline bool cexpr_strneqv(const char* s, const char* c){return strncmp(s, c, cexpr_strlen(c))==0;}

int cmp_dot_str(const char* a, const char* b);

}
}