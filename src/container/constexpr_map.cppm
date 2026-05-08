export module container:constexpr_map;

import std;

export template <typename Key, typename Value, std::size_t N>
class ConstexprMap {
 private:
  struct Pair {
    Key first;
    Value second;
  };
  std::array<Pair, N> data_;

  // constexpr insert sort
  constexpr void sort_data() noexcept {
    for (std::size_t i = 1; i < N; ++i) {
      Pair tmp = data_[i];
      std::size_t j = i;
      while (j > 0 && data_[j - 1].first > tmp.first) {
        data_[j] = data_[j - 1];
        --j;
      }
      data_[j] = tmp;
    }
  }

 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<const Key, Value>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = const value_type&;
  using const_reference = const value_type&;
  using pointer = const value_type*;
  using const_pointer = const value_type*;

  // Iterator
  class const_iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<const Key, Value>;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = const value_type&;

    constexpr const_iterator() noexcept : ptr_(nullptr) {}
    constexpr explicit const_iterator(const value_type* ptr) noexcept
        : ptr_(ptr) {}

    constexpr reference operator*() const noexcept { return *ptr_; }
    constexpr pointer operator->() const noexcept { return ptr_; }

    constexpr const_iterator& operator++() noexcept {
      ++ptr_;
      return *this;
    }
    constexpr const_iterator operator++(int) noexcept {
      auto tmp = *this;
      ++ptr_;
      return tmp;
    }

    constexpr bool operator==(const const_iterator& other) const noexcept {
      return ptr_ == other.ptr_;
    }
    constexpr bool operator!=(const const_iterator& other) const noexcept {
      return ptr_ != other.ptr_;
    }

   private:
    const value_type* ptr_;
  };
  using iterator = const_iterator;

  // Construct from std::array of std::pair (compile-time)
  constexpr explicit ConstexprMap(
      const std::array<std::pair<Key, Value>, N>& arr) {
    for (std::size_t i = 0; i < N; ++i) {
      data_[i].first = arr[i].first;
      data_[i].second = arr[i].second;
    }
    // Sort,then we could use binary search
    sort_data();
  }

  // Element access

  constexpr Value operator[](const Key& key) const {
    return get_or(key, Value{});
  }

  // Iterators
  constexpr const_iterator begin() const noexcept {
    return const_iterator(reinterpret_cast<const value_type*>(data_.data()));
  }
  constexpr const_iterator end() const noexcept {
    return const_iterator(
        reinterpret_cast<const value_type*>(data_.data() + N));
  }
  constexpr const_iterator cbegin() const noexcept { return begin(); }
  constexpr const_iterator cend() const noexcept { return end(); }

  // Capacity
  [[nodiscard]] constexpr bool empty() const noexcept { return N == 0; }
  constexpr size_type size() const noexcept { return N; }
  constexpr size_type max_size() const noexcept { return N; }

  // Lookup
  constexpr const_iterator find(const Key& key) const noexcept {
    auto it = std::lower_bound(
        data_.begin(), data_.end(), key,
        [](const Pair& p, const Key& k) { return p.first < k; });
    if (it != data_.end() && it->first == key) {
      return const_iterator(reinterpret_cast<const value_type*>(&*it));
    }
    return end();
  }
  constexpr bool contains(const Key& key) const noexcept {
    return find(key) != end();
  }
  constexpr size_type count(const Key& key) const noexcept {
    return contains(key) ? 1 : 0;
  }
  constexpr Value get_or(const Key& key, Value default_value) const noexcept {
    auto it = find(key);
    return it != end() ? it->second : default_value;
  }
  constexpr const Value* try_get(const Key& key) const noexcept {
    auto it = find(key);
    return it != end() ? &it->second : nullptr;
  }

  // Observers
  constexpr const auto& data() const noexcept { return data_; }
  constexpr auto keys() const noexcept {
    std::array<Key, N> result{};
    for (std::size_t i = 0; i < N; ++i) result[i] = data_[i].first;
    return result;
  }
  constexpr auto values() const noexcept {
    std::array<Value, N> result{};
    for (std::size_t i = 0; i < N; ++i) result[i] = data_[i].second;
    return result;
  }
};

// Non-member operators
export template <typename K, typename V, std::size_t N>
constexpr bool operator==(const ConstexprMap<K, V, N>& lhs,
                          const ConstexprMap<K, V, N>& rhs) noexcept {
  if (lhs.size() != rhs.size()) return false;
  for (const auto& [k, v] : lhs) {
    auto it = rhs.find(k);
    if (it == rhs.end() || it->second != v) return false;
  }
  return true;
}

export template <typename K, typename V, std::size_t N>
constexpr bool operator!=(const ConstexprMap<K, V, N>& lhs,
                          const ConstexprMap<K, V, N>& rhs) noexcept {
  return !(lhs == rhs);
}

// Factory function from std::array
export template <typename Key, typename Value, std::size_t N>
constexpr auto make_constexpr_map(
    const std::array<std::pair<Key, Value>, N>& arr) {
  return ConstexprMap<Key, Value, N>(arr);
}
