/**
 * @file small_vector_example.cpp
 * @brief Example usage of SmallVector container
 *
 * This example demonstrates all features of SmallVector:
 * - Basic operations (push_back, emplace_back, pop_back)
 * - Small buffer optimization
 * - Iterators and range-based for loops
 * - Construction methods
 * - Copy and move semantics
 * - Element access
 * - Appending and inserting
 * - Memory characteristics
 *
 * @author WinuxCmd Project
 * @copyright Copyright © 2026 WinuxCmd
 */

import std;
import container;

// A non-trivially copyable type to demonstrate the difference
class Person {
 private:
  std::string name_;
  int age_;

 public:
  Person(const char* name, int age) : name_(name), age_(age) {
    std::println("  [ctor] Person({}, {})", name_, age_);
  }

  ~Person() { std::println("  [dtor] Person({}, {})", name_, age_); }

  Person(const Person& other) : name_(other.name_), age_(other.age_) {
    std::println("  [copy] Person({}, {})", name_, age_);
  }

  Person(Person&& other) noexcept
      : name_(std::move(other.name_)), age_(other.age_) {
    std::println("  [move] Person({}, {})", name_, age_);
  }

  Person& operator=(const Person& other) {
    if (this != &other) {
      name_ = other.name_;
      age_ = other.age_;
      std::println("  [copy assign] Person({}, {})", name_, age_);
    }
    return *this;
  }

  Person& operator=(Person&& other) noexcept {
    if (this != &other) {
      name_ = std::move(other.name_);
      age_ = other.age_;
      std::println("  [move assign] Person({}, {})", name_, age_);
    }
    return *this;
  }

  std::string_view name() const { return name_; }
  int age() const { return age_; }

  bool operator==(const Person& other) const {
    return name_ == other.name_ && age_ == other.age_;
  }
};

// A simple struct to demonstrate trivially copyable types
struct Point {
  int x;
  int y;

  bool operator==(const Point& other) const = default;
};

// Helper function to print vector state
template <typename T, unsigned N>
void print_vector_state(const SmallVector<T, N>& vec, const char* name) {
  std::print("  {}: size={}, capacity={}, ", name, vec.size(), vec.capacity());
  if (vec.capacity() <= N) {
    std::print("inline storage ({} bytes)", N * sizeof(T));
  } else {
    std::print("heap storage ({} bytes)", vec.capacity() * sizeof(T));
  }
  std::println("");
}

// Helper to print vector contents
template <typename T, unsigned N>
void print_vector(const SmallVector<T, N>& vec, const char* name) {
  std::print("  {} = [", name);
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i > 0) std::print(", ");
    std::print("{}", vec[i]);
  }
  std::println("]");
}

// Specialization for Person
template <unsigned N>
void print_vector(const SmallVector<Person, N>& vec, const char* name) {
  std::println("  {} contains:", name);
  for (const auto& p : vec) {
    std::println("    - {} ({})", p.name(), p.age());
  }
}

int main() {
  std::println("========================================");
  std::println("SmallVector Example - Windows/MSVC Version");
  std::println("========================================");

  // ========== 1. Basic Usage ==========
  std::println("\n--- 1. Basic Usage with int (trivially copyable) ---");

  SmallVector<int, 4> numbers;
  print_vector_state(numbers, "numbers");

  std::println("  Adding 3 elements (stays inline):");
  numbers.push_back(10);
  numbers.push_back(20);
  numbers.emplace_back(30);
  print_vector(numbers, "numbers");
  print_vector_state(numbers, "numbers");

  std::println("  Adding 2 more elements (exceeds inline capacity):");
  numbers.push_back(40);
  numbers.push_back(50);
  print_vector(numbers, "numbers");
  print_vector_state(numbers, "numbers");

  std::println("  Access and modification:");
  std::println("    front = {}", numbers.front());
  std::println("    back = {}", numbers.back());
  std::println("    [2] = {}", numbers[2]);
  numbers[2] = 99;
  print_vector(numbers, "after modification");

  // ========== 2. Range-based for loop ==========
  std::println("\n--- 2. Range-based for loop ---");
  std::print("  All numbers: ");
  for (int n : numbers) {
    std::print("{} ", n);
  }
  std::println("");

  // ========== 3. Construction Methods ==========
  std::println("\n--- 3. Various Construction Methods ---");

  std::println("  From initializer list (size 3, capacity 3):");
  SmallVector<int, 3> vec1 = {1, 2, 3};
  print_vector(vec1, "vec1");
  print_vector_state(vec1, "vec1");

  std::println("  With size and default value:");
  SmallVector<std::string, 2> vec2(3, "hello");
  print_vector(vec2, "vec2");
  print_vector_state(vec2, "vec2");

  std::println("  From iterator range:");
  std::vector<double> src = {1.1, 2.2, 3.3, 4.4};
  SmallVector<double, 2> vec3(src.begin(), src.end());
  print_vector(vec3, "vec3");
  print_vector_state(vec3, "vec3");

  // ========== 4. Non-trivially Copyable Types ==========
  std::println("\n--- 4. Non-trivially Copyable Types (Person) ---");

  std::println("  Creating persons with inline storage (capacity=2):");
  SmallVector<Person, 2> persons;
  print_vector_state(persons, "persons");

  std::println("\n  Adding first person (inline):");
  persons.emplace_back("Alice", 25);
  print_vector_state(persons, "persons");

  std::println("\n  Adding second person (inline):");
  persons.emplace_back("Bob", 30);
  print_vector_state(persons, "persons");

  std::println("\n  Adding third person (triggers heap allocation + moves):");
  persons.emplace_back("Charlie", 35);
  print_vector_state(persons, "persons");

  std::println("\n  Final persons:");
  print_vector(persons, "persons");

  // ========== 5. Copy and Move Semantics ==========
  std::println("\n--- 5. Copy and Move Semantics ---");

  SmallVector<int, 3> original = {1, 2, 3, 4, 5};
  std::println("  Original:");
  print_vector(original, "original");
  print_vector_state(original, "original");

  std::println("\n  Copy construction:");
  auto copy = original;
  print_vector(copy, "copy");
  print_vector_state(copy, "copy");

  std::println("\n  Move construction:");
  auto moved = std::move(original);
  print_vector(moved, "moved");
  print_vector_state(moved, "moved");
  std::println("  Original after move: size={}", original.size());

  // ========== 6. Appending Operations ==========
  std::println("\n--- 6. Appending Operations ---");

  SmallVector<int, 3> vec4 = {1, 2, 3};
  std::println("  Initial:");
  print_vector(vec4, "vec4");
  print_vector_state(vec4, "vec4");

  std::println("\n  append(4,5):");
  vec4.append({4, 5});
  print_vector(vec4, "vec4");
  print_vector_state(vec4, "vec4");

  std::println("\n  append 3 copies of 9:");
  vec4.append(3, 9);
  print_vector(vec4, "vec4");
  print_vector_state(vec4, "vec4");

  // ========== 7. Element Operations ==========
  std::println("\n--- 7. Element Operations ---");

  SmallVector<int, 3> vec5 = {10, 20, 30, 40, 50};
  std::println("  Initial:");
  print_vector(vec5, "vec5");

  std::println("  pop_back (removes last):");
  vec5.pop_back();
  print_vector(vec5, "vec5");

  std::println("  clear():");
  vec5.clear();
  print_vector(vec5, "vec5");
  print_vector_state(vec5, "vec5");

  std::println("  reuse after clear:");
  vec5.push_back(100);
  vec5.push_back(200);
  print_vector(vec5, "vec5");

  // ========== 8. Reserve and Resize ==========
  std::println("\n--- 8. Reserve and Resize ---");

  SmallVector<int, 3> vec6;
  std::println("  Initial:");
  print_vector_state(vec6, "vec6");

  std::println("  reserve(10):");
  vec6.reserve(10);
  print_vector_state(vec6, "vec6");

  std::println("  resize(5):");
  vec6.resize(5);
  print_vector(vec6, "vec6");
  print_vector_state(vec6, "vec6");

  std::println("  resize(2) (shrink):");
  vec6.resize(2);
  print_vector(vec6, "vec6");

  // ========== 9. Comparison Operators ==========
  std::println("\n--- 9. Comparison Operators ---");

  SmallVector<int, 3> a = {1, 2, 3};
  SmallVector<int, 3> b = {1, 2, 3};
  SmallVector<int, 3> c = {1, 2, 4};

  std::println("  a == b: {}", (a == b));
  std::println("  a == c: {}", (a == c));
  std::println("  a != c: {}", (a != c));

  // ========== 10. Memory Footprint ==========
  std::println("\n--- 10. Memory Footprint ---");

  std::println("  sizeof(SmallVector<int, 0>)  = {} bytes",
               sizeof(SmallVector<int, 0>));
  std::println("  sizeof(SmallVector<int, 4>)  = {} bytes",
               sizeof(SmallVector<int, 4>));
  std::println("  sizeof(SmallVector<int, 8>)  = {} bytes",
               sizeof(SmallVector<int, 8>));
  std::println("  sizeof(SmallVector<int, 16>) = {} bytes",
               sizeof(SmallVector<int, 16>));
  std::println("  sizeof(SmallVector<Point, 4>) = {} bytes",
               sizeof(SmallVector<Point, 4>));
  std::println("  sizeof(SmallVector<Person, 4>) = {} bytes",
               sizeof(SmallVector<Person, 4>));

  // ========== 11. Edge Cases ==========
  std::println("\n--- 11. Edge Cases ---");

  std::println("  Empty vector:");
  SmallVector<int, 3> empty;
  std::println("    empty.size() = {}", empty.size());
  std::println("    empty.empty() = {}", empty.empty());

  std::println("  Single element:");
  empty.push_back(42);
  std::println("    front = {}, back = {}", empty.front(), empty.back());

  std::println("\n  Large N (128 inline elements):");
  SmallVector<int, 128> large;
  std::println("    capacity = {}, inline storage = {} bytes", large.capacity(),
               128 * sizeof(int));

  // ========== 12. Real-world Usage Pattern ==========
  std::println("\n--- 12. Real-world Usage: Command Line Arguments ---");

  // Simulate parsing command line arguments
  const char* argv[] = {"program.exe", "-v", "--file", "test.txt", "-n", "42"};
  int argc = 6;

  SmallVector<std::string_view, 8> args;
  for (int i = 0; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  std::println("  Parsed {} arguments:", args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    std::println("    args[{}] = '{}'", i, args[i]);
  }

  // ========== 13. Nested Containers ==========
  std::println("\n--- 13. Nested Containers ---");

  SmallVector<SmallVector<int, 3>, 4> matrix;

  matrix.emplace_back(std::initializer_list<int>{1, 2, 3});
  matrix.emplace_back(std::initializer_list<int>{4, 5, 6});
  matrix.emplace_back(std::initializer_list<int>{7, 8, 9});

  std::println("  Matrix (3x3):");
  for (const auto& row : matrix) {
    std::print("    [");
    for (int val : row) {
      std::print(" {}", val);
    }
    std::println(" ]");
  }

  std::println("\n========================================");
  std::println("Example completed successfully!");
  std::println("========================================");

  return 0;
}
