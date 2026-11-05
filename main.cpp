#include <exception>
#include <filesystem>
#include <iostream>

#include "core/cli/cli.hpp"
#include "core/gcc/gcc.hpp"
#include "core/loger/loger.hpp"

#ifdef PGR_WITH_GTK4
#include "gui/gtk_app.hpp"
#endif

static int run_cli(int argc, char* argv[]) {
  try {
    cli_t::settings_t settings;
    if (argc > 1) settings.project_path = argv[1];

    // Каждый запуск сборки создаёт журнал выбранного проекта.
    cli_t cli([](const project_t& project, const std::filesystem::path& build_root,
                 const gcc_t::build_settings_t& options) {
      loger_t loger(project.name(), build_root);
      gcc_t gcc;
      gcc.set_build_dir(build_root);
      gcc.set_build_settings(options);
      gcc.set_command_executor([&loger](const std::string& command) {
        return loger.execute(command);
      });
      return gcc.run(project);
    }, settings);

    return cli.run();
  } catch (const std::exception& error) {
    std::cerr << "PGR App error: " << error.what() << std::endl;
    return 1;
  }
}

// GTK4 и CLI используют одни и те же модули сборки, но независимые интерфейсы.
// Без PGR_WITH_GTK4 программа остаётся консольной и не требует библиотек GTK4.
int main(int argc, char* argv[]) {
#ifdef PGR_WITH_GTK4
  try {
    if (argc > 1 && std::string(argv[1]) == "--cli")
      return run_cli(argc - 1, argv + 1);
    const std::filesystem::path project = argc > 1 ? argv[1] : ".";
    gtk_app_t application(project);
    // GTK4 не должен воспринимать путь проекта как параметр GApplication.
    return application.run(1, argv);
  } catch (const std::exception& error) {
    std::cerr << "PGR GTK4 error: " << error.what() << std::endl;
    return 1;
  }
#else
  return run_cli(argc, argv);
#endif
}
