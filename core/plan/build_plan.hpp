#ifndef BUILD_PLAN_HPP
#define BUILD_PLAN_HPP

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// План действий компилятора, не зависящий от GTK4 или командной оболочки.
// Статус PLAN означает предполагаемый запуск, SKIP — действительный cache hit.
class build_plan_t {
public:
  struct operation_t {
    std::string stage;
    std::string decision;
    std::string reason;
    std::filesystem::path source;
    std::filesystem::path target;
    std::string command;
  };

private:
  std::vector<operation_t> operations;

public:
  void add(const std::string& stage, const std::string& decision,
           const std::string& reason, const std::filesystem::path& source,
           const std::filesystem::path& target, const std::string& command) {
    operations.push_back({stage, decision, reason, source, target, command});
  }

  const std::vector<operation_t>& items() const noexcept { return operations; }

  // Печать плана не запускает GCC и не создаёт файлов сборки.
  void print(std::ostream& stream = std::cout) const {
    stream << "[BUILD PLAN] " << operations.size() << " operation(s)\n";
    for (const auto& operation : operations) {
      stream << "[" << operation.decision << "] " << operation.stage
             << " -> " << operation.target << "\n"
             << "  Reason: " << operation.reason << '\n';
      if (!operation.command.empty() && operation.decision == "PLAN")
        stream << "  Command: " << operation.command << '\n';
    }
  }
};

#endif // BUILD_PLAN_HPP
