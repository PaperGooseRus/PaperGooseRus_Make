#ifndef CLI_HPP
#define CLI_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "../project/project.hpp"
#include "../gcc/gcc.hpp"
#include "../config/config.hpp"

// Циклический текстовый интерфейс управления проектом и сборкой.
class cli_t {
public:
  using build_callback_t = std::function<int(
    const project_t&, const std::filesystem::path&, const gcc_t::build_settings_t&)>;

  struct settings_t {
    std::filesystem::path project_path = ".";
    std::filesystem::path build_root = "C:/PGR/build";
    std::vector<std::filesystem::path> excluded_dirs;
    std::vector<std::filesystem::path> selected_sources;
    std::string target_name = "default";
    gcc_t::build_settings_t compiler;
  };

private:
  settings_t settings;
  build_callback_t build_callback;

  // Чтение команды целиком, включая пути с пробелами.
  static std::string read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line;
  }

  static std::string trim(std::string value) {
    const auto valid = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), valid));
    value.erase(std::find_if(value.rbegin(), value.rend(), valid).base(), value.end());
    return value;
  }

  std::string project_name() const {
    const auto base = settings.project_path.filename().string();
    return settings.target_name == "default" ? base : base + "__" + settings.target_name;
  }

  void load_profile(const std::string& target) {
    config_t::profile_t profile;
    if (config_t::load(config_t::path_for(settings.project_path, target), profile)) {
      settings.compiler = std::move(profile.compiler);
      settings.excluded_dirs = std::move(profile.exclusions);
      settings.selected_sources = std::move(profile.sources);
      std::cout << "[PROFILE] Loaded " << target << '\n';
    } else {
      settings.compiler = gcc_t::build_settings_t{};
      settings.excluded_dirs.clear();
      settings.selected_sources.clear();
      std::cout << "[PROFILE] New target " << target << '\n';
    }
    settings.target_name = target;
  }

  void save_profile() const {
    const config_t::profile_t profile{settings.compiler, settings.excluded_dirs,
                                      settings.selected_sources};
    const auto file = config_t::path_for(settings.project_path, settings.target_name);
    if (!config_t::save(file, profile)) throw std::runtime_error("CANNOT SAVE BUILD PROFILE");
    std::cout << "[PROFILE] Saved " << file << '\n';
  }

  std::filesystem::path build_directory() const {
    return settings.build_root / project_name();
  }

  std::filesystem::path log_path() const {
    return build_directory() / (project_name() + ".log");
  }

  // Сканирование с учётом исключённых пользователем каталогов.
  bool scan(project_t& project) const {
    for (const auto& path : settings.excluded_dirs) project.exclude(path);
    project.set_target(settings.target_name);
    project.select_sources(settings.selected_sources);
    if (project.scan() != 0) {
      std::cerr << "[ERROR] Project directory is not available.\n";
      return false;
    }
    return true;
  }

  void menu() const {
    std::cout << "\n==============================================\n"
              << "              PAPER GOOSE RUS MAKE\n"
              << "==============================================\n"
              << "Project: " << project_name() << '\n'
              << "Source:  " << settings.project_path.string() << '\n'
              << "Build:   " << build_directory().string() << '\n'
              << "----------------------------------------------\n"
              << "  1. Build project (incremental)\n"
              << "  2. Select project\n"
              << "  3. Project information\n"
              << "  4. Source files\n"
              << "  5. Show build log (last 40 lines)\n"
              << "  6. Clean build artifacts\n"
              << "  7. Excluded directories\n"
              << "  8. Help\n"
              << "  9. GCC build settings\n"
              << " 10. GCC analysis and reports\n"
              << " 11. Executable / Embedded analysis\n"
              << " 12. Build targets / profiles\n"
              << " 13. Select target source files\n"
              << "  0. Exit\n"
              << "----------------------------------------------\n";
  }

  void select_project() {
    const auto value = trim(read_line("Project path: "));
    if (!std::cin || value.empty()) return;
    std::error_code error;
    const auto root = std::filesystem::canonical(value, error);
    if (error || !std::filesystem::is_directory(root) || root.filename().empty()) {
      std::cout << "[ERROR] Invalid project directory.\n";
      return;
    }
    settings.project_path = root;
    load_profile("default");
    std::cout << "[OK] Selected: " << project_name() << '\n';
  }

  void project_info() const {
    project_t project(settings.project_path);
    if (!scan(project)) return;
    std::size_t c = 0, cpp = 0, assembly = 0;
    for (const auto& file : project.sources()) {
      if (file.language == project_source_t::language_t::C) ++c;
      else if (file.language == project_source_t::language_t::CPP) ++cpp;
      else ++assembly;
    }
    std::cout << "\n[PROJECT]\nName: " << project_name()
              << "\nSource: " << settings.project_path
              << "\nBuild: " << build_directory()
              << "\nLog: " << log_path()
              << "\nC files: " << c << "\nC++ files: " << cpp
              << "\nASM files: " << assembly << "\nTarget: " << settings.target_name
              << "\nSelected sources: " << (settings.selected_sources.empty() ? "AUTO" : std::to_string(settings.selected_sources.size()))
              << "\nExcluded directories: " << settings.excluded_dirs.size() << '\n';
    show_configuration();
  }

  void source_files() const {
    project_t project(settings.project_path);
    if (!scan(project)) return;
    std::cout << "\n[SOURCES] " << project.sources().size() << " file(s)\n";
    for (const auto& file : project.sources()) {
      const char* language = file.language == project_source_t::language_t::C ? "C  " :
                             file.language == project_source_t::language_t::CPP ? "C++" :
                             file.language == project_source_t::language_t::ASM_CPP ? "ASM+" : "ASM";
      std::cout << "  [" << language << "] " << file.path.lexically_relative(project.root()).string() << '\n';
    }
  }

  int build() const {
    project_t project(settings.project_path);
    if (!scan(project)) return 1;
    if (project.sources().empty()) {
      std::cout << "[ERROR] No source files found.\n";
      return 1;
    }
    std::cout << "\n[BUILD] " << project.name() << " (" << project.sources().size()
              << " source files)\n";
    try {
      const int result = build_callback(project, settings.build_root, settings.compiler);
      std::cout << (result == 0 ? "[OK] Build successful.\n" : "[ERROR] Build failed.\n")
                << "Exit code: " << result << "\nLog: " << log_path() << '\n';
      return result;
    } catch (const std::exception& error) {
      std::cerr << "[ERROR] " << error.what() << '\n';
      return 1;
    }
  }

  void show_log() const {
    std::ifstream file(log_path());
    if (!file) {
      std::cout << "[INFO] No log yet: " << log_path() << '\n';
      return;
    }
    // Показываем хвост журнала, не загружая весь файл в память.
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
      if (lines.size() == 40) lines.erase(lines.begin());
      lines.push_back(line);
    }
    std::cout << "\n[LOG] " << log_path() << "\n";
    for (const auto& item : lines) std::cout << item << '\n';
  }

  void clean() const {
    std::error_code error;
    const auto root = std::filesystem::absolute(settings.build_root, error).lexically_normal();
    if (error || project_name().empty() || project_name() == "." || project_name() == "..") {
      std::cerr << "[ERROR] Invalid build root.\n";
      return;
    }
    const auto target = root / project_name();
    if (target.parent_path() != root || !std::filesystem::is_directory(target, error) || error) {
      std::cout << "[INFO] Nothing to clean.\n";
      return;
    }
    if (std::filesystem::is_symlink(target, error) || error) {
      std::cerr << "[ERROR] Refusing to clean symbolic link.\n";
      return;
    }
    std::cout << "Remove intermediate files and executable in " << target << "\n"
              << "Log will be preserved.\n";
    if (trim(read_line("Type YES to confirm: ")) != "YES") {
      std::cout << "[INFO] Cancelled.\n";
      return;
    }
    std::uintmax_t removed = 0;
    for (const char* dir : {"preprocessing", "compilation", "assembly", "linking", "dependencies", "cache", "analysis", "binary"}) {
      const auto child = target / dir;
      error.clear();
      if (std::filesystem::is_symlink(child, error)) {
        std::cerr << "[ERROR] Refusing to remove symbolic link: " << child << '\n';
        continue;
      }
      if (!error) removed += std::filesystem::remove_all(child, error);
      if (error) std::cerr << "[ERROR] " << error.message() << '\n';
    }
    std::cout << "[OK] Removed entries: " << removed << '\n';
  }

  void exclusions() {
    std::cout << "\n[EXCLUDED DIRECTORIES]\n";
    if (settings.excluded_dirs.empty()) std::cout << "  (none)\n";
    for (const auto& path : settings.excluded_dirs) std::cout << "  " << path.string() << '\n';
    std::cout << "Enter relative directory to toggle, or empty to return.\n";
    const auto value = trim(read_line("Directory: "));
    if (!std::cin || value.empty()) return;
    const std::filesystem::path path(value);
    if (path.is_absolute() || path.empty() || path == "." || path == "..") {
      std::cerr << "[ERROR] Use a relative directory inside the project.\n";
      return;
    }
    for (const auto& part : path) {
      if (part == ".." || part == ".") {
        std::cerr << "[ERROR] Parent and current-directory components are forbidden.\n";
        return;
      }
    }
    const auto normalized = path.lexically_normal();
    auto it = std::find(settings.excluded_dirs.begin(), settings.excluded_dirs.end(), normalized);
    if (it != settings.excluded_dirs.end()) {
      settings.excluded_dirs.erase(it);
      std::cout << "[OK] Included: " << normalized << '\n';
    } else if (std::filesystem::is_directory(settings.project_path / normalized)) {
      settings.excluded_dirs.push_back(normalized);
      std::cout << "[OK] Excluded: " << normalized << '\n';
    } else {
      std::cerr << "[ERROR] Directory not found.\n";
    }
  }

  // Краткое описание активных параметров, полученных непосредственно из gcc_t.
  void show_configuration() const {
    const auto& config = settings.compiler;
    std::cout << "\n[GCC CONFIGURATION]\n"
              << "C compiler:   " << config.gcc_command << '\n'
              << "C options:    " << gcc_t::compiler_flags_text(config.c) << '\n'
              << "C++ compiler: " << config.gpp_command << '\n'
              << "C++ options:  " << gcc_t::compiler_flags_text(config.cpp) << '\n'
              << "Executable:   " << config.executable_name << '\n';
    std::cout << "Include dirs: " << config.include_dirs.size()
              << " | Library dirs: " << config.library_dirs.size()
              << " | Libraries: " << config.libraries.size()
              << " | Linker flags: " << config.linking_flags.size() << '\n';
    std::cout << "GCC analysis:  " << gcc_t::analysis_name(config.analysis.mode)
              << " | Optimization: " << (config.analysis.optimization_report ? "ON" : "OFF")
              << " | Timing: " << (config.analysis.time_report ? "ON" : "OFF") << '\n';
    for (const auto& dir : config.include_dirs) std::cout << "  -I" << dir << '\n';
    for (const auto& dir : config.library_dirs) std::cout << "  -L" << dir << '\n';
    for (const auto& lib : config.libraries) std::cout << "  -l" << lib << '\n';
    for (const auto& flag : config.linking_flags) std::cout << "  " << flag << '\n';
    std::cout << "Target flags: " << config.target_flags.size()
              << " | Linker script: " << (config.linker_script.empty() ? "(none)" : config.linker_script.string()) << '\n'
              << "Binary reports: " << (config.binary.reports ? "ON" : "OFF")
              << " | Map: " << (config.binary.linker_map ? "ON" : "OFF")
              << " | BIN: " << (config.binary.generate_bin ? "ON" : "OFF")
              << " | HEX: " << (config.binary.generate_hex ? "ON" : "OFF") << '\n'
              << "Toolchain prefix: " << (config.binary.toolchain_prefix.empty() ? "(host)" : config.binary.toolchain_prefix) << '\n';
    for (const auto& flag : config.target_flags) std::cout << "  target " << flag << '\n';
  }

  // Простейшее безопасное преобразование номера пункта меню.
  static std::size_t list_index(const std::string& value, std::size_t max) {
    if (value.empty()) return 0;
    std::istringstream input(value);
    std::size_t number = 0;
    char extra = 0;
    if (!(input >> number) || (input >> extra) || number == 0 || number > max) return 0;
    return number;
  }

  // Единое меню добавления и удаления флагов, библиотек и каталогов.
  template <typename T, typename Converter>
  void edit_list(std::vector<T>& values, const std::string& title, Converter convert) {
    for (;;) {
      std::cout << "\n[" << title << "]\n";
      if (values.empty()) std::cout << "  (empty)\n";
      for (std::size_t i = 0; i < values.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << values[i] << '\n';
      }
      std::cout << "  a. Add\n  d. Delete\n  0. Back\n";
      const auto command = trim(read_line("List > "));
      if (!std::cin || command == "0") return;
      if (command == "a") {
        const auto value = trim(read_line("New value: "));
        if (!std::cin) return;
        if (value.empty()) continue;
        try {
          T entry = convert(value);
          values.push_back(std::move(entry));
          try {
            gcc_t::validate_settings(settings.compiler);
            std::cout << "[OK] Added.\n";
          } catch (...) {
            values.pop_back();
            throw;
          }
        } catch (const std::exception& error) {
          std::cout << "[ERROR] " << error.what() << '\n';
        }
      } else if (command == "d") {
        const auto value = trim(read_line("Item number: "));
        if (!std::cin) return;
        const auto index = list_index(value, values.size());
        if (index == 0) std::cout << "[ERROR] Invalid item number.\n";
        else {
          values.erase(values.begin() + static_cast<std::ptrdiff_t>(index - 1));
          std::cout << "[OK] Deleted.\n";
        }
      } else {
        std::cout << "[ERROR] Unknown list action.\n";
      }
    }
  }

  // Относительный путь разворачивается относительно выбранного проекта.
  std::filesystem::path resolve_directory(const std::string& value) const {
    const std::filesystem::path path(value);
    const auto result = (path.is_absolute() ? path : settings.project_path / path).lexically_normal();
    if (!std::filesystem::is_directory(result)) {
      throw std::invalid_argument("DIRECTORY DOES NOT EXIST: " + result.string());
    }
    return result;
  }

  // Подменю настроек конкретного языка.
  void configure_language(gcc_t::gcc_element_t& element) {
    const bool cpp = element.language == gcc_t::language_t::CPP;
    for (;;) {
      std::cout << "\n[" << (cpp ? "C++" : "C") << " COMPILER SETTINGS]\n"
                << "Active: " << gcc_t::compiler_flags_text(element) << '\n'
                << "  1. Language standard\n"
                << "  2. Optimization\n"
                << "  3. Warning level\n"
                << "  4. Debug symbols\n"
                << "  5. Toggle -Werror (" << (element.warnings_as_errors ? "ON" : "OFF") << ")\n"
                << "  6. Custom compiler flags\n"
                << "  0. Back\n";
      const auto command = trim(read_line("Compiler > "));
      if (!std::cin || command == "0") return;
      if (command == "1") {
        if (cpp) std::cout << "1=C++11 2=C++14 3=C++17 4=C++20 5=C++23\n";
        else std::cout << "1=C99 2=C11 3=C17 4=C23 (GCC 13: -std=c2x)\n";
        const auto value = trim(read_line("Standard > "));
        if (!std::cin) return;
        if (cpp) {
          if (value == "1") element.standart = gcc_t::standart_t::CPP11;
          else if (value == "2") element.standart = gcc_t::standart_t::CPP14;
          else if (value == "3") element.standart = gcc_t::standart_t::CPP17;
          else if (value == "4") element.standart = gcc_t::standart_t::CPP20;
          else if (value == "5") element.standart = gcc_t::standart_t::CPP23;
          else std::cout << "[ERROR] Unknown standard.\n";
        } else {
          if (value == "1") element.standart = gcc_t::standart_t::C99;
          else if (value == "2") element.standart = gcc_t::standart_t::C11;
          else if (value == "3") element.standart = gcc_t::standart_t::C17;
          else if (value == "4") element.standart = gcc_t::standart_t::C23;
          else std::cout << "[ERROR] Unknown standard.\n";
        }
      } else if (command == "2") {
        std::cout << "0=-O0 1=-O1 2=-O2 3=-O3 s=-Os g=-Og\n";
        const auto value = trim(read_line("Optimization > "));
        if (!std::cin) return;
        if (value == "0") element.optimization = gcc_t::optimization_t::O0;
        else if (value == "1") element.optimization = gcc_t::optimization_t::O1;
        else if (value == "2") element.optimization = gcc_t::optimization_t::O2;
        else if (value == "3") element.optimization = gcc_t::optimization_t::O3;
        else if (value == "s") element.optimization = gcc_t::optimization_t::OS;
        else if (value == "g") element.optimization = gcc_t::optimization_t::OG;
        else std::cout << "[ERROR] Unknown optimization.\n";
      } else if (command == "3") {
        std::cout << "0=GCC default 1=-Wall 2=-Wall,-Wextra 3=Strict (+-Wpedantic)\n";
        const auto value = trim(read_line("Warnings > "));
        if (!std::cin) return;
        if (value == "0") element.warnings = gcc_t::warning_t::DEFAULT;
        else if (value == "1") element.warnings = gcc_t::warning_t::WALL;
        else if (value == "2") element.warnings = gcc_t::warning_t::EXTRA;
        else if (value == "3") element.warnings = gcc_t::warning_t::STRICT_WARNINGS;
        else std::cout << "[ERROR] Unknown warning mode.\n";
      } else if (command == "4") {
        std::cout << "0=Off 1=-g 3=-g3\n";
        const auto value = trim(read_line("Debug > "));
        if (!std::cin) return;
        if (value == "0") element.debug = gcc_t::debug_t::OFF;
        else if (value == "1") element.debug = gcc_t::debug_t::G;
        else if (value == "3") element.debug = gcc_t::debug_t::G3;
        else std::cout << "[ERROR] Unknown debug mode.\n";
      } else if (command == "5") {
        element.warnings_as_errors = !element.warnings_as_errors;
      } else if (command == "6") {
        edit_list(element.flags, cpp ? "C++ FLAGS" : "C FLAGS", [](const std::string& value) { return value; });
      } else {
        std::cout << "[ERROR] Unknown setting.\n";
      }
    }
  }

  // Настройки линковщика: имена библиотек вводятся без префикса -l.
  void configure_linker() {
    for (;;) {
      const auto& config = settings.compiler;
      std::cout << "\n[LINKER SETTINGS]\n"
                << "  Executable: " << config.executable_name << '\n'
                << "  C driver: " << config.gcc_command << '\n'
                << "  C++ driver: " << config.gpp_command << '\n'
                << "  1. Executable filename\n"
                << "  2. Library directories (-L)\n"
                << "  3. Libraries (-l)\n"
                << "  4. Additional linker flags\n"
                << "  5. GCC command\n"
                << "  6. G++ command\n"
                << "  0. Back\n";
      const auto command = trim(read_line("Linker > "));
      if (!std::cin || command == "0") return;
      if (command == "2") {
        edit_list(settings.compiler.library_dirs, "LIBRARY DIRECTORIES",
          [this](const std::string& value) { return resolve_directory(value); });
      } else if (command == "3") {
        edit_list(settings.compiler.libraries, "LIBRARIES",
          [](const std::string& value) { return value; });
      } else if (command == "4") {
        edit_list(settings.compiler.linking_flags, "LINKER FLAGS",
          [](const std::string& value) { return value; });
      } else if (command == "1" || command == "5" || command == "6") {
        const auto value = trim(read_line("New value: "));
        if (!std::cin) return;
        if (value.empty()) continue;
        auto modified = settings.compiler;
        if (command == "1") modified.executable_name = value;
        else if (command == "5") modified.gcc_command = value;
        else modified.gpp_command = value;
        try {
          gcc_t::validate_settings(modified);
          settings.compiler = std::move(modified);
          std::cout << "[OK] Updated.\n";
        } catch (const std::exception& error) {
          std::cout << "[ERROR] " << error.what() << '\n';
        }
      } else {
        std::cout << "[ERROR] Unknown setting.\n";
      }
    }
  }

  // Подменю сборки сохраняет параметры на время работы программы.
  void configure_build() {
    for (;;) {
      show_configuration();
      std::cout << "  1. Configure C compiler\n"
                << "  2. Configure C++ compiler\n"
                << "  3. Configure linker\n"
                << "  4. Additional include directories (-I)\n"
                << "  5. Reset GCC settings\n"
                << "  0. Back\n";
      const auto command = trim(read_line("Settings > "));
      if (!std::cin || command == "0") return;
      if (command == "1") configure_language(settings.compiler.c);
      else if (command == "2") configure_language(settings.compiler.cpp);
      else if (command == "3") configure_linker();
      else if (command == "4") {
        edit_list(settings.compiler.include_dirs, "INCLUDE DIRECTORIES",
          [this](const std::string& value) { return resolve_directory(value); });
      } else if (command == "5") {
        if (trim(read_line("Type YES to reset GCC settings: ")) == "YES") {
          settings.compiler = gcc_t::build_settings_t{};
          std::cout << "[OK] Compiler settings reset.\n";
        }
      } else {
        std::cout << "[ERROR] Unknown setting.\n";
      }
    }
  }

  // Чтение и ограниченный просмотр больших дампов без загрузки файла целиком.
  static void preview_report(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      std::cerr << "[ERROR] Cannot read report: " << path << '\n';
      return;
    }
    constexpr std::size_t max_lines = 80;
    constexpr std::size_t max_bytes = 12000;
    std::cout << "\n[REPORT] " << path << "\n----------------------------------------------\n";
    std::size_t total = 0, lines = 0;
    char character = 0;
    while (total < max_bytes && lines < max_lines && file.get(character)) {
      std::cout.put(character);
      ++total;
      if (character == '\n') ++lines;
    }
    if (!file.eof()) std::cout << "\n[TRUNCATED] Open the report in an editor to read the rest.\n";
    std::cout << "----------------------------------------------\n";
  }

  // Выбор конкретного исходного файла и постраничный просмотр его представлений.
  void browse_analysis() const {
    project_t project(settings.project_path);
    if (!scan(project)) return;
    const auto& sources = project.sources();
    if (sources.empty()) {
      std::cout << "[INFO] Project has no source files.\n";
      return;
    }

    std::cout << "\n[ANALYSIS / SOURCES]\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
      std::cout << " " << (i + 1) << ". "
                << sources[i].path.lexically_relative(project.root()).generic_string() << '\n';
    }
    const auto answer = trim(read_line("Source number (0 = back): "));
    if (!std::cin || answer == "0") return;
    const auto index = list_index(answer, sources.size());
    if (index == 0) {
      std::cout << "[ERROR] Invalid source number.\n";
      return;
    }
    const auto source_rel = sources[index - 1].path.lexically_relative(project.root());
    const auto dir = build_directory() / "analysis" / source_rel;
    if (!std::filesystem::is_directory(dir)) {
      std::cout << "[INFO] No analysis yet: " << dir << '\n';
      return;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
           dir, std::filesystem::directory_options::skip_permission_denied)) {
      if (entry.is_regular_file() && !entry.is_symlink() && entry.path().filename() != "analysis.ok") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
      std::cout << "[INFO] No GCC dumps or reports found.\n";
      return;
    }
    constexpr std::size_t page_size = 18;
    std::size_t page = 0;
    for (;;) {
      const auto first = page * page_size;
      const auto last = std::min(first + page_size, files.size());
      std::cout << "\n[ANALYSIS / " << source_rel.generic_string() << "] "
                << files.size() << " report(s) | Page " << (page + 1) << "/"
                << ((files.size() + page_size - 1) / page_size) << '\n';
      for (std::size_t i = first; i < last; ++i) {
        std::cout << " " << (i - first + 1) << ". "
                  << files[i].lexically_relative(dir).generic_string() << '\n';
      }
      std::cout << " n. Next page | p. Previous page | 0. Back\n";
      const auto choice = trim(read_line("Report > "));
      if (!std::cin || choice == "0") return;
      if (choice == "n" && last < files.size()) ++page;
      else if (choice == "p" && page > 0) --page;
      else {
        const auto selected = list_index(choice, last - first);
        if (selected != 0) preview_report(files[first + selected - 1]);
        else std::cout << "[ERROR] Invalid report selection.\n";
      }
    }
  }

  // Отдельный раздел исследования: режим влияет на сигнатуру кэша сборки.
  void configure_analysis() {
    for (;;) {
      const auto& analysis = settings.compiler.analysis;
      std::cout << "\n[GCC ANALYSIS]\n"
                << "Mode:          " << gcc_t::analysis_name(analysis.mode) << '\n'
                << "Optimization:  " << (analysis.optimization_report ? "ON" : "OFF") << '\n'
                << "Timing:        " << (analysis.time_report ? "ON" : "OFF") << '\n'
                << "Reports:       " << build_directory() / "analysis" << '\n'
                << "  1. Mode (OFF / SELECTED / ALL)\n"
                << "  2. Toggle optimization report (-fopt-info-all)\n"
                << "  3. Toggle GCC timing report (-ftime-report)\n"
                << "  4. Browse reports by source file\n"
                << "  0. Back\n";
      const auto command = trim(read_line("Analysis > "));
      if (!std::cin || command == "0") return;
      if (command == "1") {
        std::cout << "0=OFF 1=SELECTED (6 dumps) 2=ALL (many passes)\n";
        const auto choice = trim(read_line("Mode > "));
        if (!std::cin) return;
        if (choice == "0") settings.compiler.analysis.mode = gcc_t::analysis_mode_t::OFF;
        else if (choice == "1") settings.compiler.analysis.mode = gcc_t::analysis_mode_t::SELECTED;
        else if (choice == "2") settings.compiler.analysis.mode = gcc_t::analysis_mode_t::ALL;
        else std::cout << "[ERROR] Unknown analysis mode.\n";
      } else if (command == "2") {
        settings.compiler.analysis.optimization_report = !analysis.optimization_report;
      } else if (command == "3") {
        settings.compiler.analysis.time_report = !analysis.time_report;
      } else if (command == "4") browse_analysis();
      else std::cout << "[ERROR] Unknown analysis action.\n";
    }
  }

  // Просмотр отчётов ELF/PE и результатов преобразования objcopy.
  void browse_binary() const {
    const auto directory = build_directory() / "binary";
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (std::filesystem::is_directory(directory, error) && !error) {
      for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (item.is_regular_file() && !item.is_symlink() && item.path().filename() != "manifest.txt") {
          files.push_back(item.path());
        }
      }
    }
    const auto map = build_directory() / "linking" /
      (std::filesystem::path(settings.compiler.executable_name).stem().string() + ".map");
    if (std::filesystem::is_regular_file(map)) files.push_back(map);
    if (files.empty()) {
      std::cout << "[INFO] No executable reports yet. Enable them and build the project.\n";
      return;
    }
    std::sort(files.begin(), files.end());
    for (;;) {
      std::cout << "\n[EXECUTABLE / REPORTS]\n";
      for (std::size_t i = 0; i < files.size(); ++i) {
        std::cout << " " << (i + 1) << ". " << files[i].filename().string() << '\n';
      }
      const auto answer = trim(read_line("Report number (0 = back): "));
      if (!std::cin || answer == "0") return;
      const auto index = list_index(answer, files.size());
      if (index == 0) {
        std::cout << "[ERROR] Unknown report.\n";
        continue;
      }
      const auto& path = files[index - 1];
      if (path.extension() == ".bin") {
        std::cout << "[BINARY IMAGE] " << path << " (" << std::filesystem::file_size(path)
                  << " bytes). Open with a hex editor.\n";
      } else preview_report(path);
    }
  }

  // Настройки целевой архитектуры и анализ результатов GNU Binutils.
  void configure_embedded() {
    for (;;) {
      const auto& bin = settings.compiler.binary;
      std::cout << "\n[EXECUTABLE / EMBEDDED]\n"
                << "Reports: " << (bin.reports ? "ON" : "OFF")
                << " | Map: " << (bin.linker_map ? "ON" : "OFF")
                << " | BIN: " << (bin.generate_bin ? "ON" : "OFF")
                << " | HEX: " << (bin.generate_hex ? "ON" : "OFF") << '\n'
                << "Toolchain prefix: " << (bin.toolchain_prefix.empty() ? "(host)" : bin.toolchain_prefix) << '\n'
                << "Linker script: " << (settings.compiler.linker_script.empty() ? "(none)" : settings.compiler.linker_script.string()) << '\n'
                << "  1. Toggle executable analysis (sections/symbols/disassembly)\n"
                << "  2. Toggle GNU ld map (-Wl,-Map)\n"
                << "  3. Toggle BIN image (objcopy)\n"
                << "  4. Toggle Intel HEX image (objcopy)\n"
                << "  5. Set GNU toolchain prefix (e.g. arm-none-eabi-)\n"
                << "  6. Target compiler flags (-mcpu, -mthumb, ...)\n"
                << "  7. Linker script (-T)\n"
                << "  8. Browse executable reports\n"
                << "  9. Toggle function/data sections and --gc-sections\n"
                << " 10. Flash/RAM memory regions\n"
                << "  0. Back\n";
      const auto command = trim(read_line("Embedded > "));
      if (!std::cin || command == "0") return;
      if (command == "1") settings.compiler.binary.reports = !bin.reports;
      else if (command == "2") settings.compiler.binary.linker_map = !bin.linker_map;
      else if (command == "3") settings.compiler.binary.generate_bin = !bin.generate_bin;
      else if (command == "4") settings.compiler.binary.generate_hex = !bin.generate_hex;
      else if (command == "5") {
        std::cout << "Use '-' to restore host GCC/Binutils.\n";
        const auto value = trim(read_line("Toolchain prefix: "));
        if (!std::cin) return;
        if (value.empty()) continue;
        auto updated = settings.compiler;
        const auto prefix = value == "-" ? std::string{} : value;
        updated.binary.toolchain_prefix = prefix;
        updated.gcc_command = prefix + "gcc";
        updated.gpp_command = prefix + "g++";
        try {
          gcc_t::validate_settings(updated);
          settings.compiler = std::move(updated);
          std::cout << "[OK] GCC and GNU Binutils use the selected toolchain.\n";
        } catch (const std::exception& e) {
          std::cout << "[ERROR] " << e.what() << '\n';
        }
      } else if (command == "6") {
        edit_list(settings.compiler.target_flags, "TARGET GCC FLAGS",
          [](const std::string& value) { return value; });
      } else if (command == "7") {
        std::cout << "Use '-' to clear linker script.\n";
        const auto value = trim(read_line("Linker script: "));
        if (!std::cin) return;
        if (value.empty()) continue;
        if (value == "-") {
          settings.compiler.linker_script.clear();
          std::cout << "[OK] Linker script cleared.\n";
        } else {
          const std::filesystem::path path(value);
          const auto resolved = (path.is_absolute() ? path : settings.project_path / path).lexically_normal();
          if (!std::filesystem::is_regular_file(resolved)) {
            std::cout << "[ERROR] Linker script not found: " << resolved << '\n';
          } else {
            settings.compiler.linker_script = resolved;
            std::cout << "[OK] Linker script: " << resolved << '\n';
          }
        }
      } else if (command == "8") browse_binary();
      else if (command == "10") {
        std::cout << "Memory regions (hex 0x... or decimal). Size 0 = unknown.\n"
                  << "FLASH: origin=" << bin.flash_origin << ", size=" << bin.flash_size << " bytes\n"
                  << "RAM: origin=" << bin.ram_origin << ", size=" << bin.ram_size << " bytes\n";
        const auto flash_origin = trim(read_line("Flash origin: "));
        const auto flash_size = trim(read_line("Flash size (bytes): "));
        const auto ram_origin = trim(read_line("RAM origin: "));
        const auto ram_size = trim(read_line("RAM size (bytes): "));
        if (!std::cin) return;
        try {
          auto updated = settings.compiler;
          const auto number = [](const std::string& value) {
            if (value.empty() || value.front() == '-') throw std::invalid_argument("INVALID ADDRESS/SIZE");
            std::size_t consumed = 0;
            const auto result = std::stoull(value, &consumed, 0);
            if (consumed != value.size()) throw std::invalid_argument("INVALID ADDRESS/SIZE");
            return static_cast<std::uint64_t>(result);
          };
          updated.binary.flash_origin = number(flash_origin);
          updated.binary.flash_size = number(flash_size);
          updated.binary.ram_origin = number(ram_origin);
          updated.binary.ram_size = number(ram_size);
          gcc_t::validate_settings(updated);
          settings.compiler = std::move(updated);
          std::cout << "[OK] Memory regions updated. Enable reports and rebuild.\n";
        } catch (const std::exception& error) {
          std::cerr << "[ERROR] " << error.what() << '\n';
        }
      }
      else if (command == "9") {
        auto& cflags = settings.compiler.c.flags;
        auto& cppflags = settings.compiler.cpp.flags;
        auto& ldflags = settings.compiler.linking_flags;
        const std::string gc = "-Wl,--gc-sections";
        const bool enabled = std::find(ldflags.begin(), ldflags.end(), gc) != ldflags.end();
        const auto change = [](std::vector<std::string>& flags, const std::string& flag, bool add) {
          const auto it = std::find(flags.begin(), flags.end(), flag);
          if (add && it == flags.end()) flags.push_back(flag);
          if (!add && it != flags.end()) flags.erase(it);
        };
        for (auto* list : {&cflags, &cppflags}) {
          change(*list, "-ffunction-sections", !enabled);
          change(*list, "-fdata-sections", !enabled);
        }
        change(ldflags, gc, !enabled);
        std::cout << (enabled ? "[OK] Section optimization disabled.\n" :
                                "[OK] Section optimization enabled.\n");
      } else std::cout << "[ERROR] Unknown Embedded action.\n";
    }
  }

  // Профили Debug/Release/Embedded и независимые цели сборки.
  void targets() {
    for (;;) {
      std::cout << "\n[BUILD TARGETS] Active: " << settings.target_name
                << "\n  1. Select / create target\n  2. Save current target\n  3. List saved targets\n  0. Back\n";
      const auto choice = trim(read_line("Targets > "));
      if (!std::cin || choice == "0") return;
      if (choice == "1") {
        const auto name = trim(read_line("Target name (default/Debug/Release/Embedded): "));
        if (!std::cin || name.empty()) continue;
        try {
          config_t::path_for(settings.project_path, name);
          load_profile(name);
        } catch (const std::exception& error) {
          std::cerr << "[ERROR] " << error.what() << '\n';
        }
      } else if (choice == "2") {
        try { save_profile(); }
        catch (const std::exception& error) { std::cerr << "[ERROR] " << error.what() << '\n'; }
      } else if (choice == "3") {
        const auto directory = settings.project_path / ".pgr";
        if (!std::filesystem::is_directory(directory)) {
          std::cout << "[INFO] No saved targets.\n";
          continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
          if (entry.is_regular_file() && entry.path().extension() == ".cfg")
            std::cout << "  " << entry.path().stem().string() << '\n';
        }
      } else std::cout << "[ERROR] Unknown option.\n";
    }
  }

  // Явный список единиц трансляции исключает конфликт нескольких main().
  void choose_sources() {
    for (;;) {
      std::cout << "\n[SOURCES FOR TARGET " << settings.target_name << "]\n";
      if (settings.selected_sources.empty()) std::cout << "  AUTO: every supported source file\n";
      for (const auto& file : settings.selected_sources) std::cout << "  " << file << '\n';
      std::cout << "Enter relative source to toggle, 'all' for automatic scan, 0 to return.\n";
      const auto value = trim(read_line("Sources > "));
      if (!std::cin || value == "0" || value.empty()) return;
      if (value == "all") { settings.selected_sources.clear(); continue; }
      const std::filesystem::path relative(value);
      if (relative.empty() || relative.is_absolute() || relative == "." || relative == "..") {
        std::cerr << "[ERROR] Relative file path expected.\n"; continue;
      }
      bool bad = false;
      for (const auto& part : relative) if (part == ".." || part == ".") bad = true;
      const auto extension = relative.extension().string();
      if (bad || !std::filesystem::is_regular_file(settings.project_path / relative) ||
          !(extension == ".c" || extension == ".cpp" || extension == ".cc" ||
            extension == ".cxx" || extension == ".C" || extension == ".s" || extension == ".S")) {
        std::cerr << "[ERROR] Unsupported or missing source.\n"; continue;
      }
      auto& items = settings.selected_sources;
      const auto found = std::find(items.begin(), items.end(), relative);
      if (found == items.end()) items.push_back(relative);
      else items.erase(found);
    }
  }

  void help() const {
    std::cout << "\n[HELP]\n"
              << "Select a menu item by number. The menu repeats until 0 is entered.\n"
              << "Use 2 to change the project and 7 to exclude test/ or gui/.\n"
              << "For a self-build, exclude test/ (second main function).\n"
              << "GTK sources require installed GTK development libraries and flags.\n"
              << "Use 5 to inspect compiler diagnostics from the last build.\n"
              << "Unchanged files and linking are skipped automatically on subsequent builds.\n"
              << "Changing a source/header or compiler options rebuilds affected files.\n"
              << "Use 6 to remove stage outputs and cache without deleting the log.\n"
              << "Use 9 to change GCC standards, warnings, debug symbols and linker settings.\n"
              << "Use 10 for GCC tree/GIMPLE/SSA/RTL dumps and optimization/timing reports.\n"
              << "SELECTED saves six named dumps; ALL can generate hundreds of files.\n"
              << "Changing analysis mode causes recompilation; repeat builds use the cache.\n"
              << "Use 11 for ELF/PE sections, symbols, disassembly, size and map reports.\n"
              << "Embedded: choose a GNU toolchain prefix and target flags, then supply a linker script.\n"
              << "BIN/HEX conversion requires objcopy and does not by itself create bootable firmware.\n"
              << "Changing only Binutils settings does not recompile source files.\n"
              << "Use target profiles (12) to save settings; new launches load default.cfg.\n"
              << "Custom flags must be individual arguments without spaces or shell characters.\n";
  }

public:
  explicit cli_t(build_callback_t callback, settings_t initial)
    : settings(std::move(initial)), build_callback(std::move(callback)) {
    std::error_code error;
    const auto path = std::filesystem::absolute(settings.project_path, error);
    if (!error) {
      const auto canonical_path = std::filesystem::weakly_canonical(path, error);
      if (!error) settings.project_path = canonical_path;
    }
    const auto saved = config_t::path_for(settings.project_path, settings.target_name);
    if (std::filesystem::is_regular_file(saved)) load_profile(settings.target_name);
  }

  // Главный цикл; EOF корректно завершает приложение.
  int run() {
    if (!build_callback) {
      std::cerr << "[ERROR] Build callback not configured.\n";
      return 1;
    }
    int last_build_status = 0;
    for (;;) {
      menu();
      const auto command = trim(read_line("PGR > "));
      if (!std::cin) return last_build_status;
      if (command == "0" || command == "exit" || command == "quit") break;
      if (command == "1" || command == "build") last_build_status = build();
      else if (command == "2" || command == "project") select_project();
      else if (command == "3" || command == "info") project_info();
      else if (command == "4" || command == "sources") source_files();
      else if (command == "5" || command == "log") show_log();
      else if (command == "6" || command == "clean") clean();
      else if (command == "7" || command == "exclude") exclusions();
      else if (command == "8" || command == "help") help();
      else if (command == "9" || command == "settings") configure_build();
      else if (command == "10" || command == "analysis") configure_analysis();
      else if (command == "11" || command == "embedded" || command == "binary") configure_embedded();
      else if (command == "12" || command == "targets") targets();
      else if (command == "13" || command == "select") choose_sources();
      else if (!command.empty()) std::cout << "[ERROR] Unknown command. Use 8 for help.\n";
    }
    std::cout << "PGR MAKE: exit (last build status: " << last_build_status << ").\n";
    return last_build_status;
  }
};

#endif // CLI_HPP
