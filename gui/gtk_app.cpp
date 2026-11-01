#include "gtk_app.hpp"

#include <iostream>

gtk_app_t::gtk_app_t() {
    std::cout << "GTK4 App constructor." << std::endl;

    application = gtk_application_new(
        "com.example.application",
        G_APPLICATION_DEFAULT_FLAGS
    );

    // GTK принимает C-функцию; указатель this доступен через data.
    g_signal_connect(
        application,
        "activate",
        G_CALLBACK(gtk_app_t::activate),
        this
    );
}

gtk_app_t::~gtk_app_t() {
    std::cout << "GTK4 App destructor." << std::endl;
    g_object_unref(application);
}

int gtk_app_t::run(int argc, char* argv[]) {
    return g_application_run(G_APPLICATION(application), argc, argv);
}

void gtk_app_t::activate(GtkApplication* /*app*/, gpointer data) {
    auto* self = static_cast<gtk_app_t*>(data);
    self->create_window();
}

void gtk_app_t::create_window() {
    // GtkApplication может активироваться повторно: используем готовое окно.
    if (window != nullptr) {
        gtk_window_present(GTK_WINDOW(window));
        return;
    }

    window = gtk_application_window_new(application);

    gtk_window_set_title(GTK_WINDOW(window), "PaperGooseRus Make");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    // Когда GTK уничтожает окно, перестаём хранить указатель на него.
    g_signal_connect(
        window,
        "destroy",
        G_CALLBACK(gtk_app_t::on_window_destroyed),
        this
    );

    gtk_window_present(GTK_WINDOW(window));
}

void gtk_app_t::on_window_destroyed(GtkWidget* /*widget*/, gpointer data) {
    auto* self = static_cast<gtk_app_t*>(data);
    self->window = nullptr;
}
