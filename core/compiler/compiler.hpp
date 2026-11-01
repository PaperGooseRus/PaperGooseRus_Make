#ifndef COMPILER_HPP
#define COMPILER_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>

class compiler_t {
private:
    std::vector<std::string> cpp_files;
    std::vector<std::string> gcc_flags;
    std::string exe_file;

    static std::string quote(const std::string& s) {
        return '"' + s + '"';
    }

    void read_cpp_files() {
    	std::cout << "input cpp file (empty line to finish):\n";
      std::string line;

      while (std::getline(std::cin, line)) {
        if (line.empty()) {
          break;
        }

        cpp_files.push_back(line);
      }
    }

    void read_gcc_flags() {
    	std::cout << "input gcc flags (empty line to finish):\n";
      std::string line;

      while (std::getline(std::cin, line)) {
        if (line.empty()) {
          break;
        }

        gcc_flags.push_back(line);
      }
    }

    void read_exe_file() {
      std::cout << "input exe file: ";

      if (!std::getline(std::cin, exe_file) || exe_file.empty()) {
        std::cerr << "error: empty exe path\n";

        exe_file = "a.exe";
      }
    }

public:
    compiler_t() {
    	std::cout << "Compiler constructor.\n"; 
    }

    ~compiler_t() {
    	std::cout << "Compiler destructor.\n";
    }

    int run(int /*argc*/, char* /*argv*/[]) {
        read_cpp_files();
        read_gcc_flags();
        read_exe_file();

        if (cpp_files.empty()) {
            std::cerr << "error: no source files\n";

            return 1;
        }

        std::string cmd = "g++";
        for (const auto& f : gcc_flags) {
            cmd += ' ';
            cmd += quote(f);
        }
        for (const auto& f : cpp_files) {
            cmd += ' ';
            cmd += quote(f);
        }
        cmd += " -o ";
        cmd += quote(exe_file);

        std::cout << "[cmd] " << cmd << "\n";
        
        int rc = std::system(cmd.c_str());
        
        if (rc != 0) {
            std::cerr << "error: compiler returned " << rc << "\n";
        }
        
        return rc;
    }
};

#endif // COMPILER_HPP