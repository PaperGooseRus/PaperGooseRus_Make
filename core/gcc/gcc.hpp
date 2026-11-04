#ifndef GCC_HPP
#define GCC_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <filesystem>

class gcc_t {
private:
  enum class gcc_step_t {
    INACTIVE =      0x00,             //   Инструмент                   // Команда                  // Что происходит
    PREPROCESSING = 0x01,             //!< cpp (или cc1 -E)             // cpp main.c -o main.i     // Раскрытие макросов, #include, условная компиляция
    COMPILATION =   0x02,             //!< cc1 (для C) / cc1plus (C++)  // cc1 main.i -o main.s     // Трансляция C-кода в ассемблер. Это самый сложный этап, внутри которого скрыто множество подэтапов.
    ASSEMBLY =      0x03,             //!< as                           // as main.s -o main.o      // Перевод ассемблерного текста в машинный код (объектный файл).
    LINKING =       0x04              //!< collect2 / ld                // ld main.o -o app         // Объединение объектных файлов и библиотек в исполняемый файл или разделяемую библиотеку.
  };
  gcc_step_t gcc_step = gcc_step_t::INACTIVE;

  enum class gcc_compilation_step_t {
    INACTIVE                 = 0x00,  //   Компонент GCC                // Представление / механизм // Что происходит
    LEXICAL_ANALYSIS         = 0x01,  //!< Lexer                        // Tokens                   // Разбиение исходного текста на токены: ключевые слова, идентификаторы, операторы и литералы.
    SYNTAX_ANALYSIS          = 0x02,  //!< Parser                       // Syntax Tree              // Проверка грамматики языка и построение синтаксической структуры программы.
    SEMANTIC_ANALYSIS        = 0x03,  //!< Semantic Analyzer            // TREE                     // Проверка типов, объявлений, областей видимости и допустимости операций.
    GENERIC_GENERATION       = 0x04,  //!< Front End                    // GENERIC                  // Формирование высокоуровневого промежуточного представления программы.
    GIMPLE_GENERATION        = 0x05,  //!< Gimplifier                   // GENERIC -> GIMPLE        // Упрощение сложных выражений и преобразование программы в представление GIMPLE.
    SSA_CONSTRUCTION         = 0x06,  //!< SSA Passes                   // GIMPLE -> GIMPLE SSA     // Построение SSA-формы с отдельными версиями переменных для каждого определения.
    GIMPLE_OPTIMIZATION      = 0x07,  //!< Tree Optimization Passes     // GIMPLE SSA               // Оптимизация выражений, циклов и потоков управления, удаление избыточных вычислений.
    INTERPROCEDURAL_ANALYSIS = 0x08,  //!< IPA Passes                   // GIMPLE / Call Graph      // Анализ взаимодействия функций, межпроцедурные оптимизации и встраивание функций.
    RTL_GENERATION           = 0x09,  //!< RTL Expander                 // GIMPLE -> RTL            // Преобразование GIMPLE в низкоуровневое представление операций над регистрами и памятью.
    RTL_OPTIMIZATION         = 0x0A,  //!< RTL Optimization Passes      // RTL                      // Машинно-зависимые оптимизации инструкций, переходов и операций с регистрами.
    REGISTER_ALLOCATION      = 0x0B,  //!< Register Allocator           // RTL -> Hard Registers    // Распределение физических регистров процессора и размещение временных значений в памяти.
    INSTRUCTION_SCHEDULING   = 0x0C,  //!< Instruction Scheduler        // RTL                      // Упорядочивание машинных инструкций с учётом зависимостей и особенностей процессора.
    ASSEMBLY_GENERATION      = 0x0D   //!< Final Pass                   // RTL -> Assembly (.s)     // Генерация ассемблерного текста для выбранной архитектуры процессора.
  };  
  gcc_compilation_step_t gcc_compilation_step = gcc_compilation_step_t::INACTIVE;
  
  const std::filesystem::path main_dir_to_build = "C:/PGR/build";           //!< Общая директория для всех проектов
  const std::filesystem::path preprocessing_dir_to_build = "preprocessing"; //!< Диренктория этапа PREPROCESSING по пути: main_dir_to_build/$project$/preprocessing/
  const std::filesystem::path compilation_dir_to_build = "compilation";     //!< Диренктория этапа COMPILATION по пути: main_dir_to_build/$project$/compilation/
  const std::filesystem::path assembly_dir_to_build = "assembly";           //!< Диренктория этапа ASSEMBLY по пути: main_dir_to_build/$project$/assembly/
  const std::filesystem::path linking_dir_to_build = "linking";             //!< Диренктория этапа LINKING по пути: main_dir_to_build/$project$/linking/

  int preprocessing(
    const std::filesystem::path& source_file_path
  ) {
    std::cout << "GCC START PREPROCESSING ->" << std::endl;
    gcc_step = gcc_step_t::PREPROCESSING;

    // Проверка существования исходного файла.
    if (!std::filesystem::is_regular_file(source_file_path)) {
      std::cerr << "ERROR: SOURCE FILE NOT FOUND: " << source_file_path << std::endl;

      return 0x01;
    }

    // Формирование директории этапа PREPROCESSING.
    const auto parent = main_dir_to_build / project / preprocessing_dir_to_build;
    std::filesystem::create_directories(parent);

    // Получение абсолютного пути исходного файла.
    const auto source = std::filesystem::absolute(source_file_path);

    // Формирование имени выходного файла (Формирование полного пути выходного файла).
    auto filename = source_file_path.filename();
    filename.replace_extension(".ii");
    const auto target = parent / filename;

    // Формирование команды препроцессирования.
    std::string cmd = "g++ -E";

    cmd += " \"";
    cmd += source.string();
    cmd += "\" -o \"";
    cmd += target.string();
    cmd += "\"";

    // Вывод информации о выполняемой операции.
    std::cout << "[SOURCE] -> " << source << std::endl;
    std::cout << "[TARGET] -> " << target << std::endl;
    std::cout << "[CMD]    -> " << cmd << std::endl;

    // Выполнение команды препроцессирования.
    const int error = std::system(cmd.c_str());

    // Проверка результата выполнения команды.
    if (error != 0x00) {
      std::cerr << "ERROR: PREPROCESSING -> " << error << std::endl;
    }

    return error;
  }

  int compilation(
    const std::filesystem::path& source_file_path
  ) {
    std::cout << "GCC START COMPILATION ->" << std::endl;
    gcc_step = gcc_step_t::COMPILATION;

    // Проверка существования исходного файла.
    if (!std::filesystem::is_regular_file(source_file_path)) {
      std::cerr << "ERROR: SOURCE FILE NOT FOUND: " << source_file_path << std::endl;

        return 0x01;
    }

    // Формирование директории этапа COMPILATION.
    const auto parent = main_dir_to_build / project / compilation_dir_to_build;
    std::filesystem::create_directories(parent);

    // Получение абсолютного пути исходного файла.
    const auto source = std::filesystem::absolute(source_file_path);

    // Формирование имени и полного пути выходного файла.
    auto filename = source_file_path.filename();
    filename.replace_extension(".s");

    const auto target = parent / filename;

    // Формирование команды компиляции.
    std::string cmd = "g++ -S -x c++-cpp-output";

    cmd += " \"";
    cmd += source.string();
    cmd += "\" -o \"";
    cmd += target.string();
    cmd += "\"";

    // Вывод информации о выполняемой операции.
    std::cout << "[SOURCE] -> " << source << std::endl;
    std::cout << "[TARGET] -> " << target << std::endl;
    std::cout << "[CMD]    -> " << cmd << std::endl;

    // Выполнение команды компиляции.
    const int error = std::system(cmd.c_str());

    // Проверка результата выполнения команды.
    if (error != 0x00) {
      std::cerr << "ERROR: COMPILATION -> " << error << std::endl;
    }

    return error;
  }

  int assembly(void) {
    std::cout << "GCC start step=\"ASSEMBLY\" ->" << std::endl;

    gcc_step = gcc_step_t::ASSEMBLY;

    // pass

    return 0x00;
  }

  int linking(void) {
    std::cout << "GCC start step=\"LINKING\" ->" << std::endl;

    gcc_step = gcc_step_t::LINKING;

    // pass

    return 0x00;
  }

public:
  std::string project = "test";

  gcc_t(void) {
  	std::cout << "GCC constructor.\n"; 
  }


int run(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    int result = 0x00;

    // Исходный файл проекта.
    const std::filesystem::path source =
        "C:/Project/PaperGooseRus_Make/test/main.cpp";

    // Этап PREPROCESSING.
    result = preprocessing(source);

    if (result != 0x00) {
        return result;
    }

    // Путь к результату PREPROCESSING.
    const auto preprocessed_file =
        main_dir_to_build /
        project /
        preprocessing_dir_to_build /
        "main.ii";

    // Этап COMPILATION.
    result = compilation(preprocessed_file);

    if (result != 0x00) {
        return result;
    }

    return 0x00;
}


  ~gcc_t() {
    std::cout << "GCC destructor.\n";
  }
};

#endif // GCC_HPP