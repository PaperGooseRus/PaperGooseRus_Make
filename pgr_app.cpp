#include <exception>
#include <iostream>

#include "gui/gtk_app.hpp"

class pgr_app_t final {
  public:
    pgr_app_t() {
        std::cout << "PGR App constructor." << std::endl;
    }

    ~pgr_app_t() {
        std::cout << "PGR App destructor." << std::endl;
    }

    int run(int argc, char* argv[]) {
        gtk_app_t gtk_app;
        return gtk_app.run(argc, argv);
    }
};

int main(int argc, char* argv[]) {
    try {
        pgr_app_t pgr_app;
        return pgr_app.run(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << "PGR App error: " << err.what() << std::endl;
        return 1;
    }
}
