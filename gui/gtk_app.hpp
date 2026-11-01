/**
 * @file    gtk_app.hpp
 * @author  Gusev Danil (gusevdanil.work@gmail.com)
 * @brief   Класс графического пользовательского интерфейса приложения.
 */
#ifndef GTK_APP_HPP
#define GTK_APP_HPP

#include <gtk/gtk.h>

/**
 * @brief Класс графического пользовательского интерфейса приложения.
 *        Управляет созданием главного окна и жизненным циклом GTK-приложения.
 */
class gtk_app_t final {
  private:
    /**
     * @brief Указатель на объект GtkApplication.
     */
    GtkApplication* application = nullptr;
    
    /**
     * @brief Указатель на главное окно приложения.
     */
    GtkWidget* window = nullptr;

    /**
     * @brief   Callback-функция активации приложения.
     * @details Вызывается при активации GtkApplication. Создаёт главное окно.
     * @param   app - Указатель на GtkApplication.
     * @param   data - Указатель на экземпляр gtk_app_t.
     */
    static void activate(
        GtkApplication* app,
        gpointer data
    );

    /**
     * @brief   Callback-функция закрытия главного окна.
     * @details Вызывается при закрытии окна. Завершает приложение.
     * @param   widget - Указатель на виджет (главное окно).
     * @param   data - Указатель на экземпляр gtk_app_t.
     */
    static void deactivate(
        GtkWidget* widget,
        gpointer data
    );

    /**
     * @brief   Создаёт главное окно приложения.
     * @details Инициализирует и настраивает GtkWidget.
     */
    void create_window();

  public:
    /**
     * @brief   Конструктор класса gtk_app_t.
     * @details Создаёт объект GtkApplication.
     */
    gtk_app_t();

    /**
     * @brief   Запускает приложение.
     * @param   argc Количество аргументов командной строки.
     * @param   argv Массив аргументов командной строки.
     * @return  Код возврата приложения.
     */
    int run(
        int argc,
        char* argv[]
    );

    /**
     * @brief   Деструктор класса gtk_app_t.
     * @details Освобождает ресурсы, связанные с приложением и окном.
     */
    ~gtk_app_t();
};

#endif // GTK_APP_HPP