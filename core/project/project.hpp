#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// Описание исходного файла проекта.
struct project_source_t {
  enum class language_t {
    UNKNOWN = 0x00,
    C =       0x01,
    CPP =     0x02
  };

  std::filesystem::path path;
  language_t language = language_t::UNKNOWN;
};

class project_t {
private:
  std::filesystem::path project_root;
  std::string project_name;
  std::vector<project_source_t> project_sources;
  std::vector<std::filesystem::path> project_include_dirs;

  // Определение языка по расширению исходного файла.
  static project_source_t::language_t detect_language(
    const std::filesystem::path& source_file_path
  ) {
    const auto extension = source_file_path.extension().string();

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
    return name == ".git" || name == "build" || name == ".vscode" ||
           name == "node_modules";
  }

public:
  explicit project_t(const std::filesystem::path& root):
    project_root(std::filesystem::absolute(root).lexically_normal()),
    project_name(project_root.filename().string()) {
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
      if (it->is_directory()) {
        if (is_excluded_directory(it->path())) {
          it.disable_recursion_pending();
        }
        continue;
      }

      if (!it->is_regular_file()) {
        continue;
      }

      const auto language = detect_language(it->path());
      if (language != project_source_t::language_t::UNKNOWN) {
        project_sources.push_back({it->path(), language});
      }
    }

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
