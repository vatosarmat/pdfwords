#include <filesystem>

namespace fs = std::filesystem;

std::string resolve_path(const std::string& path) {
  if (fs::is_symlink(path)) {
    return fs::read_symlink(path);
  }

  return path;
}
