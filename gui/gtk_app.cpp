/**
 * @file    gtk_app.cpp
 * @author  Gusev Danil (gusevdanil.work@gmail.com)
 * @brief   Класс графического пользовательского интерфейса приложения
 */
#include <iostream>

#include "gtk_app.hpp"

/**
 * @group
 * PRIVATE_API {
 */
    /**
     * @brief   Callback-функция активации приложения.
     * @details Вызывается GTK при активации GtkApplication. Приводит
     *          пользовательские данные к указателю на gtk_app_t и создаёт
     *          главное окно приложения.
     * @param   app Указатель на GtkApplication (не используется).
     * @param   data Указатель на экземпляр gtk_app_t.
     */
    void gtk_app_t::activate(
        GtkApplication* /*app*/,
        gpointer data
    ) {
        auto* self = static_cast<gtk_app_t*>(data);
        
        self->create_window();
    }

    /**
     * @brief   Callback-функция закрытия главного окна.
     * @details Вызывается при получении сигнала "destroy" от главного окна.
     *          Сбрасывает указатель на окно, чтобы при следующей активации
     *          приложения оно было создано заново.
     * @param   widget Указатель на виджет (главное окно, не используется).
     * @param   data Указатель на экземпляр gtk_app_t.
     */
    void gtk_app_t::deactivate(
        GtkWidget* /*widget*/,
        gpointer data
    ) {
        auto* self = static_cast<gtk_app_t*>(data);
        
        self->window = nullptr;
    }

    /**
     * @brief   Создаёт главное окно приложения.
     * @details Если окно уже существует — выводит его на передний план.
     *          В противном случае создаёт новое окно GtkApplicationWindow,
     *          задаёт заголовок и размеры по умолчанию, подключает обработчик
     *          сигнала "destroy" и отображает окно.
     */
    void gtk_app_t::create_window() {
        if (window != nullptr) {
            gtk_window_present(
                GTK_WINDOW(window)
            );

            return;
        }

        window = gtk_application_window_new(application);

        gtk_window_set_title(
            GTK_WINDOW(window),
            "PaperGooseRus Make"
        );

        gtk_window_set_default_size(
            GTK_WINDOW(window),
            1920,
            1080
        );

        g_signal_connect(
            window,
            "destroy",
            G_CALLBACK(gtk_app_t::deactivate),
            this
        );

        gtk_window_present(GTK_WINDOW(window));
    }
/**
 * } PRIVATE_API
 */




/**
 * @group
 * PUBLIC_API {
 */
    /**
     * @brief   Конструктор класса gtk_app_t.
     * @details Создаёт объект GtkApplication с заданным идентификатором
     *          и подключает callback-функцию activate к сигналу "activate".
     */
    gtk_app_t::gtk_app_t() {
        std::cout << "GTK4 App constructor." << std::endl;

        application = gtk_application_new(
            "com.example.application",
            G_APPLICATION_DEFAULT_FLAGS
        );

        g_signal_connect(
            application,
            "activate",
            G_CALLBACK(gtk_app_t::activate),
            this
        );
    }

    /**
     * @brief   Запускает главный цикл приложения.
     * @details Передаёт управление GTK-приложению через g_application_run.
     * @param   argc Количество аргументов командной строки.
     * @param   argv Массив аргументов командной строки.
     * @return  Код возврата приложения.
     */
    int gtk_app_t::run(
        int argc,
        char* argv[]
    ) {
        return g_application_run(
            G_APPLICATION(application),
            argc,
            argv
        );
    }

    /**
     * @brief   Деструктор класса gtk_app_t.
     * @details Освобождает ресурсы GtkApplication через g_object_unref.
     */
    gtk_app_t::~gtk_app_t() {
        std::cout << "GTK4 App destructor." << std::endl;

        g_object_unref(application);
    }
/**
 * } PUBLIC_API
 */