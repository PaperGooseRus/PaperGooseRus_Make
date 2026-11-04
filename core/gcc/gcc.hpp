#ifndef GCC_HPP
#define GCC_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "../project/project.hpp"

class gcc_t {
public:
  enum class language_t {
    UNKNOWN = 0x00,
    C =       0x01,
    CPP =     0x02
  };

  enum class standart_t {
    C11 =   0x01,
    C17 =   0x02,
    C23 =   0x03,
    CPP11 = 0x11,
    CPP17 = 0x12,
    CPP23 = 0x13
  };

  enum class optimization_t {
    O0 = 0x00,
    O1 = 0x01,
    O2 = 0x02,
    O3 = 0x03,
    OS = 0x04,
    OG = 0x05
  };

  enum class gcc_step_t {
    INACTIVE =      0x00,             //   Инструмент                   // Команда                  // Что происходит
    PREPROCESSING = 0x01,             //!< cpp (или cc1 -E)             // cpp main.c -o main.i     // Раскрытие макросов, #include, условная компиляция
    COMPILATION =   0x02,             //!< cc1 (для C) / cc1plus (C++)  // cc1 main.i -o main.s     // Трансляция C-кода в ассемблер.
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
  };

private:
  gcc_step_t gcc_step = gcc_step_t::INACTIVE;
  gcc_compilation_step_t gcc_compilation_step = gcc_compilation_step_t::INACTIVE;

  std::filesystem::path main_dir_to_build = "C:/PGR/build";              //!< Общая директория для всех проектов.
  const std::filesystem::path preprocessing_dir_to_build = "preprocessing"; //!< Директория этапа PREPROCESSING.
  const std::filesystem::path compilation_dir_to_build = "compilation";     //!< Директория этапа COMPILATION.
  const std::filesystem::path assembly_dir_to_build = "assembly";           //!< Директория этапа ASSEMBLY.
  const std::filesystem::path linking_dir_to_build = "linking";             //!< Директория этапа LINKING.

  gcc_element_t gcc_element_c;
  gcc_element_t gcc_element_cpp;
  std::vector<std::string> linking_flags;
  std::string executable_name = "app.exe";
  std::string gcc_command = "gcc";
  std::string gpp_command = "g++";

  // Формирование параметра стандарта языка.
  static std::string standart_key(standart_t standart) {
    switch (standart) {
      case standart_t::C11:   return "-std=c11";
      case standart_t::C17:   return "-std=c17";
      case standart_t::C23:   return "-std=c23";
      case standart_t::CPP11: return "-std=c++11";
      case standart_t::CPP17: return "-std=c++17";
      case standart_t::CPP23: return "-std=c++23";
    }
    throw std::invalid_argument("UNKNOWN GCC STANDARD");
  }

  // Формирование параметра оптимизации.
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

  // Проверка совместимости стандарта и языка.
  static bool is_valid_standart(language_t language, standart_t standart) {
    const bool is_c = standart == standart_t::C11 || standart == standart_t::C17 || standart == standart_t::C23;
    const bool is_cpp = standart == standart_t::CPP11 || standart == standart_t::CPP17 || standart == standart_t::CPP23;
    return (language == language_t::C && is_c) || (language == language_t::CPP && is_cpp);
  }

  // Преобразование языка, определённого анализатором проекта.
  static language_t convert_language(project_source_t::language_t language) {
    switch (language) {
      case project_source_t::language_t::C: return language_t::C;
      case project_source_t::language_t::CPP: return language_t::CPP;
      default: return language_t::UNKNOWN;
    }
  }

  // Экранирование путей для командной строки Windows / POSIX.
  // Параметры пользовательских флагов передаются отдельно и считаются доверенными.
  static std::string quote(const std::filesystem::path& path) {
    const auto value = path.string();
    if (value.find('"') != std::string::npos || value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos) {
      throw std::invalid_argument("UNSUPPORTED CHARACTERS IN PATH");
    }
    return "\"" + value + "\"";
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

  // Создание каталога для выходного файла.
  static void prepare_target(const std::filesystem::path& target) {
    std::filesystem::create_directories(target.parent_path());
  }

  // Проверка существования входного файла.
  static bool check_source(const std::filesystem::path& source) {
    if (!std::filesystem::is_regular_file(source)) {
      std::cerr << "ERROR: SOURCE FILE NOT FOUND: " << source << std::endl;
      return false;
    }
    return true;
  }

  // Централизованное выполнение команды GCC.
  static int execute(
    const std::string& cmd,
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const char* stage
  ) {
    std::cout << "[SOURCE] -> " << source << std::endl;
    std::cout << "[TARGET] -> " << target << std::endl;
    std::cout << "[CMD]    -> " << cmd << std::endl;

    const int error = std::system(cmd.c_str());
    if (error != 0x00) {
      std::cerr << "ERROR: " << stage << " -> " << error << std::endl;
    }
    return error;
  }

  // Получение настроек выбранного языка.
  const gcc_element_t& settings_for(language_t language) const {
    if (language == language_t::C) return gcc_element_c;
    if (language == language_t::CPP) return gcc_element_cpp;
    throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  // Получение драйвера выбранного языка.
  const std::string& driver_for(language_t language) const {
    if (language == language_t::C) return gcc_command;
    if (language == language_t::CPP) return gpp_command;
    throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  // Этап PREPROCESSING.
  int preprocessing(
    const project_t& project,
    const std::filesystem::path& source_file_path,
    language_t language,
    const std::filesystem::path& target
  ) {
    std::cout << "GCC START PREPROCESSING ->" << std::endl;
    gcc_step = gcc_step_t::PREPROCESSING;

    if (!check_source(source_file_path)) return 0x01;
    prepare_target(target);

    const auto& settings = settings_for(language);
    std::string cmd = driver_for(language) + " -E " + standart_key(settings.standart);
    cmd += " " + optimization_key(settings.optimization);

    for (const auto& dir : project.include_dirs()) {
      cmd += " -I" + quote(dir);
    }
    for (const auto& flag : settings.flags) {
      cmd += " " + flag;
    }

    cmd += " " + quote(source_file_path) + " -o " + quote(target);
    return execute(cmd, source_file_path, target, "PREPROCESSING");
  }

  // Этап COMPILATION.
  int compilation(
    const std::filesystem::path& source_file_path,
    language_t language,
    const std::filesystem::path& target
  ) {
    std::cout << "GCC START COMPILATION ->" << std::endl;
    gcc_step = gcc_step_t::COMPILATION;

    if (!check_source(source_file_path)) return 0x01;
    prepare_target(target);

    const auto& settings = settings_for(language);
    std::string cmd = driver_for(language) + " -S -x ";
    cmd += language == language_t::C ? "cpp-output" : "c++-cpp-output";
    cmd += " " + standart_key(settings.standart);
    cmd += " " + optimization_key(settings.optimization);

    for (const auto& flag : settings.flags) {
      cmd += " " + flag;
    }

    cmd += " " + quote(source_file_path) + " -o " + quote(target);
    return execute(cmd, source_file_path, target, "COMPILATION");
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

    const std::string cmd = gcc_command + " -c -x assembler " +
      quote(source_file_path) + " -o " + quote(target);
    return execute(cmd, source_file_path, target, "ASSEMBLY");
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

    std::string cmd = has_cpp ? gpp_command : gcc_command;
    for (const auto& source : source_files) {
      cmd += " " + quote(source);
    }
    for (const auto& flag : linking_flags) {
      cmd += " " + flag;
    }
    cmd += " -o " + quote(target);

    return execute(cmd, source_files.front(), target, "LINKING");
  }

public:
  gcc_t(void) {
    gcc_element_c = {language_t::C, standart_t::C17, optimization_t::O0, {}};
    gcc_element_cpp = {language_t::CPP, standart_t::CPP17, optimization_t::O0, {}};
    std::cout << "GCC constructor.\n";
  }

  // Настройка общей директории сборки.
  void set_build_dir(const std::filesystem::path& path) {
    main_dir_to_build = std::filesystem::absolute(path).lexically_normal();
  }

  // Настройка компилятора для конкретного языка.
  void set_language_settings(const gcc_element_t& settings) {
    if (!is_valid_standart(settings.language, settings.standart)) {
      throw std::invalid_argument("GCC STANDARD DOES NOT MATCH LANGUAGE");
    }
    if (settings.language == language_t::C) gcc_element_c = settings;
    else if (settings.language == language_t::CPP) gcc_element_cpp = settings;
    else throw std::invalid_argument("UNKNOWN SOURCE LANGUAGE");
  }

  void set_linking_flags(const std::vector<std::string>& flags) { linking_flags = flags; }
  void set_executable_name(const std::string& name) { executable_name = name; }
  void set_compilers(const std::string& gcc, const std::string& gpp) {
    gcc_command = gcc;
    gpp_command = gpp;
  }

  gcc_step_t current_step(void) const { return gcc_step; }
  gcc_compilation_step_t current_compilation_step(void) const { return gcc_compilation_step; }

  // Сборка проекта, предварительно обработанного project_t.
  int run(const project_t& project) {
    gcc_step = gcc_step_t::INACTIVE;
    gcc_compilation_step = gcc_compilation_step_t::INACTIVE;

    if (project.sources().empty()) {
      std::cerr << "ERROR: PROJECT HAS NO SOURCE FILES" << std::endl;
      return 0x01;
    }

    std::vector<std::filesystem::path> object_files;
    bool has_cpp = false;

    for (const auto& item : project.sources()) {
      const auto language = convert_language(item.language);
      if (language == language_t::UNKNOWN) {
        std::cerr << "ERROR: UNKNOWN SOURCE LANGUAGE: " << item.path << std::endl;
        return 0x01;
      }
      has_cpp = has_cpp || language == language_t::CPP;

      const auto preprocessed_file = output_path(project, item.path,
        preprocessing_dir_to_build, language == language_t::C ? ".i" : ".ii");
      const auto compiled_file = output_path(project, item.path, compilation_dir_to_build, ".s");
      const auto assembled_file = output_path(project, item.path, assembly_dir_to_build, ".o");

      int result = preprocessing(project, item.path, language, preprocessed_file);
      if (result != 0x00) return result;

      result = compilation(preprocessed_file, language, compiled_file);
      if (result != 0x00) return result;

      result = assembly(compiled_file, assembled_file);
      if (result != 0x00) return result;

      object_files.push_back(assembled_file);
    }

    const auto executable_file = main_dir_to_build / project.name() /
      linking_dir_to_build / executable_name;
    const int result = linking(object_files, executable_file, has_cpp);
    if (result != 0x00) return result;

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
