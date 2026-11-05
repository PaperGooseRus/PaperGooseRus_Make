#ifndef LOGER_HPP
#define LOGER_HPP

#include "../process/process.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

class loger_t {
private:
    // Дублирование потока одновременно в консоль и файл журнала.
    class tee_buffer_t : public std::streambuf {
    private:
        std::streambuf* console_buffer;
        std::streambuf* file_buffer;

    protected:
        int_type overflow(int_type character) override {
            if (traits_type::eq_int_type(character, traits_type::eof())) {
                return traits_type::not_eof(character);
            }

            const auto value = traits_type::to_char_type(character);
            const auto console_result = console_buffer->sputc(value);
            const auto file_result = file_buffer->sputc(value);

            if (traits_type::eq_int_type(console_result, traits_type::eof()) ||
                traits_type::eq_int_type(file_result, traits_type::eof())) {
                return traits_type::eof();
            }

            return character;
        }

        int sync() override {
            const int console_result = console_buffer->pubsync();
            const int file_result = file_buffer->pubsync();
            return (console_result == 0 && file_result == 0) ? 0 : -1;
        }

    public:
        tee_buffer_t(std::streambuf* console, std::streambuf* file)
            : console_buffer(console), file_buffer(file) {}
    };

    std::ofstream log_file;
    std::streambuf* original_cout = nullptr;
    std::streambuf* original_cerr = nullptr;
    tee_buffer_t cout_buffer;
    tee_buffer_t cerr_buffer;
    std::filesystem::path log_path;

public:
    // Создание журнала: C:/PGR/build/<project>/<project>.log.
    explicit loger_t(
        const std::string& project,
        const std::filesystem::path& build_root = "C:/PGR/build"
    ) :
        cout_buffer(std::cout.rdbuf(), log_file.rdbuf()),
        cerr_buffer(std::cerr.rdbuf(), log_file.rdbuf())
    {
        // Имя проекта должно быть одним компонентом пути.
        const std::filesystem::path project_name(project);
        if (project.empty() || project_name != project_name.filename() ||
            project == "." || project == ".." ||
            project_name.is_absolute() || project_name.has_root_path()) {
            throw std::invalid_argument("ERROR: INVALID PROJECT NAME");
        }

        const auto project_dir = build_root / project_name;
        std::filesystem::create_directories(project_dir);
        log_path = project_dir / (project + ".log");

        // Открытие журнала в режиме добавления.
        log_file.open(log_path, std::ios::out | std::ios::app);
        if (!log_file.is_open()) {
            throw std::runtime_error("ERROR: LOG FILE OPEN FAILED: " + log_path.string());
        }

        // Сохранение исходных потоков и установка дублирования.
        std::cout.flush();
        std::cerr.flush();
        original_cout = std::cout.rdbuf();
        original_cerr = std::cerr.rdbuf();
        std::cout.rdbuf(&cout_buffer);
        std::cerr.rdbuf(&cerr_buffer);
    }

    // Выполнение GCC и GNU Binutils напрямую, без командной оболочки.
    // stdout/stderr проходят через std::cout и попадают в консоль и журнал.
    // stdin остаётся у дочернего процесса; ввод паролей не протоколируется.
    int execute(const std::string& command) {
        try {
            return process_t::run(command);
        } catch (const std::exception& error) {
            std::cerr << "ERROR: PROCESS START FAILED: " << error.what() << std::endl;
            return 1;
        }
    }

    const std::filesystem::path& path() const noexcept {
        return log_path;
    }

    ~loger_t() {
        // Восстановление потоков до закрытия файла.
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(original_cout);
        std::cerr.rdbuf(original_cerr);
        log_file.flush();
        log_file.close();
    }

    loger_t(const loger_t&) = delete;
    loger_t& operator=(const loger_t&) = delete;
    loger_t(loger_t&&) = delete;
    loger_t& operator=(loger_t&&) = delete;
};

#endif // LOGER_HPP
