#ifndef BINARY_HPP
#define BINARY_HPP

#include "../process/process.hpp"
#include "../cache/cache.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <utility>
#include <algorithm>
#include <cctype>

#ifndef _WIN32
#include <sys/wait.h>
#endif

// Анализ ELF/PE и формирование образов через инструменты GNU Binutils.
// Класс не управляет компиляцией: все операции выполняются ПОСЛЕ успешной компоновки.
class binary_t {
public:
  enum class format_t {
    UNKNOWN = 0x00,
    ELF =     0x01,
    PE =      0x02
  };

  struct settings_t {
    bool reports = false;         //!< Секции, символы, размер, дизассемблирование и заголовки.
    bool linker_map = false;      //!< Карта компоновки создаётся на этапе LINKING в gcc_t.
    bool generate_bin = false;    //!< Плоский образ через objcopy -O binary.
    bool generate_hex = false;    //!< Intel HEX через objcopy -O ihex.
    std::string toolchain_prefix; //!< Например, arm-none-eabi- для GNU Binutils.
    std::uint64_t flash_origin = 0; //!< Адрес начала Flash; 0 допустим.
    std::uint64_t flash_size = 0;   //!< Размер Flash в байтах; 0 - неизвестен.
    std::uint64_t ram_origin = 0;   //!< Адрес начала RAM; 0 допустим.
    std::uint64_t ram_size = 0;     //!< Размер RAM в байтах; 0 - неизвестен.
  };

  using executor_t = std::function<int(const std::string&)>;

private:
  static std::string quote(const std::filesystem::path& path) {
    const auto value = path.string();
    if (value.empty() || value.find('"') != std::string::npos ||
        value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
      throw std::invalid_argument("UNSUPPORTED CHARACTERS IN BINARY COMMAND");
    }
    return "\"" + value + "\"";
  }

  static int normalize_exit_code(int status) {
#ifdef _WIN32
    return status;
#else
    if (status == -1) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
  }

  static int execute(const std::string& cmd, const executor_t& executor) {
    return executor ? executor(cmd) : process_t::run(cmd);
  }

  static std::string tool(const settings_t& settings, const char* name) {
    return quote(settings.toolchain_prefix + name);
  }

  static bool valid_prefix(const std::string& prefix) {
    for (unsigned char c : prefix) {
      if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != '/' && c != '\\' && c != ':') {
        return false;
      }
    }
    return true;
  }

  // Чтение сигнатуры сделано совместимым с кэшем этапов GCC.
  static bool cache_matches(const std::filesystem::path& file, const std::string& value) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return false;
    const std::string saved((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return !input.bad() && saved == value;
  }

  static bool save_cache(const std::filesystem::path& file, const std::string& value) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << value;
    return output.good();
  }

  // Отчёты должны существовать все, а не только последний созданный файл.
  static bool reports_current(const std::filesystem::path& directory) {
    std::ifstream manifest(directory / "manifest.txt");
    std::string version, relative_name;
    if (!manifest || !std::getline(manifest, version) || version != "PGR-BINARY-V1") return false;
    std::size_t count = 0;
    while (std::getline(manifest, relative_name)) {
      const std::filesystem::path relative(relative_name);
      if (relative.empty() || relative.is_absolute()) return false;
      for (const auto& part : relative) if (part == "..") return false;
      if (!std::filesystem::is_regular_file(directory / relative)) return false;
      ++count;
    }
    return !manifest.bad() && count != 0;
  }

  static std::vector<std::string> expected_outputs(
    const std::filesystem::path& executable,
    format_t format,
    const settings_t& settings
  ) {
    std::vector<std::string> outputs = {"format.txt"};
    if (settings.reports) {
      for (const char* name : {"file-headers.txt", "sections.txt", "symbols.txt",
                               "disassembly.txt", "size.txt", "size-summary.txt", "memory.txt"}) {
        outputs.emplace_back(name);
      }
      outputs.push_back(format == format_t::ELF ? "program-headers.txt" : "pe-headers.txt");
    }
    if (settings.generate_bin) outputs.push_back(executable.stem().string() + ".bin");
    if (settings.generate_hex) outputs.push_back(executable.stem().string() + ".hex");
    return outputs;
  }

  // Вывод внешней команды сначала записываем в файл: дизассемблирование бывает огромным.
  // Ошибки не скрываются, первые строки диагностики повторяются в терминале и журнале.
  static int make_report(
    const std::string& command,
    const std::filesystem::path& report,
    const executor_t& executor
  ) {
    const std::string wrapped = "(" + command + " > " + quote(report) + " 2>&1)";
    std::cout << "[BINARY CMD] " << command << '\n';
    const int result = execute(wrapped, executor);
    if (result != 0 || !std::filesystem::is_regular_file(report)) {
      std::cerr << "[BINARY ERROR] Tool failed (" << result << "): " << report << '\n';
      std::ifstream input(report, std::ios::binary);
      std::string line;
      for (int index = 0; index < 20 && std::getline(input, line); ++index) std::cerr << line << '\n';
      return result == 0 ? 1 : result;
    }
    std::cout << "[BINARY REPORT] " << report << '\n';
    return 0;
  }

  static bool remove_old(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) return !error;
    if (std::filesystem::is_symlink(directory, error)) {
      std::cerr << "[BINARY ERROR] Refusing symlink: " << directory << '\n';
      return false;
    }
    if (error) return false;
    std::filesystem::remove_all(directory, error);
    if (error) std::cerr << "[BINARY ERROR] Cannot clean reports: " << error.message() << '\n';
    return !error;
  }


  // Вычисление пересечения секции с областью памяти по адресам VMA/LMA.
  static std::uint64_t overlap(std::uint64_t address, std::uint64_t length,
                               std::uint64_t origin, std::uint64_t capacity) {
    if (capacity == 0 || length == 0) return 0;
    const auto start = std::max(address, origin);
    const auto end = std::min(address + length, origin + capacity);
    return end > start ? end - start : 0;
  }

  // Отчёт не заменяет linker script: все выводы опираются на реальные адреса
  // секций из objdump -h и введённую пользователем карту FLASH/RAM.
  static bool make_memory_report(const std::filesystem::path& sections_file,
                                 const std::filesystem::path& target,
                                 const settings_t& settings) {
    std::ifstream sections(sections_file);
    std::ofstream report(target, std::ios::trunc);
    if (!sections || !report) return false;
    report << "PGR MEMORY REPORT\n"
           << "Source: GNU objdump -h (section headers, VMA/LMA)\n"
           << "Flash origin: 0x" << std::hex << settings.flash_origin << std::dec
           << " | size: " << settings.flash_size << " bytes\n"
           << "RAM origin:   0x" << std::hex << settings.ram_origin << std::dec
           << " | size: " << settings.ram_size << " bytes\n\n";
    std::uint64_t flash_used = 0, ram_used = 0;
    bool overflow = false;
    std::size_t recognized = 0;
    std::string line;
    while (std::getline(sections, line)) {
      std::istringstream words(line);
      unsigned int index = 0;
      std::string name, size_text, vma_text, lma_text;
      if (!(words >> index >> name >> size_text >> vma_text >> lma_text)) continue;
      try {
        const auto length = std::stoull(size_text, nullptr, 16);
        const auto vma = std::stoull(vma_text, nullptr, 16);
        const auto lma = std::stoull(lma_text, nullptr, 16);
        std::string flags;
        std::getline(sections, flags);
        const bool allocated = flags.find("ALLOC") != std::string::npos;
        const bool loaded = flags.find("LOAD") != std::string::npos;
        if (!allocated || length == 0) continue;
        ++recognized;
        report << name << ": " << length << " bytes, VMA=0x" << std::hex << vma
               << ", LMA=0x" << lma << std::dec
               << (loaded ? ", LOAD" : ", NO-LOAD") << '\n';
        if (settings.flash_size && loaded) {
          const auto bytes = overlap(lma, length, settings.flash_origin, settings.flash_size);
          flash_used += bytes;
          if (bytes && bytes < length) {
            report << "  [OVERFLOW] Section crosses Flash boundary\n";
            overflow = true;
          }
        }
        if (settings.ram_size) {
          const auto bytes = overlap(vma, length, settings.ram_origin, settings.ram_size);
          ram_used += bytes;
          if (bytes && bytes < length) {
            report << "  [OVERFLOW] Section crosses RAM boundary\n";
            overflow = true;
          }
        }
      } catch (const std::invalid_argument&) {
        continue;
      } catch (const std::out_of_range&) {
        continue;
      }
    }
    report << "\nRecognized allocated sections: " << recognized << '\n';
    if (settings.flash_size)
      report << "Flash allocated sections: " << flash_used << " / " << settings.flash_size
             << " bytes\n";
    else report << "Flash allocation unknown: configure Flash origin and size.\n";
    if (settings.ram_size)
      report << "RAM allocated sections: " << ram_used << " / " << settings.ram_size
             << " bytes\n";
    else report << "RAM allocation unknown: configure RAM origin and size.\n";
    if (!settings.flash_size || !settings.ram_size)
      report << "Warning: unknown regions are not checked.\n";
    report << "Note: RAM stack/heap reserves and bootloader regions need separate validation.\n"
           << "Note: VMA is runtime address; LMA is load address.\n"
           << "Status: " << (overflow ? "OVERFLOW" : "NO DETECTED REGION OVERFLOW") << '\n';
    return report.good() && recognized > 0 && !overflow;
  }

public:
  static void validate_settings(const settings_t& settings) {
    if (!valid_prefix(settings.toolchain_prefix)) {
      throw std::invalid_argument("INVALID GNU TOOLCHAIN PREFIX");
    }
    if ((settings.flash_size && settings.flash_origin > UINT64_MAX - settings.flash_size) ||
        (settings.ram_size && settings.ram_origin > UINT64_MAX - settings.ram_size))
      throw std::invalid_argument("INVALID FLASH/RAM ADDRESS RANGE");
    if (settings.flash_size && settings.ram_size &&
        overlap(settings.flash_origin, settings.flash_size,
                settings.ram_origin, settings.ram_size))
      throw std::invalid_argument("FLASH AND RAM REGIONS OVERLAP");
  }

  static bool active(const settings_t& settings) noexcept {
    return settings.reports || settings.generate_bin || settings.generate_hex;
  }

  // ELF определяется по magic 0x7F 'E' 'L' 'F'; PE - по MZ и PE\0\0 по e_lfanew.
  // Не полагаемся на расширение .exe/.elf: оно не определяет реальный формат файла.
  static format_t detect_format(const std::filesystem::path& executable) {
    std::ifstream file(executable, std::ios::binary);
    if (!file) return format_t::UNKNOWN;
    char magic[4]{};
    if (!file.read(magic, 4)) return format_t::UNKNOWN;
    if (static_cast<unsigned char>(magic[0]) == 0x7F &&
        magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') return format_t::ELF;
    if (magic[0] != 'M' || magic[1] != 'Z') return format_t::UNKNOWN;
    file.seekg(0x3C);
    unsigned char offset[4]{};
    if (!file.read(reinterpret_cast<char*>(offset), 4)) return format_t::UNKNOWN;
    const std::uint32_t header = static_cast<std::uint32_t>(offset[0]) |
      (static_cast<std::uint32_t>(offset[1]) << 8) |
      (static_cast<std::uint32_t>(offset[2]) << 16) |
      (static_cast<std::uint32_t>(offset[3]) << 24);
    std::error_code error;
    const auto length = std::filesystem::file_size(executable, error);
    if (error || header > length || length - header < 4) return format_t::UNKNOWN;
    file.seekg(header);
    return file.read(magic, 4) && magic[0] == 'P' && magic[1] == 'E' &&
           magic[2] == '\0' && magic[3] == '\0' ? format_t::PE : format_t::UNKNOWN;
  }

  static const char* format_name(format_t format) {
    switch (format) {
      case format_t::ELF: return "ELF";
      case format_t::PE: return "PE/COFF";
      default: return "UNKNOWN";
    }
  }

  // Отдельный этап ПОСЛЕ LINKING. При изменении только настроек анализа
  // gcc_t пропускает компиляцию и компоновку, но обновляет отчёты Binutils.
  int run(
    const std::filesystem::path& executable,
    const std::filesystem::path& report_dir,
    const std::filesystem::path& cache_file,
    const settings_t& settings,
    const executor_t& executor = {}
  ) const {
    validate_settings(settings);
    if (!active(settings)) {
      if (std::filesystem::exists(report_dir) && !remove_old(report_dir)) return 1;
      std::error_code ignored;
      std::filesystem::remove(cache_file, ignored);
      return 0;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error) || error) {
      std::cerr << "[BINARY ERROR] Executable not found: " << executable << '\n';
      return 1;
    }
    const format_t format = detect_format(executable);
    if (format == format_t::UNKNOWN) {
      std::cerr << "[BINARY ERROR] Unsupported executable format (expected ELF or PE/COFF): "
                << executable << '\n';
      return 1;
    }

    const auto outputs = expected_outputs(executable, format, settings);
    const auto last_modified = std::filesystem::last_write_time(executable, error);
    if (error) return 1;
    const auto length = std::filesystem::file_size(executable, error);
    if (error) return 1;
    std::string tool_signature;
    if (settings.reports) {
      for (const char* program : {"objdump", "nm", "size"})
        tool_signature += cache_t::tool_fingerprint(settings.toolchain_prefix + program) + "\n";
      tool_signature += cache_t::tool_fingerprint(settings.toolchain_prefix +
        std::string(format == format_t::ELF ? "readelf" : "objdump")) + "\n";
    }
    if (settings.generate_bin || settings.generate_hex)
      tool_signature += cache_t::tool_fingerprint(settings.toolchain_prefix + "objcopy") + "\n";
    const std::string signature = "PGR-BINARY-CACHE-V2\n" + executable.string() + "\n" +
      tool_signature +
      std::to_string(length) + "\n" + std::to_string(last_modified.time_since_epoch().count()) + "\n" +
      settings.toolchain_prefix + "\n" +
      std::to_string(settings.flash_origin) + ":" + std::to_string(settings.flash_size) + ":" +
      std::to_string(settings.ram_origin) + ":" + std::to_string(settings.ram_size) + "\n" +
      (settings.reports ? "1" : "0") +
      (settings.generate_bin ? "1" : "0") + (settings.generate_hex ? "1" : "0") + "\n";
    if (cache_matches(cache_file, signature) && reports_current(report_dir)) {
      bool complete = true;
      for (const auto& name : outputs) {
        if (!std::filesystem::is_regular_file(report_dir / name)) complete = false;
      }
      if (complete) {
        std::cout << "[BINARY] UP-TO-DATE -> " << report_dir << '\n';
        return 0;
      }
    }

    // Любая ошибка инструмента оставляет кэш недействительным.
    if (!remove_old(report_dir)) return 1;
    std::filesystem::create_directories(report_dir);
    std::filesystem::remove(cache_file, error);
    std::ofstream info(report_dir / "format.txt");
    if (!info) return 1;
    info << "Format: " << format_name(format) << "\nInput: " << executable << "\n"
         << "File size: " << length << " bytes\n"
         << "Note: size -A / size -B are section statistics, not a proven physical RAM/FLASH budget.\n";
    info.close();
    if (!info) return 1;

    const auto run_text = [&](const char* program, const std::string& flags, const char* name) {
      const std::string command = tool(settings, program) + " " + flags + " " + quote(executable);
      return make_report(command, report_dir / name, executor);
    };
    if (settings.reports) {
      if (int rc = run_text("objdump", "-f", "file-headers.txt")) return rc;
      if (int rc = run_text("objdump", "-h", "sections.txt")) return rc;
      if (!make_memory_report(report_dir / "sections.txt", report_dir / "memory.txt", settings)) {
        std::cerr << "[BINARY ERROR] Memory report parsing failed or section crosses a memory region.\n";
        return 1;
      }
      if (int rc = run_text("nm", "-n -C", "symbols.txt")) return rc;
      if (int rc = run_text("objdump", "-d -C", "disassembly.txt")) return rc;
      if (int rc = run_text("size", "-A", "size.txt")) return rc;
      if (int rc = run_text("size", "-B", "size-summary.txt")) return rc;
      if (format == format_t::ELF) {
        if (int rc = run_text("readelf", "-l", "program-headers.txt")) return rc;
      } else {
        if (int rc = run_text("objdump", "-p", "pe-headers.txt")) return rc;
      }
    }
    for (const auto& item : std::vector<std::pair<bool, std::string>>{
           {settings.generate_bin, ".bin"}, {settings.generate_hex, ".hex"}}) {
      if (!item.first) continue;
      const auto output = report_dir / (executable.stem().string() + item.second);
      const std::string format_name_arg = item.second == ".bin" ? "binary" : "ihex";
      const std::string command = tool(settings, "objcopy") + " -O " + format_name_arg +
        " " + quote(executable) + " " + quote(output);
      std::cout << "[BINARY CMD] " << command << '\n';
      const int result = execute(command, executor);
      if (result != 0 || !std::filesystem::is_regular_file(output)) {
        std::cerr << "[BINARY ERROR] Image conversion failed: " << output << '\n';
        return result == 0 ? 1 : result;
      }
      std::cout << "[BINARY IMAGE] " << output << '\n';
    }

    std::ofstream manifest(report_dir / "manifest.txt", std::ios::trunc);
    if (!manifest) return 1;
    manifest << "PGR-BINARY-V1\n";
    for (const auto& name : outputs) manifest << name << '\n';
    manifest.close();
    if (!manifest || !save_cache(cache_file, signature)) return 1;
    std::cout << "[BINARY] SUCCESS -> " << report_dir << '\n';
    return 0;
  }
};

#endif // BINARY_HPP
