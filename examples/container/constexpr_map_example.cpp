import std;
import container;
using namespace std::string_view_literals;

int main() {
  std::println("=== ConstexprMap Examples ===");

  constexpr auto signals = make_constexpr_map<std::string_view, int>(
      std::array<std::pair<std::string_view, int>, 4>{
          {{"KILL"sv, 9}, {"TERM"sv, 15}, {"INT"sv, 2}, {"HUP"sv, 1}}});

  std::println("Signal KILL = {}", signals["KILL"sv]);
  std::println("Signal TERM = {}", signals.get_or("TERM"sv, -1));
  std::println("Signal UNKNOWN = {}", signals.get_or("UNKNOWN"sv, -1));

  std::println("\nAll signals:");
  for (const auto& [name, num] : signals) {
    std::println("  {} -> {}", name, num);
  }

  constexpr auto errors = make_constexpr_map<int, std::string_view>(
      std::array<std::pair<int, std::string_view>, 3>{
          {{0, "Success"sv}, {2, "Not found"sv}, {13, "Permission denied"sv}}});

  std::println("\nError 0: {}", errors.get_or(0, "Unknown"sv));
  std::println("Error 2: {}", errors.get_or(2, "Unknown"sv));
  std::println("Error 99: {}", errors.get_or(99, "Unknown"sv));

  return 0;
}
