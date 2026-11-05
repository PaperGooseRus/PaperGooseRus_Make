#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// Описание исходного файла проекта.
struct project_source_t {
  enum class language_t {
    UNKNOWN = 0x00,
    C =       0x01,
    CPP =     0x02,
    ASM =     0x03,
    ASM_CPP = 0x04
  };

  std::filesystem::path path;
  language_t language = language_t::UNKNOWN;
};

class project_t {
private:
  std::filesystem::path project_root;
  std::string project_name;
  std::string source_name;
  std::vector<project_source_t> project_sources;
  std::vector<std::filesystem::path> project_include_dirs;
  std::vector<std::filesystem::path> excluded_paths;
  std::vector<std::filesystem::path> selected_sources;

  // Определение языка по расширению исходного файла.
  static project_source_t::language_t detect_language(
    const std::filesystem::path& source_file_path
  ) {
    const auto extension = source_file_path.extension().string();

    if (extension == ".s") return project_source_t::language_t::ASM;
    if (extension == ".S") return project_source_t::language_t::ASM_CPP;

    if (extension == ".c") {
      return project_source_t::language_t::C;
    }

    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".C") {
      return project_source_t::language_t::CPP;
    }

    return project_source_t::language_t::UNKNOWN;
  }

  // Проверка принадлежности каталога к служебным директориям.
  static bool is_excluded_directory(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == ".git" || name == "build" || name == ".pgr" || name == ".vscode" ||
           name == "node_modules";
  }

public:
  explicit project_t(const std::filesystem::path& root):
    project_root(std::filesystem::weakly_canonical(root)),
    project_name(project_root.filename().string()), source_name(project_name) {
  }

  // Раздельные цели используют разные каталоги кэша и исполняемых файлов.
  void set_target(const std::string& target) {
    if (target.empty() || target == "default") { project_name = source_name; return; }
    for (const unsigned char character : target) {
      if (!std::isalnum(character) && character != '_' && character != '-')
        throw std::invalid_argument("INVALID BUILD TARGET NAME");
    }
    project_name = source_name + "__" + target;
  }

  // Исключение относительного пути из сканирования.
  void exclude(const std::filesystem::path& relative_path) {
    if (relative_path.empty() || relative_path.is_absolute()) {
      throw std::invalid_argument("INVALID EXCLUDED PATH");
    }
    for (const auto& part : relative_path) {
      if (part == "..") throw std::invalid_argument("INVALID EXCLUDED PATH");
    }
    excluded_paths.push_back(relative_path.lexically_normal());
  }

  // Ограничение списка исходников конкретной целью сборки.
  // Пустой список означает автоматическое обнаружение всех исходников.
  void select_sources(const std::vector<std::filesystem::path>& sources) {
    selected_sources.clear();
    for (const auto& source : sources) {
      const auto relative = source.lexically_normal();
      if (relative.empty() || relative.is_absolute() || relative == ".")
        throw std::invalid_argument("INVALID PROJECT SOURCE");
      for (const auto& part : relative)
        if (part == "..") throw std::invalid_argument("SOURCE OUTSIDE PROJECT");
      if (std::find(selected_sources.begin(), selected_sources.end(), relative) == selected_sources.end())
        selected_sources.push_back(relative);
    }
  }

  // Рекурсивное сканирование исходных файлов проекта.
  int scan(void) {
    project_sources.clear();
    project_include_dirs.clear();

    if (!std::filesystem::is_directory(project_root)) {
      return 0x01;
    }

    project_include_dirs.push_back(project_root);

    for (std::filesystem::recursive_directory_iterator it(
           project_root, std::filesystem::directory_options::skip_permission_denied), end;
         it != end; ++it) {
      const auto relative = it->path().lexically_relative(project_root);
      bool excluded = false;
      for (const auto& path : excluded_paths) {
        if (relative == path || (relative.string().size() > path.string().size() &&
            relative.generic_string().compare(0, path.generic_string().size() + 1, path.generic_string() + "/") == 0)) {
          excluded = true;
          break;
        }
      }
      if (it->is_directory()) {
        if (excluded || is_excluded_directory(it->path())) {
          it.disable_recursion_pending();
        }
        continue;
      }

      if (excluded || !it->is_regular_file()) {
        continue;
      }

      const auto language = detect_language(it->path());
      if (language != project_source_t::language_t::UNKNOWN &&
          (selected_sources.empty() || std::find(selected_sources.begin(), selected_sources.end(),
             relative) != selected_sources.end())) {
        project_sources.push_back({it->path(), language});
      }
    }

    // Список явно выбранных исходников не должен молча игнорировать ошибки в конфигурации.
    if (!selected_sources.empty() && project_sources.size() != selected_sources.size()) return 0x01;

    std::sort(project_sources.begin(), project_sources.end(),
      [](const project_source_t& a, const project_source_t& b) {
        return a.path.generic_string() < b.path.generic_string();
      });

    // Типовой каталог заголовочных файлов.
    if (std::filesystem::is_directory(project_root / "include")) {
      add_include_dir(project_root / "include");
    }

    return 0x00;
  }

  // Добавление пользовательского каталога заголовочных файлов.
  void add_include_dir(const std::filesystem::path& path) {
    const auto absolute_path = (path.is_absolute() ? path : project_root / path).lexically_normal();
    if (std::find(project_include_dirs.begin(), project_include_dirs.end(), absolute_path)
        == project_include_dirs.end()) {
      project_include_dirs.push_back(absolute_path);
    }
  }

  const std::filesystem::path& root(void) const { return project_root; }
  const std::string& name(void) const { return project_name; }
  const std::vector<project_source_t>& sources(void) const { return project_sources; }
  const std::vector<std::filesystem::path>& include_dirs(void) const { return project_include_dirs; }
};

#endif // PROJECT_HPP
