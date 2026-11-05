#ifndef GCC_HPP
#define GCC_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <functional>
#include <utility>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <set>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "../project/project.hpp"
#include "../binary/binary.hpp"
#include "../process/process.hpp"
#include "../cache/cache.hpp"

class gcc_t {
public:
  enum class language_t {
    UNKNOWN = 0x00,
    C =       0x01,
    CPP =     0x02,
    ASM =     0x03,
    ASM_CPP = 0x04
  };

  enum class standart_t {
    C11 =   0x01,
    C17 =   0x02,
    C23 =   0x03,
    C99 =   0x04,
    CPP11 = 0x11,
    CPP17 = 0x12,
    CPP23 = 0x13,
    CPP14 = 0x14,
    CPP20 = 0x15
  };

  enum class optimization_t {
    O0 = 0x00,
    O1 = 0x01,
    O2 = 0x02,
    O3 = 0x03,
    OS = 0x04,
    OG = 0x05
  };

  enum class warning_t {
    DEFAULT = 0x00,      //!< Предупреждения GCC по умолчанию.
    WALL =    0x01,      //!< -Wall.
    EXTRA =   0x02,      //!< -Wall -Wextra.
    STRICT_WARNINGS = 0x03       //!< -Wall -Wextra -Wpedantic.
  };

  enum class debug_t {
    OFF = 0x00,          //!< Без отладочной информации.
    G =   0x01,          //!< -g.
    G3 =  0x03           //!< -g3, включая определения макросов.
  };

  enum class gcc_step_t {
    INACTIVE =      0x00,             //   Инструмент                   // Команда                  // Что происходит
    PREPROCESSING = 0x01,             //!< cpp (или cc1 -E)             // cpp main.c -o main.i     // Раскрытие макросов, #include, условная компиляция.
    COMPILATION =   0x02,             //!< cc1 (для C) / cc1plus (C++)  // cc1 main.i -o main.s     // Трансляция C/C++ в ассемблер.
    ASSEMBLY =      0x03,             //!< as                           // as main.s -o main.o      // Перевод ассемблерного текста в машинный код.
    LINKING =       0x04              //!< collect2 / ld                // ld main.o -o app         // Объединение объектных файлов и библиотек.
  };

  enum class gcc_compilation_step_t {
    INACTIVE                 = 0x00,
    LEXICAL_ANALYSIS         = 0x01,  //!< Lexer                        // Tokens                   // Лексический анализ.
    SYNTAX_ANALYSIS          = 0x02,  //!< Parser                       // Syntax Tree              // Синтаксический анализ.
    SEMANTIC_ANALYSIS        = 0x03,  //!< Semantic Analyzer            // TREE                     // Семантический анализ.
    GENERIC_GENERATION       = 0x04,  //!< Front End                    // GENERIC                  // Формирование GENERIC.
    GIMPLE_GENERATION        = 0x05,  //!< Gimplifier                   // GENERIC -> GIMPLE        // Формирование GIMPLE.
    SSA_CONSTRUCTION         = 0x06,  //!< SSA Passes                   // GIMPLE -> GIMPLE SSA     // Построение SSA.
    GIMPLE_OPTIMIZATION      = 0x07,  //!< Tree Optimization Passes     // GIMPLE SSA               // Оптимизация GIMPLE.
    INTERPROCEDURAL_ANALYSIS = 0x08,  //!< IPA Passes                   // GIMPLE / Call Graph      // Межпроцедурный анализ.
    RTL_GENERATION           = 0x09,  //!< RTL Expander                 // GIMPLE -> RTL            // Формирование RTL.
    RTL_OPTIMIZATION         = 0x0A,  //!< RTL Optimization Passes      // RTL                      // Оптимизация RTL.
    REGISTER_ALLOCATION      = 0x0B,  //!< Register Allocator           // RTL -> Hard Registers    // Распределение регистров.
    INSTRUCTION_SCHEDULING   = 0x0C,  //!< Instruction Scheduler        // RTL                      // Планирование инструкций.
    ASSEMBLY_GENERATION      = 0x0D   //!< Final Pass                   // RTL -> Assembly (.s)     // Генерация ассемблерного текста.
  };

  // Настройки компилятора для конкретного языка.
  struct gcc_element_t {
    language_t language = language_t::UNKNOWN;
    standart_t standart = standart_t::CPP17;
    optimization_t optimization = optimization_t::O0;
    std::vector<std::string> flags;
    warning_t warnings = warning_t::DEFAULT;
    debug_t debug = debug_t::OFF;
    bool warnings_as_errors = false;
  };

  // Режим сохранения промежуточных представлений GCC.
  enum class analysis_mode_t {
    OFF =      0x00,  //!< Исследование выключено.
    SELECTED = 0x01,  //!< Frontend tree, GIMPLE, SSA, optimized GIMPLE и RTL.
    ALL =      0x02   //!< Все доступные tree/RTL проходы; число зависит от версии GCC.
  };

  struct analysis_settings_t {
    analysis_mode_t mode = analysis_mode_t::OFF;
    bool optimization_report = true; //!< Отчёт об успешных и пропущенных оптимизациях.
    bool time_report = true;         //!< Время компиляции (-ftime-report).
  };

  // Единая конфигурация сборки C, C++ и компоновщика.
  struct build_settings_t {
    gcc_element_t c = {language_t::C, standart_t::C17, optimization_t::O0, {}};
    gcc_element_t cpp = {language_t::CPP, standart_t::CPP17, optimization_t::O0, {}};
    std::vector<std::string> linking_flags;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::filesystem::path> library_dirs;
    std::vector<std::string> libraries;
    std::string executable_name = "app.exe";
    std::string gcc_command = "gcc";
    std::string gpp_command = "g++";
    analysis_settings_t analysis;
    std::vector<std::string> target_flags; //!< Общие флаги целевой архитектуры: -mcpu=..., -mthumb и др.
    std::filesystem::path linker_script;  //!< Дополнительный GNU ld script (-T).
    binary_t::settings_t binary;          //!< Настройки анализа ELF/PE и образов BIN/HEX.
  };

private:
  gcc_step_t gcc_step = gcc_step_t::INACTIVE;
  gcc_compilation_step_t gcc_compilation_step = gcc_compilation_step_t::INACTIVE;

  std::filesystem::path main_dir_to_build = "C:/PGR/build";                //!< Общая директория для всех проектов.
  const std::filesystem::path preprocessing_dir_to_build = "preprocessing"; //!< Директория этапа PREPROCESSING.
  const std::filesystem::path compilation_dir_to_build = "compilation";     //!< Директория этапа COMPILATION.
  const std::filesystem::path assembly_dir_to_build = "assembly";           //!< Директория этапа ASSEMBLY.
  const std::filesystem::path linking_dir_to_build = "linking";             //!< Директория этапа LINKING.
  const std::filesystem::path dependencies_dir_to_build = "dependencies";   //!< Зависимости исходных файлов (.d).
  const std::filesystem::path cache_dir_to_build = "cache";                 //!< Команды предыдущей успешной сборки (.cmd).
  const std::filesystem::path analysis_dir_to_build = "analysis";           //!< Промежуточные представления GCC и отчёты.
  const std::filesystem::path binary_dir_to_build = "binary";               //!< Отчёты ELF/PE и прошивки BIN/HEX.

  build_settings_t build_settings;
  std::function<int(const std::string&)> command_executor;

  // Проверка совместимости стандарта и языка.
  static bool is_valid_standart(language_t language, standart_t standart) {
    const bool is_c = standart == standart_t::C99 || standart == standart_t::C11 ||
      standart == standart_t::C17 || standart == standart_t::C23;
    const bool is_cpp = standart == standart_t::CPP11 || standart == standart_t::CPP14 ||
      standart == standart_t::CPP17 || standart == standart_t::CPP20 || standart == standart_t::CPP23;
    return (language == language_t::C && is_c) || (language == language_t::CPP && is_cpp);
  }

  // Преобразование языка, определённого анализатором проекта.
  static language_t convert_language(project_source_t::language_t language) {
    switch (language) {
      case project_source_t::language_t::C: return language_t::C;
      case project_source_t::language_t::CPP: return language_t::CPP;
      case project_source_t::language_t::ASM: return language_t::ASM;
      case project_source_t::language_t::ASM_CPP: return language_t::ASM_CPP;
      default: return language_t::UNKNOWN;
    }
  }

  // Экранирование путей и имён исполняемых файлов для командной строки.
  // Для произвольного недоверенного ввода необходим запуск без командной оболочки.
  static std::string quote(const std::filesystem::path& path) {
    const auto value = path.string();
    if (value.empty() || value.find('"') != std::string::npos ||
        value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
      throw std::invalid_argument("UNSUPPORTED CHARACTERS IN COMMAND ARGUMENT");
    }
    return "\"" + value + "\"";
  }

  // Пользовательский флаг задаётся одним аргументом без пробелов и управляющих символов оболочки.
  static void validate_flag(const std::string& flag) {
    if (flag.empty() || flag.front() != '-') {
      throw std::invalid_argument("GCC FLAG MUST START WITH '-'");
    }
    for (const unsigned char character : flag) {
      if (std::isspace(character) || character == '"' || character == '\'' ||
          character == '&' || character == '|' || character == '<' ||
          character == '>' || character == '^' || character == '%' ||
          character == '!' || character == '$' || character == '`' ||
          character == ';' || character == '(' || character == ')') {
        throw std::invalid_argument("UNSUPPORTED CHARACTERS IN GCC FLAG: " + flag);
      }
    }
  }

  // Формирование пути результата с сохранением структуры исходных каталогов.
  static std::filesystem::path relative_source_path(
    const std::filesystem::path& source,
    const std::filesystem::path& root
  ) {
    const auto relative = source.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
      throw std::invalid_argument("SOURCE FILE IS OUTSIDE PROJECT DIRECTORY");
    }
    for (const auto& component : relative) {
      if (component == "..") {
        throw std::invalid_argument("SOURCE FILE IS OUTSIDE PROJECT DIRECTORY");
      }
    }
    return relative;
  }

  // Формирование пути промежуточного файла.
  std::filesystem::path output_path(
    const project_t& project,
    const std::filesystem::path& source,
    const std::filesystem::path& stage_dir,
    const std::string& extension
  ) const {
    auto relative = relative_source_path(source, project.root());
    relative += extension;
    return main_dir_to_build / project.name() / stage_dir / relative;
  }

  static void prepare_target(const std::filesystem::path& target) {
    std::filesystem::create_directories(target.parent_path());
  }

  static bool check_source(const std::filesystem::path& source) {
    if (!std::filesystem::is_regular_file(source)) {
      std::cerr << "ERROR: SOURCE FILE NOT FOUND: " << source << std::endl;
      return false;
    }
    return true;
  }

  // На POSIX system() возвращает wait status, а не непосредственный exit code.
  static int normalize_exit_code(int status) {
#ifdef _WIN32
    return status;
#else
    if (status == -1) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
  }

  // Централизованное выполнение команды GCC.
  int execute(
    const std::string& cmd,
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const char* stage
  ) {
    std::cout << "[SOURCE] -> " << source << std::endl;
    std::cout << "[TARGET] -> " << target << std::endl;
    std::cout << "[CMD]    -> " << cmd << std::endl;

    const int error = command_executor ? command_executor(cmd) : process_t::run(cmd);
    if (error != 0x00) std::cerr << "ERROR: " << stage << " -> " << error << std::endl;
    return error;
  }

  const gcc_element_t& settings_for(language_t language) const {
    if (language == language_t::C) return build_settings.c;
    if (language == language_t::CPP) return build_settings.cpp;
    throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  const std::string& driver_for(language_t language) const {
    if (language == language_t::C) return build_settings.gcc_command;
    if (language == language_t::CPP) return build_settings.gpp_command;
    throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  // Подготовка каталогов заголовочных файлов для текущего проекта.
  std::vector<std::filesystem::path> include_directories(const project_t& project) const {
    std::vector<std::filesystem::path> dirs = project.include_dirs();
    for (const auto& dir : build_settings.include_dirs) {
      if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) dirs.push_back(dir);
    }
    return dirs;
  }

  // Формирование команд вынесено отдельно от их выполнения для сравнения с кэшем.
  std::string preprocessing_command(
    const project_t& project,
    const std::filesystem::path& source,
    language_t language,
    const std::filesystem::path& target,
    const std::filesystem::path& dependency_file
  ) const {
    const auto& settings = settings_for(language);
    std::string cmd = quote(driver_for(language)) + " -E";
    for (const auto& flag : compiler_flags(settings, false)) cmd += " " + flag;
    for (const auto& flag : build_settings.target_flags) cmd += " " + flag;
    for (const auto& dir : include_directories(project)) cmd += " -I" + quote(dir);

    // -MT задаёт постоянное имя цели, не содержащее двоеточия диска Windows.
    // -MP создаёт фиктивные правила для удалённых заголовочных файлов.
    cmd += " -MMD -MP -MF " + quote(dependency_file) + " -MT pgr_dependencies";
    cmd += " " + quote(source) + " -o " + quote(target);
    return cmd;
  }

  // Директория исследования конкретного исходного файла, включая его расширение.
  std::filesystem::path analysis_path(
    const project_t& project,
    const std::filesystem::path& source
  ) const {
    return main_dir_to_build / project.name() / analysis_dir_to_build /
      relative_source_path(source, project.root());
  }

  static const char* analysis_mode_name(analysis_mode_t mode) {
    switch (mode) {
      case analysis_mode_t::OFF: return "OFF";
      case analysis_mode_t::SELECTED: return "SELECTED";
      case analysis_mode_t::ALL: return "ALL";
    }
    return "UNKNOWN";
  }

  // Явные имена отчётов исключают конфликты между одинаковыми именами исходников.
  std::string compilation_command(
    const std::filesystem::path& source,
    language_t language,
    const std::filesystem::path& target,
    const std::filesystem::path& report_dir
  ) const {
    const auto& settings = settings_for(language);
    std::string cmd = quote(driver_for(language)) + " -S -x ";
    cmd += language == language_t::C ? "cpp-output" : "c++-cpp-output";
    for (const auto& flag : compiler_flags(settings, true)) cmd += " " + flag;
    for (const auto& flag : build_settings.target_flags) cmd += " " + flag;

    const auto& analysis = build_settings.analysis;
    if (analysis.mode == analysis_mode_t::SELECTED) {
      cmd += " -fdump-tree-original=" + quote(report_dir / "original.txt");
      cmd += " -fdump-tree-gimple=" + quote(report_dir / "gimple.txt");
      cmd += " -fdump-tree-ssa=" + quote(report_dir / "ssa.txt");
      cmd += " -fdump-tree-optimized=" + quote(report_dir / "optimized.txt");
      cmd += " -fdump-rtl-expand=" + quote(report_dir / "rtl-expand.txt");
      cmd += " -fdump-rtl-final=" + quote(report_dir / "rtl-final.txt");
    } else if (analysis.mode == analysis_mode_t::ALL) {
      // Выборочные -fdump-*=file не совмещаем с -fdump-*-all.
      // -dumpdir и -dumpbase группируют нумерованные дампы по исходнику.
      cmd += " -fdump-tree-all -fdump-rtl-all";
      cmd += " -dumpdir " + quote(report_dir / "full" / "");
      cmd += " -dumpbase " + quote(source.filename());
    }
    if (analysis.mode != analysis_mode_t::OFF) {
      if (analysis.optimization_report) {
        cmd += " -fopt-info-all=" + quote(report_dir / "optimizations.txt");
      }
      if (analysis.time_report) cmd += " -ftime-report";
    }
    cmd += " " + quote(source) + " -o " + quote(target);
    return cmd;
  }

  // При -ftime-report GCC пишет информацию в stderr. Сохраняем stderr в файл,
  // затем повторяем его в терминал/лог. Скобки сохраняют перенаправление,
  // даже если исполнитель внешних процессов дополнительно применяет 2>&1.
  int execute_compilation(
    const std::string& command,
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const std::filesystem::path& report_dir
  ) {
    const bool timing = build_settings.analysis.mode != analysis_mode_t::OFF &&
      build_settings.analysis.time_report;
    if (!timing) return execute(command, source, target, "COMPILATION");

    const auto stderr_file = report_dir / "timing.txt";
    const std::string wrapped = "(" + command + " 2> " + quote(stderr_file) + ")";
    const int result = execute(wrapped, source, target, "COMPILATION");
    std::ifstream captured(stderr_file, std::ios::binary);
    if (!captured) {
      std::cerr << "[ERROR] GCC STDERR REPORT NOT FOUND: " << stderr_file << '\n';
      return result == 0 ? 1 : result;
    }
    // Показываем и ошибки компилятора: перенаправление не должно их скрывать.
    std::cerr << captured.rdbuf() << std::flush;
    return result;
  }

  // Создаём пояснения, если выбранный проход не выполнялся при данном -O.
  // Так отсутствие дампа не будет вызывать бесконечную пересборку.
  static bool complete_analysis_reports(
    const std::filesystem::path& report_dir,
    const analysis_settings_t& settings
  ) {
    if (settings.mode == analysis_mode_t::OFF) return true;
    if (settings.mode == analysis_mode_t::SELECTED) {
      for (const char* name : {"original.txt", "gimple.txt", "ssa.txt", "optimized.txt",
                               "rtl-expand.txt", "rtl-final.txt"}) {
        const auto path = report_dir / name;
        if (std::filesystem::exists(path)) continue;
        std::ofstream file(path);
        if (!file) return false;
        file << "GCC did not emit this pass with the current compiler settings.\n";
      }
    }
    if (settings.optimization_report &&
        !std::filesystem::exists(report_dir / "optimizations.txt")) {
      std::ofstream file(report_dir / "optimizations.txt");
      if (!file) return false;
      file << "GCC reported no applicable optimization decisions.\n";
    }
    if (settings.mode == analysis_mode_t::ALL) {
      std::error_code error;
      auto begin = std::filesystem::directory_iterator(report_dir / "full", error);
      if (error || begin == std::filesystem::directory_iterator{}) {
        std::ofstream file(report_dir / "full" / "no-passes.txt");
        if (!file) return false;
        file << "GCC did not produce tree or RTL dumps.\n";
      }
    }
    // Манифест содержит каждый созданный дамп, чтобы удалённый файл
    // обнаруживался даже в режиме ALL с сотнями внутренних проходов GCC.
    std::vector<std::string> produced;
    for (const auto& item : std::filesystem::recursive_directory_iterator(report_dir)) {
      if (item.is_regular_file() && item.path().filename() != "analysis.ok") {
        produced.push_back(item.path().lexically_relative(report_dir).generic_string());
      }
    }
    std::sort(produced.begin(), produced.end());
    std::ofstream marker(report_dir / "analysis.ok", std::ios::trunc);
    if (!marker) return false;
    marker << "PGR-ANALYSIS-V2\n" << analysis_mode_name(settings.mode) << '\n';
    for (const auto& file : produced) marker << file << '\n';
    return marker.good() && !produced.empty();
  }

  // В кэше учитываем исчезновение отчётов, а не только объектных файлов.
  static bool analysis_current(
    const std::filesystem::path& report_dir,
    const analysis_settings_t& settings
  ) {
    if (settings.mode == analysis_mode_t::OFF) return true;
    std::ifstream marker(report_dir / "analysis.ok");
    if (!marker) return false;
    std::string version, mode, path;
    if (!std::getline(marker, version) || version != "PGR-ANALYSIS-V2" ||
        !std::getline(marker, mode) || mode != analysis_mode_name(settings.mode)) return false;
    std::size_t recorded = 0;
    while (std::getline(marker, path)) {
      if (path.empty()) return false;
      const std::filesystem::path relative(path);
      if (relative.is_absolute() || relative.has_root_path()) return false;
      for (const auto& part : relative) if (part == "..") return false;
      if (!std::filesystem::is_regular_file(report_dir / relative)) return false;
      ++recorded;
    }
    if (marker.bad() || recorded == 0) return false;
    if (settings.mode == analysis_mode_t::SELECTED) {
      for (const char* name : {"original.txt", "gimple.txt", "ssa.txt", "optimized.txt",
                               "rtl-expand.txt", "rtl-final.txt"}) {
        if (!std::filesystem::is_regular_file(report_dir / name)) return false;
      }
    }
    if (settings.optimization_report &&
        !std::filesystem::is_regular_file(report_dir / "optimizations.txt")) return false;
    if (settings.time_report &&
        !std::filesystem::is_regular_file(report_dir / "timing.txt")) return false;
    return true;
  }

  std::string assembly_command(
    const std::filesystem::path& source,
    const std::filesystem::path& target
  ) const {
    std::string cmd = quote(build_settings.gcc_command) + " -c -x assembler";
    for (const auto& flag : build_settings.target_flags) cmd += " " + flag;
    cmd += " " + quote(source) + " -o " + quote(target);
    return cmd;
  }

  std::string linking_command(
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& target,
    bool has_cpp
  ) const {
    std::string cmd = quote(has_cpp ? build_settings.gpp_command : build_settings.gcc_command);
    for (const auto& object : objects) cmd += " " + quote(object);
    for (const auto& flag : build_settings.target_flags) cmd += " " + flag;
    if (!build_settings.linker_script.empty()) cmd += " -T " + quote(build_settings.linker_script);
    if (build_settings.binary.linker_map) {
      const auto map = target.parent_path() / (target.stem().string() + ".map");
      cmd += " -Wl,-Map=" + quote(map);
    }
    for (const auto& dir : build_settings.library_dirs) cmd += " -L" + quote(dir);
    for (const auto& flag : build_settings.linking_flags) cmd += " " + flag;
    for (const auto& library : build_settings.libraries) cmd += " -l" + library;
    cmd += " -o " + quote(target);
    return cmd;
  }





















  std::string link_input_fingerprints() const {
    std::string result = "PGR-LINK-INPUTS-V2\n";
    result += cache_t::tool_fingerprint(build_settings.gcc_command) + "\n";
    result += cache_t::tool_fingerprint(build_settings.gpp_command) + "\n";
    std::set<std::string> visited;
    if (!build_settings.linker_script.empty()) {
      cache_t::script_fingerprints(build_settings.linker_script, visited, result);
    }
    for (const auto& library : build_settings.libraries) {
      const auto file = cache_t::resolve_library(library, build_settings.library_dirs);
      result += file.empty() ? "SYSTEM-LIBRARY:" + library + "\n" : cache_t::fingerprint(file) + "\n";
    }
    for (const auto& flag : build_settings.linking_flags) {
      if (flag.size() > 2 && flag.substr(0, 2) == "-l") {
        const auto file = cache_t::resolve_library(flag.substr(2), build_settings.library_dirs);
        result += file.empty() ? "SYSTEM-LIBRARY:" + flag + "\n" : cache_t::fingerprint(file) + "\n";
      }
    }
    return result;
  }

  // Препроцессирование .S с обработкой #include и макросов ассемблера.
  int preprocessing_assembler(
    const project_t& project,
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const std::filesystem::path& dependency_file
  ) {
    gcc_step = gcc_step_t::PREPROCESSING;
    std::cout << "GCC START PREPROCESSING (ASM) ->" << std::endl;
    if (!check_source(source)) return 1;
    prepare_target(target);
    prepare_target(dependency_file);
    std::string command = quote(build_settings.gcc_command) + " -E -x assembler-with-cpp";
    for (const auto& flag : build_settings.target_flags) command += " " + flag;
    for (const auto& dir : include_directories(project)) command += " -I" + quote(dir);
    command += " -MMD -MP -MF " + quote(dependency_file) + " -MT pgr_dependencies";
    command += " " + quote(source) + " -o " + quote(target);
    return execute(command, source, target, "PREPROCESSING");
  }

  // Этап PREPROCESSING.
  int preprocessing(
    const project_t& project,
    const std::filesystem::path& source_file_path,
    language_t language,
    const std::filesystem::path& target,
    const std::filesystem::path& dependency_file
  ) {
    std::cout << "GCC START PREPROCESSING ->" << std::endl;
    gcc_step = gcc_step_t::PREPROCESSING;
    if (!check_source(source_file_path)) return 0x01;
    prepare_target(target);
    prepare_target(dependency_file);
    return execute(preprocessing_command(project, source_file_path, language, target, dependency_file),
      source_file_path, target, "PREPROCESSING");
  }

  // Этап COMPILATION.
  int compilation(
    const std::filesystem::path& source_file_path,
    language_t language,
    const std::filesystem::path& target,
    const std::filesystem::path& report_dir
  ) {
    std::cout << "GCC START COMPILATION ->" << std::endl;
    gcc_step = gcc_step_t::COMPILATION;
    if (!check_source(source_file_path)) return 0x01;
    prepare_target(target);
    if (build_settings.analysis.mode != analysis_mode_t::OFF) {
      std::filesystem::create_directories(report_dir);
      if (build_settings.analysis.mode == analysis_mode_t::ALL) {
        std::filesystem::create_directories(report_dir / "full");
      }
    }
    const int result = execute_compilation(
      compilation_command(source_file_path, language, target, report_dir),
      source_file_path, target, report_dir);
    if (result != 0) return result;
    if (!complete_analysis_reports(report_dir, build_settings.analysis)) {
      std::cerr << "ERROR: CANNOT SAVE GCC ANALYSIS: " << report_dir << '\n';
      return 1;
    }
    return 0;
  }

  // Этап ASSEMBLY.
  int assembly(
    const std::filesystem::path& source_file_path,
    const std::filesystem::path& target
  ) {
    std::cout << "GCC START ASSEMBLY ->" << std::endl;
    gcc_step = gcc_step_t::ASSEMBLY;
    if (!check_source(source_file_path)) return 0x01;
    prepare_target(target);
    return execute(assembly_command(source_file_path, target), source_file_path, target, "ASSEMBLY");
  }

  // Этап LINKING.
  int linking(
    const std::vector<std::filesystem::path>& source_files,
    const std::filesystem::path& target,
    bool has_cpp
  ) {
    std::cout << "GCC START LINKING ->" << std::endl;
    gcc_step = gcc_step_t::LINKING;
    if (source_files.empty()) {
      std::cerr << "ERROR: NO OBJECT FILES TO LINK" << std::endl;
      return 0x01;
    }
    for (const auto& source : source_files) {
      if (!check_source(source)) return 0x01;
    }
    prepare_target(target);
    return execute(linking_command(source_files, target, has_cpp),
      source_files.front(), target, "LINKING");
  }

public:
  gcc_t(void) {
    std::cout << "GCC constructor.\n";
  }

  // Формирование параметра стандарта языка.
  static std::string standart_key(standart_t standart) {
    switch (standart) {
      case standart_t::C99:   return "-std=c99";
      case standart_t::C11:   return "-std=c11";
      case standart_t::C17:   return "-std=c17";
      case standart_t::C23:   return "-std=c2x"; // Совместимость с GCC 13; C23 в GCC 13 ещё экспериментален.
      case standart_t::CPP11: return "-std=c++11";
      case standart_t::CPP14: return "-std=c++14";
      case standart_t::CPP17: return "-std=c++17";
      case standart_t::CPP20: return "-std=c++20";
      case standart_t::CPP23: return "-std=c++23";
    }
    throw std::invalid_argument("UNKNOWN GCC STANDARD");
  }

  static std::string optimization_key(optimization_t optimization) {
    switch (optimization) {
      case optimization_t::O0: return "-O0";
      case optimization_t::O1: return "-O1";
      case optimization_t::O2: return "-O2";
      case optimization_t::O3: return "-O3";
      case optimization_t::OS: return "-Os";
      case optimization_t::OG: return "-Og";
    }
    throw std::invalid_argument("UNKNOWN GCC OPTIMIZATION");
  }

  static std::string warning_name(warning_t warnings) {
    switch (warnings) {
      case warning_t::DEFAULT: return "default";
      case warning_t::WALL: return "-Wall";
      case warning_t::EXTRA: return "-Wall -Wextra";
      case warning_t::STRICT_WARNINGS: return "-Wall -Wextra -Wpedantic";
    }
    throw std::invalid_argument("UNKNOWN WARNING MODE");
  }

  static std::string debug_name(debug_t debug) {
    switch (debug) {
      case debug_t::OFF: return "off";
      case debug_t::G: return "-g";
      case debug_t::G3: return "-g3";
    }
    throw std::invalid_argument("UNKNOWN DEBUG MODE");
  }

  // Флаги, общие для препроцессирования и компиляции; отладочные символы нужны на этапе COMPILATION.
  static std::vector<std::string> compiler_flags(const gcc_element_t& settings, bool include_debug = true) {
    std::vector<std::string> flags = {standart_key(settings.standart), optimization_key(settings.optimization)};
    switch (settings.warnings) {
      case warning_t::DEFAULT: break;
      case warning_t::WALL: flags.push_back("-Wall"); break;
      case warning_t::EXTRA: flags.insert(flags.end(), {"-Wall", "-Wextra"}); break;
      case warning_t::STRICT_WARNINGS: flags.insert(flags.end(), {"-Wall", "-Wextra", "-Wpedantic"}); break;
      default: throw std::invalid_argument("UNKNOWN WARNING MODE");
    }
    if (settings.warnings_as_errors) flags.push_back("-Werror");
    if (include_debug) {
      if (settings.debug == debug_t::G) flags.push_back("-g");
      else if (settings.debug == debug_t::G3) flags.push_back("-g3");
      else if (settings.debug != debug_t::OFF) throw std::invalid_argument("UNKNOWN DEBUG MODE");
    }
    for (const auto& flag : settings.flags) {
      validate_flag(flag);
      flags.push_back(flag);
    }
    return flags;
  }

  static std::string analysis_name(analysis_mode_t mode) {
    return analysis_mode_name(mode);
  }

  static std::string compiler_flags_text(const gcc_element_t& settings) {
    std::string result;
    for (const auto& flag : compiler_flags(settings)) {
      if (!result.empty()) result += ' ';
      result += flag;
    }
    return result;
  }

  // Проверка всей конфигурации до начала сборки.
  static void validate_settings(const build_settings_t& settings) {
    if (settings.c.language != language_t::C ||
        !is_valid_standart(settings.c.language, settings.c.standart) ||
        settings.cpp.language != language_t::CPP ||
        !is_valid_standart(settings.cpp.language, settings.cpp.standart)) {
      throw std::invalid_argument("GCC STANDARD DOES NOT MATCH LANGUAGE");
    }
    compiler_flags(settings.c);
    compiler_flags(settings.cpp);
    if (settings.analysis.mode != analysis_mode_t::OFF &&
        settings.analysis.mode != analysis_mode_t::SELECTED &&
        settings.analysis.mode != analysis_mode_t::ALL) {
      throw std::invalid_argument("UNKNOWN GCC ANALYSIS MODE");
    }
    for (const auto& flag : settings.linking_flags) validate_flag(flag);
    for (const auto& flag : settings.target_flags) validate_flag(flag);
    binary_t::validate_settings(settings.binary);
    if (!settings.linker_script.empty()) quote(settings.linker_script);
    for (const auto& library : settings.libraries) {
      if (library.empty()) throw std::invalid_argument("EMPTY LIBRARY NAME");
      for (const unsigned char character : library) {
        if (!std::isalnum(character) && character != '_' && character != '-' &&
            character != '+' && character != '.') {
          throw std::invalid_argument("INVALID LIBRARY NAME: " + library);
        }
      }
    }
    const std::filesystem::path executable(settings.executable_name);
    if (settings.executable_name.empty() || settings.executable_name == "." ||
        settings.executable_name == ".." || executable != executable.filename()) {
      throw std::invalid_argument("INVALID EXECUTABLE NAME");
    }
    quote(settings.gcc_command);
    quote(settings.gpp_command);
    quote(settings.executable_name);
    for (const auto& dir : settings.include_dirs) quote(dir);
    for (const auto& dir : settings.library_dirs) quote(dir);
  }

  void set_command_executor(std::function<int(const std::string&)> executor) {
    command_executor = std::move(executor);
  }

  void set_build_dir(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("EMPTY BUILD DIRECTORY");
    main_dir_to_build = std::filesystem::absolute(path).lexically_normal();
  }

  void set_build_settings(const build_settings_t& settings) {
    validate_settings(settings);
    build_settings = settings;
  }

  const build_settings_t& get_build_settings() const noexcept { return build_settings; }

  // Совместимость с прежними методами конфигурации.
  void set_language_settings(const gcc_element_t& settings) {
    if (!is_valid_standart(settings.language, settings.standart)) {
      throw std::invalid_argument("GCC STANDARD DOES NOT MATCH LANGUAGE");
    }
    compiler_flags(settings);
    if (settings.language == language_t::C) build_settings.c = settings;
    else if (settings.language == language_t::CPP) build_settings.cpp = settings;
    else throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  void set_linking_flags(const std::vector<std::string>& flags) {
    for (const auto& flag : flags) validate_flag(flag);
    build_settings.linking_flags = flags;
  }
  void set_executable_name(const std::string& name) {
    auto updated = build_settings;
    updated.executable_name = name;
    validate_settings(updated);
    build_settings = std::move(updated);
  }
  void set_compilers(const std::string& gcc, const std::string& gpp) {
    auto updated = build_settings;
    updated.gcc_command = gcc;
    updated.gpp_command = gpp;
    validate_settings(updated);
    build_settings = std::move(updated);
  }

  gcc_step_t current_step(void) const { return gcc_step; }
  gcc_compilation_step_t current_compilation_step(void) const { return gcc_compilation_step; }

  // Инкрементальная сборка проекта, предварительно обработанного project_t.
  int run(const project_t& project) {
    gcc_step = gcc_step_t::INACTIVE;
    gcc_compilation_step = gcc_compilation_step_t::INACTIVE;
    validate_settings(build_settings);

    if (project.sources().empty()) {
      std::cerr << "ERROR: PROJECT HAS NO SOURCE FILES" << std::endl;
      return 0x01;
    }

    std::cout << "[CONFIG] C:   " << compiler_flags_text(build_settings.c) << '\n'
              << "[CONFIG] C++: " << compiler_flags_text(build_settings.cpp) << '\n'
              << "[CONFIG] EXE: " << build_settings.executable_name << '\n'
              << "[CONFIG] GCC ANALYSIS: " << analysis_name(build_settings.analysis.mode) << '\n'
              << "[CONFIG] BINARY REPORTS: " << (build_settings.binary.reports ? "ON" : "OFF")
              << " | MAP: " << (build_settings.binary.linker_map ? "ON" : "OFF")
              << " | BIN: " << (build_settings.binary.generate_bin ? "ON" : "OFF")
              << " | HEX: " << (build_settings.binary.generate_hex ? "ON" : "OFF") << '\n';

    std::vector<std::filesystem::path> object_files;
    bool has_cpp = false;
    bool any_rebuilt = false;
    std::size_t rebuilt_count = 0;
    std::size_t skipped_count = 0;

    for (const auto& item : project.sources()) {
      const auto language = convert_language(item.language);
      if (language == language_t::UNKNOWN) {
        std::cerr << "ERROR: UNKNOWN SOURCE LANGUAGE: " << item.path << std::endl;
        return 0x01;
      }
      has_cpp = has_cpp || language == language_t::CPP;

      if (language == language_t::ASM || language == language_t::ASM_CPP) {
        const bool with_preprocessor = language == language_t::ASM_CPP;
        const auto output = output_path(project, item.path, assembly_dir_to_build, ".o");
        const auto preprocessed = output_path(project, item.path, preprocessing_dir_to_build, ".s");
        const auto dependencies = output_path(project, item.path, dependencies_dir_to_build, ".d");
        const auto cache = output_path(project, item.path, cache_dir_to_build, ".cmd");
        const auto input_cache = output_path(project, item.path, cache_dir_to_build, ".inputs");
        const std::string signature = "PGR-ASM-CACHE-V1\n" +
          cache_t::tool_fingerprint(build_settings.gcc_command) + "\n" +
          (with_preprocessor ? "ASM_CPP\n" : "ASM\n") +
          assembly_command(with_preprocessor ? preprocessed : item.path, output) + "\n" +
          (with_preprocessor ? preprocessing_command(project, item.path, language_t::C,
             preprocessed, dependencies) : "") + "\n";
        const std::string inputs = with_preprocessor ? cache_t::dependency_fingerprints(dependencies)
          : cache_t::fingerprint(item.path) + "\n";
        const bool current = std::filesystem::is_regular_file(output) &&
          (!with_preprocessor || (std::filesystem::is_regular_file(preprocessed) &&
             std::filesystem::is_regular_file(dependencies)));
        if (current && cache_t::cache_matches(cache, signature) && cache_t::cache_matches(input_cache, inputs)) {
          std::cout << "[SKIP] UP-TO-DATE -> " << item.path.lexically_relative(project.root()) << '\n';
          ++skipped_count;
        } else {
          std::cout << "[BUILD] CHANGED -> " << item.path.lexically_relative(project.root()) << '\n';
          if (with_preprocessor) {
            if (const int result = preprocessing_assembler(project, item.path, preprocessed, dependencies))
              return result;
          }
          if (const int result = assembly(with_preprocessor ? preprocessed : item.path, output))
            return result;
          const std::string actual_inputs = with_preprocessor ? cache_t::dependency_fingerprints(dependencies)
            : cache_t::fingerprint(item.path) + "\n";
          if (actual_inputs.empty() || !cache_t::save_cache(cache, signature) ||
              !cache_t::save_cache(input_cache, actual_inputs)) return 1;
          ++rebuilt_count;
          any_rebuilt = true;
        }
        object_files.push_back(output);
        continue;
      }

      const auto preprocessed_file = output_path(project, item.path,
        preprocessing_dir_to_build, language == language_t::C ? ".i" : ".ii");
      const auto compiled_file = output_path(project, item.path, compilation_dir_to_build, ".s");
      const auto assembled_file = output_path(project, item.path, assembly_dir_to_build, ".o");
      const auto dependency_file = output_path(project, item.path, dependencies_dir_to_build, ".d");
      const auto cache_file = output_path(project, item.path, cache_dir_to_build, ".cmd");
      const auto input_cache = output_path(project, item.path, cache_dir_to_build, ".inputs");
      const auto report_dir = analysis_path(project, item.path);

      const std::string signature = "PGR-OBJECT-CACHE-V2\n" +
        cache_t::tool_fingerprint(driver_for(language)) + "\n" + cache_t::tool_fingerprint(build_settings.gcc_command) + "\n" +
        preprocessing_command(project, item.path, language, preprocessed_file, dependency_file) + "\n" +
        compilation_command(preprocessed_file, language, compiled_file, report_dir) + "\n" +
        assembly_command(compiled_file, assembled_file) + "\n";

      if (cache_t::cache_matches(cache_file, signature) &&
          cache_t::cache_matches(input_cache, cache_t::dependency_fingerprints(dependency_file)) &&
          cache_t::outputs_current(preprocessed_file, compiled_file, assembled_file, dependency_file) &&
          analysis_current(report_dir, build_settings.analysis)) {
        std::cout << "[SKIP] UP-TO-DATE -> " << item.path.lexically_relative(project.root()) << '\n';
        ++skipped_count;
      } else {
        std::cout << "[BUILD] CHANGED -> " << item.path.lexically_relative(project.root()) << '\n';
        int result = preprocessing(project, item.path, language, preprocessed_file, dependency_file);
        if (result != 0x00) return result;
        // Удаляем старые дампы, чтобы не смешивать проходы разных сборок.
        std::error_code remove_error;
        if (std::filesystem::is_symlink(report_dir, remove_error)) {
          std::cerr << "ERROR: REFUSING SYMLINK IN ANALYSIS: " << report_dir << '\n';
          return 1;
        }
        std::filesystem::remove_all(report_dir, remove_error);
        if (remove_error) {
          std::cerr << "ERROR: CANNOT CLEAN ANALYSIS: " << remove_error.message() << '\n';
          return 1;
        }
        result = compilation(preprocessed_file, language, compiled_file, report_dir);
        if (result != 0x00) return result;
        result = assembly(compiled_file, assembled_file);
        if (result != 0x00) return result;
        if (!std::filesystem::is_regular_file(dependency_file) ||
            !cache_t::save_cache(cache_file, signature) ||
            !cache_t::save_cache(input_cache, cache_t::dependency_fingerprints(dependency_file))) {
          std::cerr << "ERROR: CANNOT SAVE BUILD CACHE -> " << cache_file << '\n';
          return 0x01;
        }
        any_rebuilt = true;
        ++rebuilt_count;
      }
      object_files.push_back(assembled_file);
    }

    const auto executable_file = main_dir_to_build / project.name() /
      linking_dir_to_build / build_settings.executable_name;
    const auto link_cache = main_dir_to_build / project.name() / cache_dir_to_build / "linking.cmd";
    const auto link_inputs_cache = main_dir_to_build / project.name() / cache_dir_to_build / "linking.inputs";
    // Удалённая или изменённая карта компоновки требует повторной линковки.
    const auto map_file = executable_file.parent_path() / (executable_file.stem().string() + ".map");
    const std::string link_signature = "PGR-LINK-CACHE-V2\n" +
      linking_command(object_files, executable_file, has_cpp) + "\n";
    // Отслеживаем содержимое самих .o, включая случай ручного изменения
    // объектного файла с восстановлением его временной метки.
    const auto link_inputs = [&]() {
      std::string value = link_input_fingerprints();
      for (const auto& object : object_files) value += cache_t::fingerprint(object) + "\n";
      return value;
    };

    std::error_code error;
    bool link_needed = any_rebuilt || !cache_t::cache_matches(link_cache, link_signature) ||
      !cache_t::cache_matches(link_inputs_cache, link_inputs()) ||
      !std::filesystem::is_regular_file(executable_file, error) || error;
    if (!link_needed && build_settings.binary.linker_map &&
        !std::filesystem::is_regular_file(map_file)) link_needed = true;
    if (!link_needed && !build_settings.linker_script.empty() &&
        !std::filesystem::is_regular_file(build_settings.linker_script)) {
      std::cerr << "ERROR: LINKER SCRIPT NOT FOUND: " << build_settings.linker_script << '\n';
      return 1;
    }
    if (!build_settings.linker_script.empty() &&
        !std::filesystem::is_regular_file(build_settings.linker_script)) {
      std::cerr << "ERROR: LINKER SCRIPT NOT FOUND: " << build_settings.linker_script << '\n';
      return 1;
    }
    if (!link_needed) {
      const auto executable_time = std::filesystem::last_write_time(executable_file, error);
      if (error) link_needed = true;
      else {
        if (!build_settings.linker_script.empty() &&
            (std::filesystem::last_write_time(build_settings.linker_script, error) > executable_time || error)) {
          link_needed = true;
        }
        for (const auto& object : object_files) {
          if (std::filesystem::last_write_time(object, error) > executable_time || error) {
            link_needed = true;
            break;
          }
        }
      }
    }

    if (link_needed) {
      // Не выдаём карту прошлой сборки за новую; старую карту убираем до вызова ld.
      std::error_code map_error;
      if (std::filesystem::is_symlink(map_file, map_error)) {
        std::cerr << "ERROR: REFUSING SYMLINK IN LINKER MAP: " << map_file << '\n';
        return 1;
      }
      map_error.clear();
      std::filesystem::remove(map_file, map_error);
      if (map_error) {
        std::cerr << "ERROR: CANNOT CLEAN LINKER MAP: " << map_error.message() << '\n';
        return 1;
      }
      const int result = linking(object_files, executable_file, has_cpp);
      if (result != 0x00) return result;
      if (build_settings.binary.linker_map && !std::filesystem::is_regular_file(map_file)) {
        std::cerr << "ERROR: LINKER DID NOT CREATE MAP: " << map_file << '\n';
        return 1;
      }
      if (!cache_t::save_cache(link_cache, link_signature) ||
          !cache_t::save_cache(link_inputs_cache, link_inputs())) {
        std::cerr << "ERROR: CANNOT SAVE LINK CACHE -> " << link_cache << '\n';
        return 0x01;
      }
    } else {
      std::cout << "[LINK] UP-TO-DATE -> " << executable_file << '\n';
    }

    // Перекомпиляция не требуется при изменении только параметров анализа Binutils.
    binary_t binary;
    const auto binary_dir = main_dir_to_build / project.name() / binary_dir_to_build;
    const auto binary_cache = main_dir_to_build / project.name() / cache_dir_to_build / "binary.cmd";
    const int binary_result = binary.run(executable_file, binary_dir, binary_cache,
      build_settings.binary, command_executor);
    if (binary_result != 0) return binary_result;

    std::cout << "[SUMMARY] Rebuilt: " << rebuilt_count
              << " | Skipped: " << skipped_count
              << " | Linking: " << (link_needed ? "YES" : "NO") << '\n';
    std::cout << "GCC BUILD SUCCESS -> " << executable_file << std::endl;
    return 0x00;
  }

  // Совместимость с исходным main.cpp из архива.
  int run(int argc, char* argv[]) {
    const std::filesystem::path root = argc > 1 ? argv[1] : "test";
    project_t project(root);
    if (project.scan() != 0x00) {
      std::cerr << "ERROR: PROJECT DIRECTORY NOT FOUND: " << root << std::endl;
      return 0x01;
    }
    return run(project);
  }

  ~gcc_t() {
    std::cout << "GCC destructor.\n";
  }
};

#endif // GCC_HPP
