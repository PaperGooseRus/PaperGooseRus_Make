#include <exception>
#include <iostream>

#include "core/gcc/gcc.hpp"

int main(int argc, char* argv[]) {
    try {
        gcc_t gcc;

        return gcc.run(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << "PGR App error: " << err.what() << std::endl;
        
        return 1;
    }
}