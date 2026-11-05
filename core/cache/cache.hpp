#ifndef CACHE_HPP
#define CACHE_HPP

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Кэширование входных файлов: сигнатуры команд, зависимости,
// контрольные суммы исходников, библиотек и linker scripts.
// Не зависит от GCC API и интерфейса пользователя.
class cache_t {
public:

  // Совпадение команды предыдущей успешной сборки.
  static bool cache_matches(const std::filesystem::path& cache_file, const std::string& value) {
    std::ifstream file(cache_file, std::ios::binary);
    if (!file) return false;
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return !file.bad() && content == value;
  }

  // Запись данных только после успешного завершения сборки.
  static bool save_cache(const std::filesystem::path& cache_file, const std::string& value) {
    std::filesystem::create_directories(cache_file.parent_path());
    std::ofstream file(cache_file, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << value;
    file.flush();
    return file.good();
  }

  // Чтение формата Makefile .d, включая экранированные пробелы.
  static bool read_dependencies(
    const std::filesystem::path& dependency_file,
    std::vector<std::filesystem::path>& dependencies
  ) {
    std::ifstream file(dependency_file, std::ios::binary);
    if (!file) return false;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) return false;

    const std::string marker = "pgr_dependencies:";
    if (text.compare(0, marker.size(), marker) != 0) return false;

    std::string token;
    const auto append_token = [&]() {
      if (!token.empty()) {
        dependencies.emplace_back(token);
        token.clear();
      }
    };

    for (std::size_t i = marker.size(); i < text.size(); ++i) {
      const char character = text[i];
      if (character == '\n' || character == '\r') {
        append_token();
        break;  // Следующие строки от -MP не являются зависимостями цели.
      }
      if (character == '\\' && i + 1 < text.size()) {
        const char next = text[i + 1];
        if (next == '\n' || (next == '\r' && i + 2 < text.size() && text[i + 2] == '\n')) {
          i += next == '\r' ? 2 : 1;
          append_token();
          continue;
        }
        if (next == ' ' || next == '\t' || next == '#' || next == ':' || next == '\\') {
          token += next;
          ++i;
          continue;
        }
      }
      if (character == ' ' || character == '\t') append_token();
      else token += character;
    }
    append_token();
    return !dependencies.empty();
  }

  // Контроль существования результатов и временных меток.
  static bool outputs_current(
    const std::filesystem::path& preprocessed,
    const std::filesystem::path& compiled,
    const std::filesystem::path& object,
    const std::filesystem::path& dependency_file
  ) {
    std::error_code error;
    for (const auto& path : {preprocessed, compiled, object, dependency_file}) {
      if (!std::filesystem::is_regular_file(path, error) || error) return false;
    }

    const auto object_time = std::filesystem::last_write_time(object, error);
    if (error) return false;
    // Ручное изменение промежуточного результата также требует пересборки.
    for (const auto& path : {preprocessed, compiled, dependency_file}) {
      if (std::filesystem::last_write_time(path, error) > object_time || error) return false;
    }
    std::vector<std::filesystem::path> dependencies;
    if (!read_dependencies(dependency_file, dependencies)) return false;
    for (const auto& path : dependencies) {
      if (!std::filesystem::is_regular_file(path, error) || error) return false;
      if (std::filesystem::last_write_time(path, error) > object_time || error) return false;
    }
    return true;
  }

  // FNV-1a-64: обнаружение изменения содержимого при прежнем mtime.
  static std::string fingerprint(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "MISSING:" + path.generic_string();
    std::uint64_t hash = UINT64_C(14695981039346656037);
    std::uint64_t length = 0;
    char buffer[16384];
    while (input) {
      input.read(buffer, sizeof(buffer));
      const auto count = input.gcount();
      for (std::streamsize i = 0; i < count; ++i) {
        hash ^= static_cast<unsigned char>(buffer[i]);
        hash *= UINT64_C(1099511628211);
      }
      length += static_cast<std::uint64_t>(count);
    }
    if (input.bad()) return "UNREADABLE:" + path.generic_string();
    std::ostringstream stream;
    stream << path.lexically_normal().generic_string() << ':' << length << ':'
           << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
  }

  // Разрешение пути инструмента через PATH без shell.
  static std::filesystem::path find_tool(const std::string& command) {
    const std::filesystem::path executable(command);
    if (executable.has_parent_path()) return executable;
    const char* environment = std::getenv("PATH");
    if (environment == nullptr) return {};
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::istringstream directories(environment);
    std::string directory;
    while (std::getline(directories, directory, separator)) {
      if (directory.empty()) directory = ".";
      const auto base = std::filesystem::path(directory) / executable;
      if (std::filesystem::is_regular_file(base)) return base;
#ifdef _WIN32
      for (const char* extension : {".exe", ".cmd", ".bat"}) {
        const auto candidate = std::filesystem::path(base.string() + extension);
        if (std::filesystem::is_regular_file(candidate)) return candidate;
      }
#endif
    }
    return {};
  }

  // Контроль изменения файла компилятора.
  static std::string tool_fingerprint(const std::string& tool) {
    const auto path = find_tool(tool);
    return path.empty() ? "NOT_FOUND:" + tool : fingerprint(path);
  }

  // Контроль всех исходников и заголовков из .d.
  static std::string dependency_fingerprints(const std::filesystem::path& depfile) {
    std::vector<std::filesystem::path> dependencies;
    if (!read_dependencies(depfile, dependencies)) return {};
    std::string result = "PGR-INPUTS-V2\n";
    for (const auto& dependency : dependencies) {
      if (!std::filesystem::is_regular_file(dependency)) return {};
      result += fingerprint(dependency) + "\n";
    }
    return result;
  }

  // Поиск статической/динамической библиотеки в -L каталогах.
  static std::filesystem::path resolve_library(
    const std::string& name,
    const std::vector<std::filesystem::path>& directories
  ) {
    for (const auto& directory : directories) {
      const std::string filename = name.size() > 1 && name.front() == ':' ? name.substr(1) : "";
      const std::vector<std::string> variants = filename.empty()
        ? std::vector<std::string>{"lib" + name + ".a", "lib" + name + ".so",
                                   "lib" + name + ".dll.a", name + ".lib"}
        : std::vector<std::string>{filename};
      for (const auto& variant : variants) {
        const auto candidate = directory / variant;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
      }
    }
    return {};
  }

  // Рекурсивная проверка INCLUDE внутри GNU ld script.
  static void script_fingerprints(
    const std::filesystem::path& script,
    std::set<std::string>& visited,
    std::string& result
  ) {
    const auto absolute = std::filesystem::absolute(script).lexically_normal();
    if (!visited.insert(absolute.generic_string()).second) return;
    result += fingerprint(absolute) + "\n";
    std::ifstream file(absolute);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
      const auto comment = line.find("/*");
      if (comment != std::string::npos) line.resize(comment);
      std::istringstream words(line);
      std::string word;
      while (words >> word) {
        if (word != "INCLUDE") continue;
        std::string name;
        if (!(words >> std::quoted(name))) break;
        if (name.empty()) break;
        std::filesystem::path child(name);
        if (!child.is_absolute()) child = absolute.parent_path() / child;
        script_fingerprints(child, visited, result);
      }
    }
  }

};

#endif // CACHE_HPP
