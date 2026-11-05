#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Прямой запуск внешней программы без cmd.exe или /bin/sh.
// Строковый интерфейс сохраняет совместимость со старыми модулями GCC/Binutils.
// Поддерживаются только перенаправления >, 2> и 2>&1; команды оболочки запрещены.
class process_t {
public:
  struct command_t {
    std::vector<std::string> arguments;
    std::filesystem::path stdout_file;
    std::filesystem::path stderr_file;
    bool merge_stderr = false;
  };

  // Лексический разбор двойных кавычек, не интерпретирующий символы оболочки.
  static command_t parse(std::string command) {
    if (command.size() >= 2 && command.front() == '(' && command.back() == ')') {
      command = command.substr(1, command.size() - 2);
    }
    std::vector<std::string> tokens;
    std::string word;
    bool quoted = false, active = false;
    for (std::size_t i = 0; i < command.size(); ++i) {
      const char c = command[i];
      if (c == '\n' || c == '\r' || c == '\0') throw std::invalid_argument("INVALID PROCESS COMMAND");
      if (c == '"') {
        quoted = !quoted;
        active = true;
      } else if (!quoted && (c == ' ' || c == '\t')) {
        if (active) { tokens.push_back(word); word.clear(); active = false; }
      } else {
        word += c;
        active = true;
      }
    }
    if (quoted) throw std::invalid_argument("UNTERMINATED PROCESS QUOTE");
    if (active) tokens.push_back(word);
    command_t parsed;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
      const auto& token = tokens[i];
      if (token == ">" || token == "1>" || token == "2>") {
        if (++i >= tokens.size() || tokens[i].empty()) throw std::invalid_argument("REDIRECTION FILE MISSING");
        if (token == "2>") parsed.stderr_file = tokens[i];
        else parsed.stdout_file = tokens[i];
      } else if (token == "2>&1") {
        parsed.merge_stderr = true;
      } else {
        // Синтаксис shell не должен случайно запускать конвейеры или несколько команд.
        if (token == ";" || token == "&&" || token == "||" || token == "|" ||
            token == "<" || token == "&" || token == ">>" || token == "$(") {
          throw std::invalid_argument("SHELL OPERATORS ARE NOT SUPPORTED");
        }
        parsed.arguments.push_back(token);
      }
    }
    if (parsed.arguments.empty() || parsed.arguments.front().empty())
      throw std::invalid_argument("PROCESS PROGRAM IS EMPTY");
    if (parsed.merge_stderr && !parsed.stderr_file.empty())
      throw std::invalid_argument("CONFLICTING STDERR REDIRECTION");
    return parsed;
  }

  // Внешний исполнитель получает аргументы без повторного разбора оболочкой.
  static int run(const command_t& command) {
    if (command.arguments.empty()) return 1;
    for (const auto& path : {command.stdout_file, command.stderr_file}) {
      if (!path.empty() && !path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    }
#ifdef _WIN32
    return run_windows(command);
#else
    return run_posix(command);
#endif
  }

  static int run(const std::string& command) {
    return run(parse(command));
  }

private:
#ifndef _WIN32
  static int run_posix(const command_t& command) {
    int output[2];
    if (pipe(output) != 0) { std::perror("pipe"); return 1; }
    const pid_t pid = fork();
    if (pid < 0) {
      close(output[0]); close(output[1]); std::perror("fork"); return 1;
    }
    if (pid == 0) {
      close(output[0]);
      if (command.stdout_file.empty()) {
        if (dup2(output[1], STDOUT_FILENO) < 0) _exit(127);
      } else {
        const int fd = open(command.stdout_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) _exit(127);
        close(fd);
      }
      if (!command.stderr_file.empty()) {
        const int fd = open(command.stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(127);
        close(fd);
      } else if (command.merge_stderr) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) _exit(127);
      } else if (dup2(output[1], STDERR_FILENO) < 0) _exit(127);
      close(output[1]);
      std::vector<char*> argv;
      for (const auto& arg : command.arguments) argv.push_back(const_cast<char*>(arg.c_str()));
      argv.push_back(nullptr);
      execvp(argv[0], argv.data());
      std::fprintf(stderr, "Cannot execute: %s\n", argv[0]);
      _exit(127);
    }
    close(output[1]);
    char buffer[8192];
    for (;;) {
      const ssize_t count = read(output[0], buffer, sizeof(buffer));
      if (count > 0) std::cout.write(buffer, count).flush();
      else if (count == 0) break;
      else if (errno != EINTR) break;
    }
    close(output[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
  }
#else
  static std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    UINT codepage = CP_UTF8;
    int len = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, nullptr, 0);
    if (!len) {
      codepage = CP_ACP;
      len = MultiByteToWideChar(codepage, 0, utf8.c_str(), -1, nullptr, 0);
    }
    if (!len) throw std::runtime_error("PROCESS INVALID PATH ENCODING");
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(codepage, 0, utf8.c_str(), -1, wide.data(), len);
    wide.pop_back();
    return wide;
  }

  // Правила экранирования аргументов CreateProcessW / CommandLineToArgvW.
  static std::wstring quote_arg(const std::wstring& arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : arg) {
      if (c == L'\\') { ++slashes; continue; }
      if (c == L'"') result.append(slashes * 2 + 1, L'\\');
      else result.append(slashes, L'\\');
      slashes = 0;
      result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
  }

  static int run_windows(const command_t& command) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return 1;
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
      CloseHandle(read_pipe); CloseHandle(write_pipe); return 1;
    }
    HANDLE stdout_handle = write_pipe, stderr_handle = write_pipe;
    HANDLE stdout_file = INVALID_HANDLE_VALUE, stderr_file = INVALID_HANDLE_VALUE;
    if (!command.stdout_file.empty()) {
      stdout_file = CreateFileW(command.stdout_file.wstring().c_str(), GENERIC_WRITE,
          FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      stdout_handle = stdout_file;
    }
    if (!command.stderr_file.empty()) {
      stderr_file = CreateFileW(command.stderr_file.wstring().c_str(), GENERIC_WRITE,
          FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      stderr_handle = stderr_file;
    } else if (command.merge_stderr) stderr_handle = stdout_handle;
    if (stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE) {
      CloseHandle(read_pipe); CloseHandle(write_pipe);
      if (stdout_file != INVALID_HANDLE_VALUE) CloseHandle(stdout_file);
      if (stderr_file != INVALID_HANDLE_VALUE) CloseHandle(stderr_file);
      return 1;
    }
    std::wstring command_line;
    for (const auto& arg : command.arguments) {
      if (!command_line.empty()) command_line += L' ';
      command_line += quote_arg(to_wide(arg));
    }
    std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
    mutable_line.push_back(L'\0');
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = stdout_handle;
    start.hStdError = stderr_handle;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr,
        TRUE, 0, nullptr, nullptr, &start, &info);
    CloseHandle(write_pipe);
    if (stdout_file != INVALID_HANDLE_VALUE) CloseHandle(stdout_file);
    if (stderr_file != INVALID_HANDLE_VALUE) CloseHandle(stderr_file);
    if (!created) {
      std::cerr << "[PROCESS ERROR] CreateProcessW: " << GetLastError() << '\n';
      CloseHandle(read_pipe);
      return 1;
    }
    char data[8192];
    DWORD count = 0;
    while (ReadFile(read_pipe, data, sizeof(data), &count, nullptr) && count)
      std::cout.write(data, count).flush();
    CloseHandle(read_pipe);
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(info.hProcess, &result);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return static_cast<int>(result);
  }
#endif
};

#endif // PROCESS_HPP
