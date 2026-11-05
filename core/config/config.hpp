#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "../gcc/gcc.hpp"

// Один профиль соответствует одной цели сборки: входные файлы,
// исключения, параметры GCC/Binutils и конфигурация Embedded.
class config_t {
public:
  struct profile_t {
    gcc_t::build_settings_t compiler;
    std::vector<std::filesystem::path> exclusions;
    std::vector<std::filesystem::path> sources;
  };

  static std::filesystem::path path_for(const std::filesystem::path& root,
                                         const std::string& target) {
    if (target.empty()) throw std::invalid_argument("EMPTY PROFILE NAME");
    for (const unsigned char c : target)
      if (!std::isalnum(c) && c != '_' && c != '-')
        throw std::invalid_argument("INVALID PROFILE NAME");
    return root / ".pgr" / (target + ".cfg");
  }

  static bool save(const std::filesystem::path& path, const profile_t& profile) {
    gcc_t::validate_settings(profile.compiler);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << "PGR-PROFILE-V1\n";
    const auto entry = [&](const char* name, const std::string& value) {
      file << name << ' ' << std::quoted(value) << '\n';
    };
    const auto boolean = [&](const char* name, bool value) { entry(name, value ? "1" : "0"); };
    const auto number = [&](const char* name, int value) { entry(name, std::to_string(value)); };
    const auto paths = [&](const char* name, const std::vector<std::filesystem::path>& values) {
      for (const auto& value : values) entry(name, value.generic_string());
    };
    const auto strings = [&](const char* name, const std::vector<std::string>& values) {
      for (const auto& value : values) entry(name, value);
    };
    const auto& g = profile.compiler;
    number("c_std", static_cast<int>(g.c.standart));
    number("cpp_std", static_cast<int>(g.cpp.standart));
    number("c_opt", static_cast<int>(g.c.optimization));
    number("cpp_opt", static_cast<int>(g.cpp.optimization));
    number("c_warn", static_cast<int>(g.c.warnings));
    number("cpp_warn", static_cast<int>(g.cpp.warnings));
    number("c_debug", static_cast<int>(g.c.debug));
    number("cpp_debug", static_cast<int>(g.cpp.debug));
    boolean("c_werror", g.c.warnings_as_errors);
    boolean("cpp_werror", g.cpp.warnings_as_errors);
    strings("c_flag", g.c.flags);
    strings("cpp_flag", g.cpp.flags);
    entry("exe", g.executable_name);
    entry("gcc", g.gcc_command);
    entry("gpp", g.gpp_command);
    strings("link_flag", g.linking_flags);
    strings("target_flag", g.target_flags);
    paths("include", g.include_dirs);
    paths("library_dir", g.library_dirs);
    strings("library", g.libraries);
    entry("script", g.linker_script.generic_string());
    number("analysis", static_cast<int>(g.analysis.mode));
    boolean("opt_report", g.analysis.optimization_report);
    boolean("time_report", g.analysis.time_report);
    boolean("reports", g.binary.reports);
    boolean("map", g.binary.linker_map);
    boolean("bin", g.binary.generate_bin);
    boolean("hex", g.binary.generate_hex);
    entry("tool_prefix", g.binary.toolchain_prefix);
    entry("flash_origin", std::to_string(g.binary.flash_origin));
    entry("flash_size", std::to_string(g.binary.flash_size));
    entry("ram_origin", std::to_string(g.binary.ram_origin));
    entry("ram_size", std::to_string(g.binary.ram_size));
    paths("excluded", profile.exclusions);
    paths("source", profile.sources);
    file.flush();
    return file.good();
  }

  static bool load(const std::filesystem::path& path, profile_t& profile) {
    std::ifstream file(path);
    if (!file) return false;
    std::string header;
    if (!std::getline(file, header) || header != "PGR-PROFILE-V1")
      throw std::runtime_error("UNKNOWN PGR PROFILE FORMAT");
    profile_t updated;
    std::string key, value, line;
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      std::istringstream stream(line);
      if (!(stream >> key >> std::quoted(value)))
        throw std::runtime_error("DAMAGED PGR PROFILE");
      const auto integer = [&]() { return std::stoi(value); };
      const auto flag = [&]() {
        if (value != "0" && value != "1") throw std::runtime_error("INVALID PROFILE BOOLEAN");
        return value == "1";
      };
      auto& g = updated.compiler;
      if (key == "c_std") g.c.standart = static_cast<gcc_t::standart_t>(integer());
      else if (key == "cpp_std") g.cpp.standart = static_cast<gcc_t::standart_t>(integer());
      else if (key == "c_opt") g.c.optimization = static_cast<gcc_t::optimization_t>(integer());
      else if (key == "cpp_opt") g.cpp.optimization = static_cast<gcc_t::optimization_t>(integer());
      else if (key == "c_warn") g.c.warnings = static_cast<gcc_t::warning_t>(integer());
      else if (key == "cpp_warn") g.cpp.warnings = static_cast<gcc_t::warning_t>(integer());
      else if (key == "c_debug") g.c.debug = static_cast<gcc_t::debug_t>(integer());
      else if (key == "cpp_debug") g.cpp.debug = static_cast<gcc_t::debug_t>(integer());
      else if (key == "c_werror") g.c.warnings_as_errors = flag();
      else if (key == "cpp_werror") g.cpp.warnings_as_errors = flag();
      else if (key == "c_flag") g.c.flags.push_back(value);
      else if (key == "cpp_flag") g.cpp.flags.push_back(value);
      else if (key == "exe") g.executable_name = value;
      else if (key == "gcc") g.gcc_command = value;
      else if (key == "gpp") g.gpp_command = value;
      else if (key == "link_flag") g.linking_flags.push_back(value);
      else if (key == "target_flag") g.target_flags.push_back(value);
      else if (key == "include") g.include_dirs.emplace_back(value);
      else if (key == "library_dir") g.library_dirs.emplace_back(value);
      else if (key == "library") g.libraries.push_back(value);
      else if (key == "script") g.linker_script = value;
      else if (key == "analysis") g.analysis.mode = static_cast<gcc_t::analysis_mode_t>(integer());
      else if (key == "opt_report") g.analysis.optimization_report = flag();
      else if (key == "time_report") g.analysis.time_report = flag();
      else if (key == "reports") g.binary.reports = flag();
      else if (key == "map") g.binary.linker_map = flag();
      else if (key == "bin") g.binary.generate_bin = flag();
      else if (key == "hex") g.binary.generate_hex = flag();
      else if (key == "tool_prefix") g.binary.toolchain_prefix = value;
      else if (key == "flash_origin") g.binary.flash_origin = std::stoull(value);
      else if (key == "flash_size") g.binary.flash_size = std::stoull(value);
      else if (key == "ram_origin") g.binary.ram_origin = std::stoull(value);
      else if (key == "ram_size") g.binary.ram_size = std::stoull(value);
      else if (key == "excluded") updated.exclusions.emplace_back(value);
      else if (key == "source") updated.sources.emplace_back(value);
      else throw std::runtime_error("UNKNOWN PGR PROFILE KEY: " + key);
    }
    if (file.bad()) return false;
    gcc_t::validate_settings(updated.compiler);
    const auto valid_relative = [](const std::filesystem::path& path) {
      if (path.empty() || path.is_absolute() || path == ".") return false;
      for (const auto& part : path) if (part == ".." || part == ".") return false;
      return true;
    };
    for (const auto& selected : updated.sources)
      if (!valid_relative(selected)) throw std::runtime_error("INVALID SOURCE IN PROFILE");
    for (const auto& excluded : updated.exclusions)
      if (!valid_relative(excluded)) throw std::runtime_error("INVALID EXCLUSION IN PROFILE");
    profile = std::move(updated);
    return true;
  }
};

#endif // CONFIG_HPP
