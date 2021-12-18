#include <boost/optional/optional.hpp>
#include <fmt/core.h>
#include <iostream>
#include <string>

template <class T>
using boptional = boost::optional<T>;

std::string resolve_path(const std::string& path);

template <typename V>
struct fmt::formatter<boptional<V>> : formatter<string_view> {
  template <typename FormatContext>
  auto format(const boptional<V>& opt, FormatContext& ctx) -> decltype(ctx.out()) {
    if (opt.has_value()) {
      return format_to(ctx.out(), "{}", opt.value());
    }
    return format_to(ctx.out(), "NONE");
  }
};

template <typename... T>
void panic(fmt::format_string<T...> fmt, T&&... args) {
  fmt::print(stderr, fmt, args...);
  std::cerr << std::endl;
  exit(1);
}
