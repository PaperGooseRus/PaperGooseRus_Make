#ifndef GTK_APP_HPP
#define GTK_APP_HPP

#include <gtk/gtk.h>

// Управляет GtkApplication и его главным окном.
class gtk_app_t final {
  private:
    GtkApplication* application = nullptr;
    GtkWidget* window = nullptr; // Не владеющий указатель: жизненным циклом управляет GTK.

    static void activate(GtkApplication* app, gpointer data);
    static void on_window_destroyed(GtkWidget* widget, gpointer data);

    void create_window();

  public:
    gtk_app_t();
    ~gtk_app_t();

    // Копирование и перемещение запрещены: объект владеет GTK-ресурсом.
    gtk_app_t(const gtk_app_t&) = delete;
    gtk_app_t& operator=(const gtk_app_t&) = delete;
    gtk_app_t(gtk_app_t&&) = delete;
    gtk_app_t& operator=(gtk_app_t&&) = delete;

    int run(int argc, char* argv[]);
};

#endif // GTK_APP_HPP
