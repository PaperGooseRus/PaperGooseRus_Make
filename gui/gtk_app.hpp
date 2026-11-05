/**
 * @file    gtk_app.hpp
 * @author  Gusev Danil (gusevdanil.work@gmail.com)
 * @brief   Графический интерфейс PaperGooseRus Make на GTK4.
 * @details Header-only интерфейс к существующим project_t, gcc_t, config_t и loger_t.
 */
#ifndef GTK_APP_HPP
#define GTK_APP_HPP

#include <gtk/gtk.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../core/config/config.hpp"
#include "../core/gcc/gcc.hpp"
#include "../core/loger/loger.hpp"
#include "../core/project/project.hpp"

/**
 * @brief   Класс графического пользовательского интерфейса приложения.
 * @details Создаёт страницы GTK4, управляет проектом и профилями; сборка выполняется
 *          в отдельном потоке, а виджеты изменяются исключительно из GTK main loop.
 */
class gtk_app_t final {
private:
    struct settings_t {
        std::filesystem::path project_path = ".";
        std::filesystem::path build_root = "C:/PGR/build";
        std::string target_name = "default";
        std::vector<std::filesystem::path> excluded_dirs;
        std::vector<std::filesystem::path> selected_sources;
        gcc_t::build_settings_t compiler;
    } settings;

    GtkApplication* application = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* summary_label = nullptr;
    GtkWidget* project_entry = nullptr;
    GtkWidget* build_root_entry = nullptr;
    GtkWidget* target_entry = nullptr;
    GtkWidget* excluded_entry = nullptr;
    GtkWidget* source_box = nullptr;
    GtkWidget* source_count_label = nullptr;
    GtkWidget* build_button = nullptr;
    GtkWidget* clean_button = nullptr;
    GtkWidget* spinner = nullptr;
    GtkWidget* log_view = nullptr;
    GtkWidget* report_view = nullptr;
    GtkWidget* report_choice = nullptr;

    GtkWidget* c_standard = nullptr;
    GtkWidget* cpp_standard = nullptr;
    GtkWidget* c_optimization = nullptr;
    GtkWidget* cpp_optimization = nullptr;
    GtkWidget* c_warnings = nullptr;
    GtkWidget* cpp_warnings = nullptr;
    GtkWidget* c_debug = nullptr;
    GtkWidget* cpp_debug = nullptr;
    GtkWidget* c_werror = nullptr;
    GtkWidget* cpp_werror = nullptr;
    GtkWidget* c_flags = nullptr;
    GtkWidget* cpp_flags = nullptr;
    GtkWidget* gcc_command = nullptr;
    GtkWidget* gpp_command = nullptr;
    GtkWidget* include_dirs = nullptr;
    GtkWidget* library_dirs = nullptr;
    GtkWidget* libraries = nullptr;
    GtkWidget* linking_flags = nullptr;
    GtkWidget* executable_name = nullptr;
    GtkWidget* target_flags = nullptr;
    GtkWidget* linker_script = nullptr;
    GtkWidget* analysis_mode = nullptr;
    GtkWidget* optimization_report = nullptr;
    GtkWidget* time_report = nullptr;
    GtkWidget* binary_reports = nullptr;
    GtkWidget* binary_map = nullptr;
    GtkWidget* binary_bin = nullptr;
    GtkWidget* binary_hex = nullptr;
    GtkWidget* toolchain_prefix = nullptr;
    GtkWidget* flash_origin = nullptr;
    GtkWidget* flash_size = nullptr;
    GtkWidget* ram_origin = nullptr;
    GtkWidget* ram_size = nullptr;

    std::vector<std::pair<GtkWidget*, std::filesystem::path>> source_checks;
    std::thread build_worker;
    std::atomic<bool> building{false};
    std::atomic<int> build_exit_code{0};
    guint update_timer = 0;
    std::uintmax_t last_log_size = static_cast<std::uintmax_t>(-1);
    std::filesystem::path last_log_path;

    /** @brief   Проверка, что открыт исходный проект самой системы сборки. */
    static bool is_pgr_project(const std::filesystem::path& root) {
        return std::filesystem::is_regular_file(root / "main.cpp") &&
               std::filesystem::is_regular_file(root / "gui" / "gtk_app.hpp") &&
               std::filesystem::is_regular_file(root / "core" / "gcc" / "gcc.hpp");
    }

    /** @brief   Настройка сканирования проекта с учётом устаревшего GTK CPP. */
    static void prepare_scan(project_t& project, const settings_t& selected,
                             bool include_selection = true) {
        project.set_target(selected.target_name);
        for (const auto& path : selected.excluded_dirs) project.exclude(path);
        auto sources = include_selection ? selected.selected_sources :
            std::vector<std::filesystem::path>{};
        if (is_pgr_project(selected.project_path)) {
            // GTK4 реализован только в gtk_app.hpp. Старый CPP мог остаться
            // после распаковки ZIP поверх ранее установленной версии.
            project.exclude("gui/gtk_app.cpp");
            sources.erase(std::remove(sources.begin(), sources.end(),
                                     std::filesystem::path("gui/gtk_app.cpp")), sources.end());
            // В режиме автоматического выбора не компилируем тестовый main.cpp.
            if (sources.empty()) project.exclude("test");
        }
        project.select_sources(sources);
    }

    /** @brief   Получение аргументов pkg-config без интерпретации командной оболочкой. */
    static std::vector<std::string> gtk4_flags(const char* option) {
        gchar* args[] = {const_cast<gchar*>("pkg-config"),
                         const_cast<gchar*>(option),
                         const_cast<gchar*>("gtk4"), nullptr};
        gchar* output = nullptr;
        gchar* diagnostics = nullptr;
        gint exit_status = -1;
        GError* error = nullptr;
        const gboolean launched = g_spawn_sync(nullptr, args, nullptr,
            G_SPAWN_SEARCH_PATH, nullptr, nullptr, &output, &diagnostics,
            &exit_status, &error);
        if (!launched) {
            const std::string reason = error ? error->message : "unknown error";
            if (error) g_error_free(error);
            g_free(output);
            g_free(diagnostics);
            throw std::runtime_error("PKG-CONFIG START FAILED: " + reason);
        }
        if (!g_spawn_check_wait_status(exit_status, &error)) {
            const std::string reason = error ? error->message : "unknown error";
            if (error) g_error_free(error);
            g_free(output);
            g_free(diagnostics);
            throw std::runtime_error("GTK4 DEVELOPMENT PACKAGE NOT FOUND: " + reason);
        }
        gint count = 0;
        gchar** words = nullptr;
        if (!g_shell_parse_argv(output ? output : "", &count, &words, &error)) {
            const std::string reason = error ? error->message : "unknown error";
            if (error) g_error_free(error);
            g_free(output);
            g_free(diagnostics);
            throw std::runtime_error("INVALID PKG-CONFIG OUTPUT: " + reason);
        }
        std::vector<std::string> result;
        for (gint index = 0; index < count; ++index) result.emplace_back(words[index]);
        g_strfreev(words);
        g_free(output);
        g_free(diagnostics);
        return result;
    }

    /** @brief   Создание отдельного GTK4-профиля для самосборки графического приложения. */
    void configure_gtk4_self_build() {
        if (building.load()) return;
        read_widgets(false);
        if (!is_pgr_project(settings.project_path))
            throw std::runtime_error("GTK4 SELF-BUILD IS AVAILABLE ONLY FOR PGR MAKE");
        const auto cflags = gtk4_flags("--cflags");
        const auto libraries = gtk4_flags("--libs");
        settings.compiler = gcc_t::build_settings_t{};
        settings.target_name = "GTK4";
#ifdef _WIN32
        settings.compiler.executable_name = "pgr_gui.exe";
#else
        settings.compiler.executable_name = "pgr_gui";
#endif
        settings.selected_sources.clear();
        const auto append_unique = [](auto& items, const auto& value) {
            if (std::find(items.begin(), items.end(), value) == items.end())
                items.push_back(value);
        };
        append_unique(settings.compiler.cpp.flags, std::string("-DPGR_WITH_GTK4"));
        append_unique(settings.compiler.cpp.flags, std::string("-pthread"));
        append_unique(settings.compiler.linking_flags, std::string("-pthread"));
        for (std::size_t i = 0; i < cflags.size(); ++i) {
            const auto& flag = cflags[i];
            if (flag == "-I" && i + 1 < cflags.size())
                append_unique(settings.compiler.include_dirs, std::filesystem::path(cflags[++i]));
            else if (flag.rfind("-I", 0) == 0 && flag.size() > 2)
                append_unique(settings.compiler.include_dirs, std::filesystem::path(flag.substr(2)));
            else
                append_unique(settings.compiler.cpp.flags, flag);
        }
        for (std::size_t i = 0; i < libraries.size(); ++i) {
            const auto& flag = libraries[i];
            if (flag == "-L" && i + 1 < libraries.size())
                append_unique(settings.compiler.library_dirs, std::filesystem::path(libraries[++i]));
            else if (flag.rfind("-L", 0) == 0 && flag.size() > 2)
                append_unique(settings.compiler.library_dirs, std::filesystem::path(flag.substr(2)));
            else if (flag == "-l" && i + 1 < libraries.size())
                append_unique(settings.compiler.libraries, libraries[++i]);
            else if (flag.rfind("-l", 0) == 0 && flag.size() > 2)
                append_unique(settings.compiler.libraries, flag.substr(2));
            else
                append_unique(settings.compiler.linking_flags, flag);
        }
        gcc_t::validate_settings(settings.compiler);
        write_widgets();
        refresh_sources(false);
        status("GTK4 настроен через pkg-config; сохраните профиль, затем выполните сборку");
    }

    /** @brief   Получение значения текстового поля. */
    static std::string text_of(GtkWidget* entry) {
        return gtk_editable_get_text(GTK_EDITABLE(entry));
    }

    /** @brief   Изменение значения текстового поля. */
    static void put_text(GtkWidget* entry, const std::string& value) {
        gtk_editable_set_text(GTK_EDITABLE(entry), value.c_str());
    }

    /** @brief   Удаление пробельных символов по краям значения. */
    static std::string trim(std::string value) {
        const auto valid = [](unsigned char value) { return !std::isspace(value); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), valid));
        value.erase(std::find_if(value.rbegin(), value.rend(), valid).base(), value.end());
        return value;
    }

    /** @brief   В полях со списками каждый элемент отделяется символом ';'. */
    static std::vector<std::string> split(const std::string& value) {
        std::vector<std::string> result;
        std::istringstream input(value);
        std::string item;
        while (std::getline(input, item, ';')) {
            item = trim(item);
            if (!item.empty()) result.push_back(item);
        }
        return result;
    }

    static std::string join(const std::vector<std::string>& values) {
        std::string result;
        for (const auto& item : values) {
            if (!result.empty()) result += "; ";
            result += item;
        }
        return result;
    }

    static std::vector<std::filesystem::path> paths_of(const std::string& value) {
        std::vector<std::filesystem::path> result;
        for (const auto& item : split(value)) result.emplace_back(item);
        return result;
    }

    static std::string join_paths(const std::vector<std::filesystem::path>& values) {
        std::vector<std::string> result;
        for (const auto& path : values) result.push_back(path.generic_string());
        return join(result);
    }

    static std::string hex_value(std::uint64_t value) {
        std::ostringstream output;
        output << "0x" << std::hex << std::uppercase << value;
        return output.str();
    }

    static std::uint64_t parse_number(GtkWidget* entry) {
        const auto value = trim(text_of(entry));
        if (value.empty()) return 0;
        std::size_t used = 0;
        const auto result = std::stoull(value, &used, 0);
        if (used != value.size()) throw std::invalid_argument("INVALID MEMORY ADDRESS OR SIZE");
        return result;
    }

    static GtkWidget* title(const char* value) {
        GtkWidget* label = gtk_label_new(value);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        return label;
    }

    static GtkWidget* button(const char* value, GCallback callback, gpointer data) {
        GtkWidget* result = gtk_button_new_with_label(value);
        g_signal_connect(result, "clicked", callback, data);
        return result;
    }

    static GtkWidget* entry(const char* placeholder = nullptr) {
        GtkWidget* result = gtk_entry_new();
        gtk_widget_set_hexpand(result, TRUE);
        if (placeholder) gtk_entry_set_placeholder_text(GTK_ENTRY(result), placeholder);
        return result;
    }

    static GtkWidget* dropdown(const char* const* options) {
        GtkWidget* result = gtk_drop_down_new_from_strings(options);
        gtk_widget_set_hexpand(result, TRUE);
        return result;
    }

    static GtkWidget* panel(GtkWidget* parent, const char* caption) {
        GtkWidget* result = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_box_append(GTK_BOX(parent), result);
        gtk_box_append(GTK_BOX(result), title(caption));
        return result;
    }

    static GtkWidget* grid(GtkWidget* parent) {
        GtkWidget* result = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(result), 16);
        gtk_grid_set_row_spacing(GTK_GRID(result), 12);
        gtk_box_append(GTK_BOX(parent), result);
        return result;
    }

    static void field(GtkWidget* form, int row, const char* name, GtkWidget* control) {
        GtkWidget* caption = gtk_label_new(name);
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_widget_set_valign(caption, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(form), caption, 0, row, 1, 1);
        gtk_widget_set_hexpand(control, TRUE);
        gtk_grid_attach(GTK_GRID(form), control, 1, row, 1, 1);
    }

    static GtkWidget* page(GtkWidget* stack, const char* name, const char* label,
                            const char* heading, const char* description) {
        GtkWidget* scroller = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
        gtk_widget_set_margin_top(content, 28);
        gtk_widget_set_margin_bottom(content, 32);
        gtk_widget_set_margin_start(content, 30);
        gtk_widget_set_margin_end(content, 30);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
        gtk_stack_add_titled(GTK_STACK(stack), scroller, name, label);
        gtk_box_append(GTK_BOX(content), title(heading));
        GtkWidget* description_label = title(description);
        gtk_box_append(GTK_BOX(content), description_label);
        return content;
    }

    /** @brief   Показ статуса; вызывается только из потока GTK. */
    void status(const std::string& message) {
        if (status_label) gtk_label_set_text(GTK_LABEL(status_label), message.c_str());
    }

    std::filesystem::path project_name_path() const {
        return settings.project_path.lexically_normal().filename();
    }

    std::string project_name() const {
        const std::string base = project_name_path().string();
        return settings.target_name == "default" ? base : base + "__" + settings.target_name;
    }

    std::filesystem::path build_directory() const {
        return settings.build_root / project_name();
    }

    std::filesystem::path log_path() const {
        return build_directory() / (project_name() + ".log");
    }

    /** @brief   Проверка имени цели без допуска компонентов пути. */
    static void check_target(const std::string& target) {
        (void)config_t::path_for(".", target);
    }

    /** @brief   Применение полей GTK к модели конфигурации. */
    void read_widgets(bool read_sources = true) {
        settings_t updated = settings;
        const auto root = std::filesystem::canonical(text_of(project_entry));
        if (!std::filesystem::is_directory(root) || root.filename().empty())
            throw std::invalid_argument("INVALID PROJECT DIRECTORY");
        const bool project_changed = root != settings.project_path;
        updated.project_path = root;
        if (project_changed) {
            // Нельзя применять флажки старого проекта к новому каталогу.
            updated.selected_sources.clear();
            read_sources = false;
        }
        updated.build_root = trim(text_of(build_root_entry));
        if (updated.build_root.empty()) throw std::invalid_argument("EMPTY BUILD ROOT");
        updated.target_name = trim(text_of(target_entry));
        check_target(updated.target_name);
        updated.excluded_dirs = paths_of(text_of(excluded_entry));
        for (const auto& excluded : updated.excluded_dirs) {
            if (excluded.is_absolute() || excluded == "." || excluded.empty())
                throw std::invalid_argument("INVALID EXCLUDED DIRECTORY");
            for (const auto& part : excluded)
                if (part == "." || part == "..") throw std::invalid_argument("INVALID EXCLUDED DIRECTORY");
        }
        auto& c = updated.compiler.c;
        auto& cpp = updated.compiler.cpp;
        static constexpr gcc_t::standart_t c_standards[] = {
            gcc_t::standart_t::C99, gcc_t::standart_t::C11,
            gcc_t::standart_t::C17, gcc_t::standart_t::C23
        };
        static constexpr gcc_t::standart_t cpp_standards[] = {
            gcc_t::standart_t::CPP11, gcc_t::standart_t::CPP14,
            gcc_t::standart_t::CPP17, gcc_t::standart_t::CPP20,
            gcc_t::standart_t::CPP23
        };
        c.standart = c_standards[std::min(gtk_drop_down_get_selected(GTK_DROP_DOWN(c_standard)), 3u)];
        cpp.standart = cpp_standards[std::min(gtk_drop_down_get_selected(GTK_DROP_DOWN(cpp_standard)), 4u)];
        c.optimization = static_cast<gcc_t::optimization_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(c_optimization)));
        cpp.optimization = static_cast<gcc_t::optimization_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(cpp_optimization)));
        c.warnings = static_cast<gcc_t::warning_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(c_warnings)));
        cpp.warnings = static_cast<gcc_t::warning_t>(gtk_drop_down_get_selected(GTK_DROP_DOWN(cpp_warnings)));
        static constexpr gcc_t::debug_t debugs[] = {
            gcc_t::debug_t::OFF, gcc_t::debug_t::G, gcc_t::debug_t::G3
        };
        c.debug = debugs[std::min(gtk_drop_down_get_selected(GTK_DROP_DOWN(c_debug)), 2u)];
        cpp.debug = debugs[std::min(gtk_drop_down_get_selected(GTK_DROP_DOWN(cpp_debug)), 2u)];
        c.warnings_as_errors = gtk_check_button_get_active(GTK_CHECK_BUTTON(c_werror));
        cpp.warnings_as_errors = gtk_check_button_get_active(GTK_CHECK_BUTTON(cpp_werror));
        c.flags = split(text_of(c_flags));
        cpp.flags = split(text_of(cpp_flags));
        auto& compiler = updated.compiler;
        compiler.gcc_command = trim(text_of(gcc_command));
        compiler.gpp_command = trim(text_of(gpp_command));
        compiler.include_dirs = paths_of(text_of(include_dirs));
        compiler.library_dirs = paths_of(text_of(library_dirs));
        compiler.libraries = split(text_of(libraries));
        compiler.linking_flags = split(text_of(linking_flags));
        compiler.executable_name = trim(text_of(executable_name));
        compiler.target_flags = split(text_of(target_flags));
        compiler.linker_script = trim(text_of(linker_script));
        compiler.analysis.mode = static_cast<gcc_t::analysis_mode_t>(
            gtk_drop_down_get_selected(GTK_DROP_DOWN(analysis_mode)));
        compiler.analysis.optimization_report = gtk_check_button_get_active(GTK_CHECK_BUTTON(optimization_report));
        compiler.analysis.time_report = gtk_check_button_get_active(GTK_CHECK_BUTTON(time_report));
        compiler.binary.reports = gtk_check_button_get_active(GTK_CHECK_BUTTON(binary_reports));
        compiler.binary.linker_map = gtk_check_button_get_active(GTK_CHECK_BUTTON(binary_map));
        compiler.binary.generate_bin = gtk_check_button_get_active(GTK_CHECK_BUTTON(binary_bin));
        compiler.binary.generate_hex = gtk_check_button_get_active(GTK_CHECK_BUTTON(binary_hex));
        compiler.binary.toolchain_prefix = trim(text_of(toolchain_prefix));
        compiler.binary.flash_origin = parse_number(flash_origin);
        compiler.binary.flash_size = parse_number(flash_size);
        compiler.binary.ram_origin = parse_number(ram_origin);
        compiler.binary.ram_size = parse_number(ram_size);
        gcc_t::validate_settings(compiler);
        if (read_sources && !source_checks.empty()) {
            std::vector<std::filesystem::path> selected;
            for (const auto& item : source_checks)
                if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.first))) selected.push_back(item.second);
            if (selected.empty()) throw std::invalid_argument("SELECT AT LEAST ONE SOURCE FILE");
            if (selected.size() == source_checks.size()) selected.clear(); // AUTO.
            updated.selected_sources = std::move(selected);
        }
        settings = std::move(updated);
    }

    /** @brief   Загрузка актуальной модели в виджеты. */
    void write_widgets() {
        put_text(project_entry, settings.project_path.string());
        put_text(build_root_entry, settings.build_root.string());
        put_text(target_entry, settings.target_name);
        put_text(excluded_entry, join_paths(settings.excluded_dirs));
        const auto& c = settings.compiler.c;
        const auto& cpp = settings.compiler.cpp;
        const auto c_index = c.standart == gcc_t::standart_t::C99 ? 0u :
            c.standart == gcc_t::standart_t::C11 ? 1u : c.standart == gcc_t::standart_t::C23 ? 3u : 2u;
        const auto cpp_index = cpp.standart == gcc_t::standart_t::CPP11 ? 0u :
            cpp.standart == gcc_t::standart_t::CPP14 ? 1u : cpp.standart == gcc_t::standart_t::CPP20 ? 3u :
            cpp.standart == gcc_t::standart_t::CPP23 ? 4u : 2u;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c_standard), c_index);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(cpp_standard), cpp_index);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c_optimization), static_cast<guint>(c.optimization));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(cpp_optimization), static_cast<guint>(cpp.optimization));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c_warnings), static_cast<guint>(c.warnings));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(cpp_warnings), static_cast<guint>(cpp.warnings));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c_debug), c.debug == gcc_t::debug_t::G3 ? 2u :
                                   c.debug == gcc_t::debug_t::G ? 1u : 0u);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(cpp_debug), cpp.debug == gcc_t::debug_t::G3 ? 2u :
                                   cpp.debug == gcc_t::debug_t::G ? 1u : 0u);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(c_werror), c.warnings_as_errors);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(cpp_werror), cpp.warnings_as_errors);
        put_text(c_flags, join(c.flags));
        put_text(cpp_flags, join(cpp.flags));
        put_text(gcc_command, settings.compiler.gcc_command);
        put_text(gpp_command, settings.compiler.gpp_command);
        put_text(include_dirs, join_paths(settings.compiler.include_dirs));
        put_text(library_dirs, join_paths(settings.compiler.library_dirs));
        put_text(libraries, join(settings.compiler.libraries));
        put_text(linking_flags, join(settings.compiler.linking_flags));
        put_text(executable_name, settings.compiler.executable_name);
        put_text(target_flags, join(settings.compiler.target_flags));
        put_text(linker_script, settings.compiler.linker_script.string());
        gtk_drop_down_set_selected(GTK_DROP_DOWN(analysis_mode), static_cast<guint>(settings.compiler.analysis.mode));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(optimization_report), settings.compiler.analysis.optimization_report);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(time_report), settings.compiler.analysis.time_report);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(binary_reports), settings.compiler.binary.reports);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(binary_map), settings.compiler.binary.linker_map);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(binary_bin), settings.compiler.binary.generate_bin);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(binary_hex), settings.compiler.binary.generate_hex);
        put_text(toolchain_prefix, settings.compiler.binary.toolchain_prefix);
        put_text(flash_origin, hex_value(settings.compiler.binary.flash_origin));
        put_text(flash_size, hex_value(settings.compiler.binary.flash_size));
        put_text(ram_origin, hex_value(settings.compiler.binary.ram_origin));
        put_text(ram_size, hex_value(settings.compiler.binary.ram_size));
    }

    /** @brief   Сканирование исходников на основе сохранённых исключений. */
    void refresh_sources(bool read_form = true) {
        if (read_form) read_widgets(false);
        while (GtkWidget* child = gtk_widget_get_first_child(source_box))
            gtk_box_remove(GTK_BOX(source_box), child);
        source_checks.clear();
        project_t project(settings.project_path);
        prepare_scan(project, settings, false); // Отобразить и невыбранные файлы.
        if (project.scan() != 0) throw std::runtime_error("PROJECT SCAN FAILED");
        for (const auto& item : project.sources()) {
            const auto relative = item.path.lexically_relative(project.root());
            const bool checked = settings.selected_sources.empty() ||
                std::find(settings.selected_sources.begin(), settings.selected_sources.end(), relative)
                != settings.selected_sources.end();
            GtkWidget* check = gtk_check_button_new_with_label(relative.generic_string().c_str());
            gtk_check_button_set_active(GTK_CHECK_BUTTON(check), checked);
            gtk_box_append(GTK_BOX(source_box), check);
            source_checks.emplace_back(check, relative);
        }
        const auto count = project.sources().size();
        gtk_label_set_text(GTK_LABEL(source_count_label),
            (std::to_string(count) + " исходных файлов (C / C++ / ASM)").c_str());
        refresh_summary(count);
    }

    void refresh_summary(std::size_t source_count) {
        const auto value = "Проект: " + project_name() + "   |   Исходники: " +
            std::to_string(source_count) + "   |   Сборка: " + build_directory().string();
        gtk_label_set_text(GTK_LABEL(summary_label), value.c_str());
    }

    /** @brief   Выбор нового корневого каталога. */
    void select_project(const std::filesystem::path& root) {
        if (building.load()) return;
        const auto path = std::filesystem::canonical(root);
        if (!std::filesystem::is_directory(path) || path.filename().empty())
            throw std::invalid_argument("INVALID PROJECT DIRECTORY");
        settings.project_path = path;
        settings.target_name = "default";
        settings.compiler = gcc_t::build_settings_t{};
        settings.excluded_dirs.clear();
        settings.selected_sources.clear();
        load_profile_internal("default", false);
        write_widgets();
        refresh_sources(false);
        refresh_log();
        status("Выбран проект: " + path.string());
    }

    /** @brief   Подключение настроек выбранного профиля. */
    void load_profile_internal(const std::string& target, bool required) {
        check_target(target);
        config_t::profile_t profile;
        const auto file = config_t::path_for(settings.project_path, target);
        if (config_t::load(file, profile)) {
            settings.compiler = std::move(profile.compiler);
            settings.excluded_dirs = std::move(profile.exclusions);
            settings.selected_sources = std::move(profile.sources);
        } else if (required) {
            throw std::runtime_error("PROFILE NOT FOUND: " + file.string());
        }
        settings.target_name = target;
    }

    void load_profile() {
        if (building.load()) return;
        const std::string target = trim(text_of(target_entry));
        const std::filesystem::path root = std::filesystem::canonical(text_of(project_entry));
        check_target(target);
        config_t::profile_t profile;
        const auto file = config_t::path_for(root, target);
        if (!config_t::load(file, profile))
            throw std::runtime_error("PROFILE NOT FOUND: " + file.string());
        // Конфигурация заменяется только после успешной загрузки.
        settings.project_path = root;
        settings.target_name = target;
        settings.compiler = std::move(profile.compiler);
        settings.excluded_dirs = std::move(profile.exclusions);
        settings.selected_sources = std::move(profile.sources);
        write_widgets();
        refresh_sources(false);
        refresh_log();
        status("Профиль загружен: " + target);
    }

    void save_profile() {
        if (building.load()) return;
        const auto old_root = settings.project_path;
        read_widgets();
        if (settings.project_path != old_root) refresh_sources(false);
        const auto file = config_t::path_for(settings.project_path, settings.target_name);
        const config_t::profile_t profile{
            settings.compiler, settings.excluded_dirs, settings.selected_sources
        };
        if (!config_t::save(file, profile))
            throw std::runtime_error("CANNOT SAVE PROFILE: " + file.string());
        status("Профиль сохранён: " + file.string());
    }

    /** @brief   Вывод ограниченного объёма файла, чтобы GUI не зависал на больших отчётах. */
    static std::string read_tail(const std::filesystem::path& path, std::size_t limit = 256 * 1024) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return "Файл пока не создан: " + path.string();
        const auto end = input.tellg();
        if (end < 0) return "Не удалось прочитать файл";
        const auto size = static_cast<std::uint64_t>(end);
        const auto offset = size > limit ? size - limit : 0;
        input.seekg(static_cast<std::streamoff>(offset));
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (offset) {
            const auto first_line = text.find('\n');
            if (first_line != std::string::npos) text.erase(0, first_line + 1);
            return "[Показана последняя часть файла]\n" + text;
        }
        return text;
    }

    static void set_text_view(GtkWidget* view, const std::string& text) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        // Вывод компилятора может содержать байты в локальной кодировке Windows.
        gchar* utf8 = g_utf8_make_valid(text.data(), static_cast<gssize>(text.size()));
        gtk_text_buffer_set_text(buffer, utf8, -1);
        g_free(utf8);
    }

    void refresh_log() {
        const auto path = log_path();
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        const auto actual_size = error ? 0 : size;
        if (path == last_log_path && actual_size == last_log_size) return;
        last_log_path = path;
        last_log_size = actual_size;
        set_text_view(log_view, read_tail(path));
    }

    void refresh_report() {
        static const char* const reports[] = {
            "memory.txt", "sections.txt", "symbols.txt", "disassembly.txt",
            "size-summary.txt", "format.txt", "file-headers.txt", "program-headers.txt"
        };
        const auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(report_choice));
        const auto file = build_directory() / "binary" /
            reports[std::min(selected, 7u)];
        set_text_view(report_view, read_tail(file, 400 * 1024));
    }

    /** @brief   Создание фонового процесса сборки без блокировки GTK main loop. */
    void start_build() {
        if (building.load()) return;
        const auto old_root = settings.project_path;
        read_widgets();
        if (settings.project_path != old_root) refresh_sources(false);
        project_t project(settings.project_path);
        prepare_scan(project, settings);
        if (project.scan() != 0 || project.sources().empty())
            throw std::runtime_error("NO SOURCE FILES FOR BUILD");
        if (build_worker.joinable()) build_worker.join();
        const settings_t snapshot = settings;
        // Все виджеты GTK изменяются только из главного потока.
        building.store(true);
        build_exit_code.store(-1);
        gtk_widget_set_sensitive(build_button, FALSE);
        gtk_widget_set_sensitive(clean_button, FALSE);
        gtk_spinner_start(GTK_SPINNER(spinner));
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "log");
        status("Сборка выполняется: " + project.name());
        // Только GUI main loop изменяет виджеты. Поток сборки пишет в обычный журнал.
        try {
            build_worker = std::thread([this, snapshot]() {
                int result = 1;
                try {
                    project_t selected(snapshot.project_path);
                    prepare_scan(selected, snapshot);
                    if (selected.scan() != 0 || selected.sources().empty()) {
                        throw std::runtime_error("PROJECT SCAN FAILED");
                    }
                    loger_t loger(selected.name(), snapshot.build_root);
                    std::cout << "\n[GTK4] BUILD START -> " << selected.name() << '\n';
                    gcc_t gcc;
                    gcc.set_build_dir(snapshot.build_root);
                    gcc.set_build_settings(snapshot.compiler);
                    gcc.set_command_executor([&loger](const std::string& command) {
                        return loger.execute(command);
                    });
                    result = gcc.run(selected);
                    std::cout << "[GTK4] BUILD FINISHED, EXIT CODE: " << result << '\n';
                } catch (const std::exception& exception) {
                    std::cerr << "[GTK4] BUILD ERROR: " << exception.what() << '\n';
                }
                build_exit_code.store(result);
                building.store(false);
            });
        } catch (...) {
            building.store(false);
            gtk_widget_set_sensitive(build_button, TRUE);
            gtk_widget_set_sensitive(clean_button, TRUE);
            gtk_spinner_stop(GTK_SPINNER(spinner));
            throw;
        }
    }

    /** @brief   Безопасная очистка только известных каталогов выбранной цели. */
    void clean_now() {
        if (building.load()) return;
        read_widgets(false);
        const auto target = std::filesystem::absolute(settings.build_root / project_name()).lexically_normal();
        const auto root = std::filesystem::absolute(settings.build_root).lexically_normal();
        if (target.parent_path() != root || !std::filesystem::is_directory(target)) {
            status("Каталог результатов отсутствует");
            return;
        }
        std::error_code error;
        if (std::filesystem::is_symlink(target, error) || error)
            throw std::runtime_error("REFUSING TO CLEAN SYMLINK");
        std::uintmax_t removed = 0;
        for (const char* name : {"preprocessing", "compilation", "assembly", "linking",
                                 "dependencies", "cache", "analysis", "binary"}) {
            const auto directory = target / name;
            error.clear();
            if (std::filesystem::is_symlink(directory, error) || error) continue;
            removed += std::filesystem::remove_all(directory, error);
            if (error) throw std::runtime_error("CLEAN FAILED: " + error.message());
        }
        refresh_log();
        status("Очистка завершена; удалено объектов: " + std::to_string(removed) +
               ". Журнал сохранён.");
    }

    /** @brief   Обновление UI после завершения фоновой сборки. */
    gboolean tick() {
        if (window == nullptr) return G_SOURCE_CONTINUE;
        refresh_log();
        if (!building.load() && build_worker.joinable()) {
            build_worker.join();
            gtk_spinner_stop(GTK_SPINNER(spinner));
            gtk_widget_set_sensitive(build_button, TRUE);
            gtk_widget_set_sensitive(clean_button, TRUE);
            const int code = build_exit_code.load();
            status(code == 0 ? "Сборка завершена успешно" :
                "Сборка завершилась с ошибкой (код " + std::to_string(code) + ")");
            refresh_report();
        }
        return G_SOURCE_CONTINUE;
    }

    /** @brief   Формирование главного окна и страниц. */
    void create_window() {
        if (window != nullptr) {
            gtk_window_present(GTK_WINDOW(window));
            return;
        }
        window = gtk_application_window_new(application);
        gtk_window_set_title(GTK_WINDOW(window), "PaperGooseRus Make");
        gtk_window_set_default_size(GTK_WINDOW(window), 1250, 820);
        g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
            static_cast<gtk_app_t*>(data)->window = nullptr;
        }), this);
        g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
            auto* self = static_cast<gtk_app_t*>(data);
            if (self->building.load()) {
                self->status("Сначала дождитесь завершения сборки, затем закройте окно");
                return TRUE;
            }
            return FALSE;
        }), this);
        GtkWidget* vertical = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_window_set_child(GTK_WINDOW(window), vertical);
        GtkWidget* header = gtk_header_bar_new();
        gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title("PGR MAKE"));
        gtk_window_set_titlebar(GTK_WINDOW(window), header);
        spinner = gtk_spinner_new();
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), spinner);
        build_button = button("Собрать", G_CALLBACK(on_build), this);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), build_button);
        GtkWidget* center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_vexpand(center, TRUE);
        gtk_box_append(GTK_BOX(vertical), center);
        stack = gtk_stack_new();
        gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
        gtk_widget_set_hexpand(stack, TRUE);
        gtk_widget_set_vexpand(stack, TRUE);
        GtkWidget* sidebar = gtk_stack_sidebar_new();
        gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
        gtk_widget_set_size_request(sidebar, 210, -1);
        gtk_box_append(GTK_BOX(center), sidebar);
        gtk_box_append(GTK_BOX(center), stack);

        make_overview();
        make_sources();
        make_compiler();
        make_analysis();
        make_embedded();
        make_log();
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "overview");

        GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        status_label = gtk_label_new("Готов к работе");
        gtk_widget_set_halign(status_label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(status_label, TRUE);
        gtk_box_append(GTK_BOX(footer), status_label);
        gtk_box_append(GTK_BOX(vertical), footer);
        write_widgets();
        try { refresh_sources(false); } catch (const std::exception& e) { status(e.what()); }
        refresh_log();
        refresh_report();
        if (!update_timer) update_timer = g_timeout_add(400, on_timer, this);
        gtk_window_present(GTK_WINDOW(window));
    }

    /** @brief   Раздел проекта, профилей и основных действий. */
    void make_overview() {
        GtkWidget* content = page(stack, "overview", "Проект", "Обзор проекта",
            "Выбор исходного каталога, профиля и места сохранения результатов сборки.");
        GtkWidget* project_panel = panel(content, "Рабочий проект");
        GtkWidget* form = grid(project_panel);
        project_entry = entry("C:/Project/MyProject");
        field(form, 0, "Каталог проекта", project_entry);
        GtkWidget* chooser = button("Выбрать папку…", G_CALLBACK(on_choose_project), this);
        gtk_grid_attach(GTK_GRID(form), chooser, 2, 0, 1, 1);
        build_root_entry = entry("C:/PGR/build");
        field(form, 1, "Каталог результатов", build_root_entry);
        target_entry = entry("default");
        field(form, 2, "Профиль / цель", target_entry);
        GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(project_panel), actions);
        gtk_box_append(GTK_BOX(actions), button("Загрузить профиль", G_CALLBACK(on_load_profile), this));
        gtk_box_append(GTK_BOX(actions), button("Сохранить профиль", G_CALLBACK(on_save_profile), this));
        gtk_box_append(GTK_BOX(actions), button("Обновить проект", G_CALLBACK(on_refresh_sources), this));
        gtk_box_append(GTK_BOX(project_panel),
            button("Настроить самосборку GTK4", G_CALLBACK(on_configure_gtk4), this));
        GtkWidget* info = panel(content, "Текущая конфигурация");
        summary_label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(summary_label), 0);
        gtk_label_set_wrap(GTK_LABEL(summary_label), TRUE);
        gtk_box_append(GTK_BOX(info), summary_label);
        GtkWidget* extra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(info), extra);
        gtk_box_append(GTK_BOX(extra), button("Открыть результаты", G_CALLBACK(on_open_output), this));
        clean_button = button("Очистить результаты…", G_CALLBACK(on_clean), this);
        gtk_box_append(GTK_BOX(extra), clean_button);
        GtkWidget* note = title("Журнал <проект>.log сохраняется при очистке. Все параметры передаются в существующий gcc_t.");
        gtk_box_append(GTK_BOX(content), note);
    }

    /** @brief   Список исходников и исключаемых каталогов. */
    void make_sources() {
        GtkWidget* content = page(stack, "sources", "Исходники", "Исходные файлы",
            "Выбор файлов для текущей цели. Если выбраны все, используется автоматическое сканирование.");
        GtkWidget* exclusions = panel(content, "Исключаемые каталоги");
        GtkWidget* form = grid(exclusions);
        excluded_entry = entry("test; gui; examples");
        field(form, 0, "Относительные пути (через ;) ", excluded_entry);
        gtk_box_append(GTK_BOX(exclusions), button("Пересканировать", G_CALLBACK(on_refresh_sources), this));
        GtkWidget* files = panel(content, "Файлы, включённые в сборку");
        source_count_label = title("0 файлов");
        gtk_box_append(GTK_BOX(files), source_count_label);
        source_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
        gtk_box_append(GTK_BOX(files), source_box);
        gtk_box_append(GTK_BOX(files), button("Применить выбор файлов", G_CALLBACK(on_apply_sources), this));
    }

    /** @brief   Параметры языков C/C++ и компоновщика. */
    void make_compiler() {
        static const char* const c_std[] = {"C99", "C11", "C17", "C23", nullptr};
        static const char* const cpp_std[] = {"C++11", "C++14", "C++17", "C++20", "C++23", nullptr};
        static const char* const opt[] = {"-O0", "-O1", "-O2", "-O3", "-Os", "-Og", nullptr};
        static const char* const warn[] = {"Default", "-Wall", "-Wall -Wextra", "Strict", nullptr};
        static const char* const debug[] = {"Off", "-g", "-g3", nullptr};
        GtkWidget* content = page(stack, "compiler", "GCC", "Параметры GCC",
            "Для C и C++ стандарты, оптимизации и предупреждения задаются раздельно.");
        GtkWidget* c = panel(content, "Компилятор C");
        GtkWidget* form = grid(c);
        c_standard = dropdown(c_std); field(form, 0, "Стандарт", c_standard);
        c_optimization = dropdown(opt); field(form, 1, "Оптимизация", c_optimization);
        c_warnings = dropdown(warn); field(form, 2, "Предупреждения", c_warnings);
        c_debug = dropdown(debug); field(form, 3, "Отладочная информация", c_debug);
        c_werror = gtk_check_button_new_with_label("Предупреждения как ошибки (-Werror)");
        gtk_box_append(GTK_BOX(c), c_werror);
        c_flags = entry("-DDEBUG; -fno-builtin"); field(form, 4, "Дополнительные флаги (;) ", c_flags);
        GtkWidget* cpp = panel(content, "Компилятор C++");
        form = grid(cpp);
        cpp_standard = dropdown(cpp_std); field(form, 0, "Стандарт", cpp_standard);
        cpp_optimization = dropdown(opt); field(form, 1, "Оптимизация", cpp_optimization);
        cpp_warnings = dropdown(warn); field(form, 2, "Предупреждения", cpp_warnings);
        cpp_debug = dropdown(debug); field(form, 3, "Отладочная информация", cpp_debug);
        cpp_werror = gtk_check_button_new_with_label("Предупреждения как ошибки (-Werror)");
        gtk_box_append(GTK_BOX(cpp), cpp_werror);
        cpp_flags = entry("-fno-exceptions"); field(form, 4, "Дополнительные флаги (;) ", cpp_flags);
        GtkWidget* linker = panel(content, "Компоновка и окружение");
        form = grid(linker);
        gcc_command = entry("gcc"); field(form, 0, "GCC", gcc_command);
        gpp_command = entry("g++"); field(form, 1, "G++", gpp_command);
        executable_name = entry("app.exe"); field(form, 2, "Исполняемый файл", executable_name);
        include_dirs = entry("include; vendor/include"); field(form, 3, "-I каталоги (;) ", include_dirs);
        library_dirs = entry("lib; vendor/lib"); field(form, 4, "-L каталоги (;) ", library_dirs);
        libraries = entry("m; pthread"); field(form, 5, "Библиотеки -l (;) ", libraries);
        linking_flags = entry("-Wl,--gc-sections"); field(form, 6, "Флаги компоновщика (;) ", linking_flags);
        gtk_box_append(GTK_BOX(content), button("Применить настройки", G_CALLBACK(on_apply_settings), this));
    }

    /** @brief   Настройки генерации GIMPLE, SSA, RTL и отчётов. */
    void make_analysis() {
        static const char* const modes[] = {"Выключено", "Основные представления", "Все проходы", nullptr};
        GtkWidget* content = page(stack, "analysis", "Исследование GCC", "Исследование GCC",
            "Сохранение промежуточных представлений компилятора и информации об оптимизациях.");
        GtkWidget* pane = panel(content, "GIMPLE / SSA / RTL");
        GtkWidget* form = grid(pane);
        analysis_mode = dropdown(modes);
        field(form, 0, "Режим дампов GCC", analysis_mode);
        optimization_report = gtk_check_button_new_with_label("Отчёт об оптимизациях (-fopt-info)");
        time_report = gtk_check_button_new_with_label("Отчёт времени компиляции (-ftime-report)");
        gtk_box_append(GTK_BOX(pane), optimization_report);
        gtk_box_append(GTK_BOX(pane), time_report);
        gtk_box_append(GTK_BOX(content), button("Применить настройки", G_CALLBACK(on_apply_settings), this));
        GtkWidget* hint = panel(content, "Где искать результаты");
        gtk_box_append(GTK_BOX(hint), title("<build>/<project>/analysis/<source>/ — GIMPLE, SSA, RTL и полные дампы GCC."));
    }

    /** @brief   Настройки ELF/PE, BIN/HEX и памяти Embedded. */
    void make_embedded() {
        GtkWidget* content = page(stack, "embedded", "Embedded", "ELF / PE и Embedded",
            "Инструменты GNU Binutils, образы прошивки и проверка размещения секций в памяти.");
        GtkWidget* binary = panel(content, "Анализ исполняемого файла");
        binary_reports = gtk_check_button_new_with_label("Секции, символы, size, дизассемблирование");
        binary_map = gtk_check_button_new_with_label("Карта GNU ld (.map)");
        binary_bin = gtk_check_button_new_with_label("Формировать BIN");
        binary_hex = gtk_check_button_new_with_label("Формировать Intel HEX");
        for (GtkWidget* item : {binary_reports, binary_map, binary_bin, binary_hex})
            gtk_box_append(GTK_BOX(binary), item);
        GtkWidget* toolchain = panel(content, "Целевая архитектура и компоновка");
        GtkWidget* form = grid(toolchain);
        toolchain_prefix = entry("arm-none-eabi-"); field(form, 0, "Префикс Binutils", toolchain_prefix);
        target_flags = entry("-mcpu=cortex-m4; -mthumb"); field(form, 1, "Флаги архитектуры (;) ", target_flags);
        linker_script = entry("memory.ld"); field(form, 2, "Linker script (-T)", linker_script);
        GtkWidget* memory = panel(content, "Области памяти");
        form = grid(memory);
        flash_origin = entry("0x08000000"); field(form, 0, "Flash origin", flash_origin);
        flash_size = entry("0x20000"); field(form, 1, "Flash size (bytes)", flash_size);
        ram_origin = entry("0x20000000"); field(form, 2, "RAM origin", ram_origin);
        ram_size = entry("0x8000"); field(form, 3, "RAM size (bytes)", ram_size);
        gtk_box_append(GTK_BOX(content), button("Применить настройки", G_CALLBACK(on_apply_settings), this));
        GtkWidget* reports = panel(content, "Просмотр результатов Binutils");
        static const char* const names[] = {
            "Память", "Секции", "Символы", "Дизассемблирование",
            "Размер", "Формат файла", "Заголовки", "Program headers", nullptr
        };
        report_choice = dropdown(names);
        gtk_box_append(GTK_BOX(reports), report_choice);
        gtk_box_append(GTK_BOX(reports), button("Открыть отчёт", G_CALLBACK(on_refresh_report), this));
        report_view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(report_view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(report_view), GTK_WRAP_NONE);
        GtkWidget* scroller = gtk_scrolled_window_new();
        gtk_widget_set_size_request(scroller, -1, 270);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), report_view);
        gtk_box_append(GTK_BOX(reports), scroller);
    }

    /** @brief   Просмотр общего журнала сборки. */
    void make_log() {
        GtkWidget* content = page(stack, "log", "Журнал", "Журнал сборки",
            "Вывод GCC, GNU Binutils и диагностика. В процессе сборки обновляется автоматически.");
        GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(content), controls);
        gtk_box_append(GTK_BOX(controls), button("Обновить журнал", G_CALLBACK(on_refresh_log), this));
        gtk_box_append(GTK_BOX(controls), button("Открыть результаты", G_CALLBACK(on_open_output), this));
        log_view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_NONE);
        GtkWidget* scroller = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scroller, TRUE);
        gtk_widget_set_size_request(scroller, -1, 480);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), log_view);
        gtk_box_append(GTK_BOX(content), scroller);
    }

    /** @brief   Единая обработка исключений GTK callbacks. */
    template <typename F>
    static void invoke(gpointer data, F callback) {
        auto* self = static_cast<gtk_app_t*>(data);
        try { (self->*callback)(); }
        catch (const std::exception& error) { self->status(std::string("Ошибка: ") + error.what()); }
    }

    static void on_build(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::start_build); }
    static void on_configure_gtk4(GtkButton*, gpointer data) {
        invoke(data, &gtk_app_t::configure_gtk4_self_build);
    }
    static void on_refresh_sources(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::refresh_sources_action); }
    static void on_apply_sources(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::apply_sources_action); }
    static void on_apply_settings(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::apply_settings_action); }
    static void on_save_profile(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::save_profile); }
    static void on_load_profile(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::load_profile); }
    static void on_refresh_log(GtkButton*, gpointer data) {
        static_cast<gtk_app_t*>(data)->last_log_size = static_cast<std::uintmax_t>(-1);
        invoke(data, &gtk_app_t::refresh_log);
    }
    static void on_refresh_report(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::refresh_report); }
    static gboolean on_timer(gpointer data) {
        auto* self = static_cast<gtk_app_t*>(data);
        try { return self->tick(); }
        catch (const std::exception& error) { self->status(error.what()); return G_SOURCE_CONTINUE; }
    }

    void refresh_sources_action() {
        if (building.load()) return;
        refresh_sources();
        status("Список исходников обновлён");
    }
    void apply_sources_action() {
        if (building.load()) return;
        read_widgets();
        status("Выбор исходных файлов применён");
    }
    void apply_settings_action() {
        if (building.load()) return;
        read_widgets();
        status("Настройки применены. Сохраните профиль, чтобы использовать их после перезапуска.");
    }

    /** @brief   Подтверждение удаления промежуточных файлов. */
    static void on_clean_confirm(GtkButton* clicked, gpointer data) {
        invoke(data, &gtk_app_t::clean_now);
        GtkWidget* dialog = gtk_widget_get_ancestor(GTK_WIDGET(clicked), GTK_TYPE_WINDOW);
        if (dialog) gtk_window_destroy(GTK_WINDOW(dialog));
    }
    static void on_clean(GtkButton*, gpointer data) {
        auto* self = static_cast<gtk_app_t*>(data);
        if (self->building.load()) return;
        GtkWidget* dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(dialog), "Подтверждение очистки");
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->window));
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 140);
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
        gtk_widget_set_margin_top(content, 20);
        gtk_widget_set_margin_bottom(content, 20);
        gtk_widget_set_margin_start(content, 20);
        gtk_widget_set_margin_end(content, 20);
        gtk_window_set_child(GTK_WINDOW(dialog), content);
        GtkWidget* caption = gtk_label_new("Удалить результаты выбранной цели? Журнал будет сохранён.");
        gtk_label_set_wrap(GTK_LABEL(caption), TRUE);
        gtk_box_append(GTK_BOX(content), caption);
        GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(content), actions);
        GtkWidget* cancel = gtk_button_new_with_label("Отмена");
        g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
        gtk_box_append(GTK_BOX(actions), cancel);
        gtk_box_append(GTK_BOX(actions), button("Удалить", G_CALLBACK(on_clean_confirm), data));
        gtk_window_present(GTK_WINDOW(dialog));
    }

    /** @brief   Открытие каталога результатов в системном файловом менеджере. */
    void open_output() {
        if (building.load()) return;
        read_widgets(false);
        const auto folder = build_directory();
        std::filesystem::create_directories(folder);
        GFile* file = g_file_new_for_path(folder.string().c_str());
        gchar* uri = g_file_get_uri(file);
        GError* error = nullptr;
        const gboolean launched = g_app_info_launch_default_for_uri(uri, nullptr, &error);
        g_free(uri);
        g_object_unref(file);
        if (!launched) {
            std::string message = error ? error->message : "FILE MANAGER START FAILED";
            if (error) g_error_free(error);
            throw std::runtime_error(message);
        }
    }
    static void on_open_output(GtkButton*, gpointer data) { invoke(data, &gtk_app_t::open_output); }

#if GTK_CHECK_VERSION(4, 10, 0)
    static void on_folder_selected(GObject* source, GAsyncResult* result, gpointer data) {
        auto* self = static_cast<gtk_app_t*>(data);
        GError* error = nullptr;
        GFile* folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
        if (folder) {
            gchar* path = g_file_get_path(folder);
            try { if (path) self->select_project(path); }
            catch (const std::exception& exception) { self->status(exception.what()); }
            g_free(path);
            g_object_unref(folder);
        }
        if (error) g_error_free(error); // Отмена выбора не является ошибкой сборки.
    }
#else
    static void on_folder_selected(GtkNativeDialog* native, int response, gpointer data) {
        auto* self = static_cast<gtk_app_t*>(data);
        if (response == GTK_RESPONSE_ACCEPT) {
            GFile* folder = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
            if (folder) {
                gchar* path = g_file_get_path(folder);
                try { if (path) self->select_project(path); }
                catch (const std::exception& exception) { self->status(exception.what()); }
                g_free(path);
                g_object_unref(folder);
            }
        }
        g_object_unref(native);
    }
#endif
    static void on_choose_project(GtkButton*, gpointer data) {
        auto* self = static_cast<gtk_app_t*>(data);
        if (self->building.load()) return;
#if GTK_CHECK_VERSION(4, 10, 0)
        GtkFileDialog* dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, "Выберите каталог проекта");
        gtk_file_dialog_select_folder(dialog, GTK_WINDOW(self->window), nullptr,
                                      on_folder_selected, self);
        g_object_unref(dialog);
#else
        GtkFileChooserNative* dialog = gtk_file_chooser_native_new("Выберите каталог проекта",
            GTK_WINDOW(self->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "Выбрать", "Отмена");
        g_signal_connect(dialog, "response", G_CALLBACK(on_folder_selected), self);
        gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
#endif
    }

    /** @brief   Callback-функция активации приложения. */
    static void activate(GtkApplication*, gpointer data) {
        static_cast<gtk_app_t*>(data)->create_window();
    }

public:
    /** @brief   Конструктор класса gtk_app_t. */
    explicit gtk_app_t(const std::filesystem::path& project_path = ".") {
        std::error_code error;
        settings.project_path = std::filesystem::weakly_canonical(project_path, error);
        if (error || !std::filesystem::is_directory(settings.project_path))
            settings.project_path = std::filesystem::current_path();
        if (!settings.project_path.filename().empty()) {
            try { load_profile_internal("default", false); }
            catch (const std::exception&) { /* Проект может ещё не содержать профилей. */ }
        }
        // Локальное приложение не использует регистрацию единственного экземпляра.
        // G_APPLICATION_NON_UNIQUE исключает ожидание регистрации через session bus,
        // которое в некоторых конфигурациях Windows/MSYS2 завершается тайм-аутом.
        application = gtk_application_new("org.pgr.make", G_APPLICATION_NON_UNIQUE);
        if (!application) throw std::runtime_error("GTK4 APPLICATION INITIALIZATION FAILED");
        g_signal_connect(application, "activate", G_CALLBACK(activate), this);
    }

    /** @brief   Запускает цикл событий GTK4. */
    int run(int argc, char* argv[]) {
        const int result = g_application_run(G_APPLICATION(application), argc, argv);
        if (update_timer) {
            g_source_remove(update_timer);
            update_timer = 0;
        }
        if (build_worker.joinable()) build_worker.join();
        return result;
    }

    /** @brief   Деструктор класса gtk_app_t. */
    ~gtk_app_t() {
        if (update_timer) g_source_remove(update_timer);
        if (build_worker.joinable()) build_worker.join();
        if (application) g_object_unref(application);
    }

    gtk_app_t(const gtk_app_t&) = delete;
    gtk_app_t& operator=(const gtk_app_t&) = delete;
};

#endif // GTK_APP_HPP
