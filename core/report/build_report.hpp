#ifndef BUILD_REPORT_HPP
#define BUILD_REPORT_HPP

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../cache/cache.hpp"
#include "../plan/build_plan.hpp"
#include "../process/process.hpp"
#include "../project/project.hpp"

// Машинно-читаемый протокол реальных действий GCC и причин пропуска.
// Формат JSON создаётся средствами C++17, без дополнительных зависимостей.
class build_report_t {
public:
  struct operation_t {
    std::string stage;
    std::string status;
    std::string reason;
    std::filesystem::path source;
    std::filesystem::path target;
    std::string command;
    std::vector<std::string> arguments;
    std::string input_fingerprint;
    std::string output_fingerprint;
    std::string started_at;
    double duration_ms = 0.0;
    int exit_code = 0;
  };

  struct tool_t {
    std::string name;
    std::string version;
    std::string executable;
    std::string fingerprint;
  };

private:
  std::filesystem::path project_root;
  std::filesystem::path build_root;
  std::filesystem::path process_cwd;
  std::string project_name;
  std::string started_at;
  std::string finished_at;
  int exit_code = 0;
  std::vector<operation_t> operations;
  std::vector<tool_t> tools;

  // Текущий момент в UTC в формате ISO 8601.
public:
  static std::string now_utc() {
    const auto value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
  }

private:
  // JSON UTF-8: исключаем управляющие символы, корректно экранируем Windows paths.
  static std::string escape(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
      switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
          if (c < 0x20) {
            out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                << static_cast<unsigned int>(c) << std::dec;
          } else out << static_cast<char>(c);
      }
    }
    out << '"';
    return out.str();
  }

  static std::string escaped_path(const std::filesystem::path& value) {
    return escape(value.u8string());
  }

  static bool write_text(const std::filesystem::path& filename, const std::string& contents) {
    std::filesystem::create_directories(filename.parent_path());
    const auto temporary = std::filesystem::path(filename.string() + ".tmp");
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << contents;
      out.flush();
      if (!out.good()) return false;
    }
    // На Windows rename() не заменяет существующий файл без удаления.
    std::error_code error;
    std::filesystem::rename(temporary, filename, error);
    if (error) {
      std::filesystem::remove(filename, error);
      error.clear();
      std::filesystem::rename(temporary, filename, error);
    }
    return !error;
  }

  static std::vector<std::string> arguments_for(const std::string& command) {
    try { return process_t::parse(command).arguments; }
    catch (const std::exception&) { return {}; }
  }

  // Версия инструмента запрашивается без запуска shell и удаляет временный файл.
  static std::string tool_version(const std::string& tool,
                                  const std::filesystem::path& temp_file,
                                  bool gcc_driver) {
    if (tool.empty()) return "unavailable";
    process_t::command_t command;
    command.arguments = {tool};
    if (gcc_driver) command.arguments.insert(command.arguments.end(),
      {"-dumpfullversion", "-dumpversion"});
    else command.arguments.push_back("--version");
    command.stdout_file = temp_file;
    command.stderr_file = std::filesystem::path(temp_file.string() + ".err");
    int result = 1;
    try { result = process_t::run(command); }
    catch (const std::exception&) { result = 1; }
    std::ifstream input(temp_file, std::ios::binary);
    std::string version;
    if (result == 0 && input) std::getline(input, version);
    input.close();
    std::error_code ignored;
    std::filesystem::remove(temp_file, ignored);
    std::filesystem::remove(command.stderr_file, ignored);
    return version.empty() ? "unavailable" : version;
  }

public:
  build_report_t(const project_t& project, const std::filesystem::path& root):
    project_root(project.root()), build_root(root / project.name()),
    process_cwd(std::filesystem::current_path()), project_name(project.name()),
    started_at(now_utc()) {}

  // Сохраняем фактическую команду, параметры и снимки входа/результата.
  void add(const std::string& stage, const std::string& status,
           const std::string& reason, const std::filesystem::path& source,
           const std::filesystem::path& target, const std::string& command,
           double duration_ms = 0.0, int result = 0,
           const std::string& input_fingerprint = {},
           const std::string& started = {}) {
    operation_t operation;
    operation.stage = stage;
    operation.status = status;
    operation.reason = reason;
    operation.source = source;
    operation.target = target;
    operation.command = command;
    operation.arguments = arguments_for(command);
    operation.started_at = started.empty() ? now_utc() : started;
    operation.duration_ms = duration_ms;
    operation.exit_code = result;
    operation.input_fingerprint = !input_fingerprint.empty() ? input_fingerprint :
      (status == "SKIPPED" ? "VALIDATED_BY_BUILD_CACHE" : cache_t::fingerprint(source));
    operation.output_fingerprint = (status == "SKIPPED" && stage != "ASSEMBLY" && stage != "LINKING") ?
      "VALIDATED_BY_BUILD_CACHE" : cache_t::fingerprint(target);
    operations.push_back(std::move(operation));
  }

  // Идентификация фактически выбранного toolchain.
  void register_tool(const std::string& name, const std::string& command,
                     bool gcc_driver = false) {
    const auto executable = cache_t::find_tool(command);
    const auto temporary = build_root / "reports" / (name + ".probe.tmp");
    tools.push_back({name, tool_version(command, temporary, gcc_driver),
                     executable.empty() ? command : executable.u8string(),
                     cache_t::tool_fingerprint(command)});
  }

  std::size_t operation_count() const noexcept { return operations.size(); }

  void finish(int result) {
    exit_code = result;
    finished_at = now_utc();
  }

  // Дополнительно сохраняем все зависимости .d с контрольными суммами.
  bool save(const project_t& project) const {
    try {
      std::ostringstream report;
      report << "{\n  \"format\": \"pgr-build-report-v1\",\n"
             << "  \"project\": " << escape(project_name) << ",\n"
             << "  \"root\": " << escaped_path(project_root) << ",\n"
             << "  \"build_directory\": " << escaped_path(build_root) << ",\n"
             << "  \"started_at\": " << escape(started_at) << ",\n"
             << "  \"finished_at\": " << escape(finished_at) << ",\n"
             << "  \"exit_code\": " << exit_code << ",\n"
             << "  \"result\": " << escape(exit_code == 0 ? "SUCCESS" : "FAILED") << ",\n"
             << "  \"operations\": [\n";
      for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto& o = operations[i];
        report << "    {\"stage\": " << escape(o.stage)
               << ", \"status\": " << escape(o.status)
               << ", \"reason\": " << escape(o.reason)
               << ", \"source\": " << escaped_path(o.source)
               << ", \"target\": " << escaped_path(o.target)
               << ", \"command\": " << escape(o.command)
               << ", \"arguments\": [";
        for (std::size_t j = 0; j < o.arguments.size(); ++j) {
          if (j) report << ", ";
          report << escape(o.arguments[j]);
        }
        report << "], \"cwd\": " << escaped_path(project_root)
               << ", \"started_at\": " << escape(o.started_at)
               << ", \"duration_ms\": " << std::fixed << std::setprecision(3) << o.duration_ms
               << ", \"exit_code\": " << o.exit_code
               << ", \"input_fingerprint\": " << escape(o.input_fingerprint)
               << ", \"output_fingerprint\": " << escape(o.output_fingerprint) << "}";
        report << (i + 1 == operations.size() ? "\n" : ",\n");
      }
      report << "  ]\n}\n";

      std::ostringstream manifest;
      manifest << "{\n  \"format\": \"pgr-build-manifest-v1\",\n"
               << "  \"project\": " << escape(project_name) << ",\n"
               << "  \"root\": " << escaped_path(project_root) << ",\n"
               << "  \"build_directory\": " << escaped_path(build_root) << ",\n"
               << "  \"started_at\": " << escape(started_at) << ",\n"
               << "  \"finished_at\": " << escape(finished_at) << ",\n"
               << "  \"exit_code\": " << exit_code << ",\n"
               << "  \"tools\": [\n";
      for (std::size_t i = 0; i < tools.size(); ++i) {
        const auto& t = tools[i];
        manifest << "    {\"name\": " << escape(t.name)
                 << ", \"version\": " << escape(t.version)
                 << ", \"executable\": " << escape(t.executable)
                 << ", \"fingerprint\": " << escape(t.fingerprint) << "}"
                 << (i + 1 == tools.size() ? "\n" : ",\n");
      }
      manifest << "  ],\n  \"environment\": {";
      bool first_env = true;
      for (const char* key : {"PATH", "LANG", "LC_ALL", "SOURCE_DATE_EPOCH"}) {
        const char* value = std::getenv(key);
        if (value == nullptr) continue;
        if (!first_env) manifest << ", ";
        first_env = false;
        manifest << escape(key) << ": " << escape(value);
      }
      manifest << "},\n  \"sources\": [\n";
      const auto& files = project.sources();
      for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        const auto relative = file.path.lexically_relative(project.root());
        const auto dependency = build_root / "dependencies" /
          std::filesystem::path(relative.string() + ".d");
        std::vector<std::filesystem::path> dependencies;
        cache_t::read_dependencies(dependency, dependencies);
        manifest << "    {\"file\": " << escaped_path(file.path)
                 << ", \"fingerprint\": " << escape(cache_t::fingerprint(file.path))
                 << ", \"dependencies\": [";
        for (std::size_t j = 0; j < dependencies.size(); ++j) {
          if (j) manifest << ", ";
          manifest << "{\"path\": " << escaped_path(dependencies[j])
                   << ", \"fingerprint\": " << escape(cache_t::fingerprint(dependencies[j])) << "}";
        }
        manifest << "]}" << (i + 1 == files.size() ? "\n" : ",\n");
      }
      manifest << "  ],\n  \"outputs\": [\n";
      bool first = true;
      for (const auto& o : operations) {
        if (o.exit_code != 0 ||
            (o.status != "EXECUTED" && o.status != "SKIPPED")) continue;
        if (o.status == "SKIPPED" && o.stage != "ASSEMBLY" && o.stage != "LINKING") continue;
        if (!first) manifest << ",\n";
        first = false;
        manifest << "    {\"path\": " << escaped_path(o.target)
                 << ", \"fingerprint\": " << escape(o.output_fingerprint) << "}";
      }
      manifest << "\n  ]\n}\n";
      return write_text(build_root / "reports" / "build-report.json", report.str()) &&
             write_text(build_root / "build-manifest.json", manifest.str());
    } catch (const std::exception&) { return false; }
  }

  // Compile database для clangd: исходный файл + эквивалентные флаги,
  // а не промежуточный .ii. Реально запущенные argv находятся в build-report.json.
  static bool save_compile_commands(
    const project_t& project, const std::filesystem::path& build,
    const std::vector<std::pair<std::filesystem::path, std::vector<std::string>>>& entries) {
    try {
      std::ostringstream out;
      out << "[\n";
      for (std::size_t i = 0; i < entries.size(); ++i) {
        out << "  {\"directory\": " << escaped_path(project.root())
            << ", \"file\": " << escaped_path(entries[i].first)
            << ", \"arguments\": [";
        for (std::size_t j = 0; j < entries[i].second.size(); ++j) {
          if (j) out << ", ";
          out << escape(entries[i].second[j]);
        }
        out << "]}" << (i + 1 == entries.size() ? "\n" : ",\n");
      }
      out << "]\n";
      return write_text(build / "compile_commands.json", out.str());
    } catch (const std::exception&) { return false; }
  }
};

#endif // BUILD_REPORT_HPP
