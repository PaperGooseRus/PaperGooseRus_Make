#include <exception>
#include <iostream>

#include "gui/gtk_app.hpp"

/**
 * @brief   Главный класс приложения, управляющий жизненным циклом GUI.
 * @details Создаёт и запускает графический интерфейс, а также обрабатывает
 *          исключения, возникающие в процессе работы.
 */
class pgr_app_t final {
  public:
    /**
     * @brief Конструктор класса pgr_app_t.
     */
    pgr_app_t() {
        std::cout << "PGR App constructor." << std::endl;
    }

    /**
     * @brief Деструктор класса pgr_app_t.
     */
    ~pgr_app_t() {
        std::cout << "PGR App destructor." << std::endl;
    }

    /**
     * @brief   Запускает приложение.
     * @details Создаёт объект gtk_app_t и передаёт ему управление.
     * @param   argc Количество аргументов командной строки.
     * @param   argv Массив аргументов командной строки.
     * @return  Код возврата приложения.
     */
    int run(int argc, char* argv[]) {
        gtk_app_t gtk_app;
        return gtk_app.run(argc, argv);
    }
};

/**
 * @brief   Точка входа в приложение.
 * @details Создаёт объект pgr_app_t и запускает его. В случае возникновения
 *          исключения выводит сообщение об ошибке в stderr и возвращает 1.ы
 * @param   argc Количество аргументов командной строки.
 * @param   argv Массив аргументов командной строки.
 * @return  Код возврата приложения.
 */
int main(int argc, char* argv[]) {
    try {
        pgr_app_t pgr_app;
        return pgr_app.run(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << "PGR App error: " << err.what() << std::endl;
        return 1;
    }
}