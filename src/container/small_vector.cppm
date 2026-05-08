/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: small_vector.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

export module container:small_vector;

import std;
import std.compat;

// Runtime check
[[noreturn]] inline void sv_terminate(const char* msg) { std::abort(); }

#define SV_ASSERT(cond, msg) \
  do {                       \
    if (!(cond)) {           \
      sv_terminate(msg);     \
    }                        \
  } while (0)

/**
 * @brief Base class for SmallVector memory management
 */
template <class Size_T>
class SmallVectorBase {
 protected:
  void* BeginX = nullptr;
  Size_T Size = 0;
  Size_T Capacity = 0;

  static constexpr size_t SizeTypeMax() {
    return std::numeric_limits<Size_T>::max();
  }

  SmallVectorBase() = delete;

  SmallVectorBase(void* FirstEl, size_t TotalCapacity)
      : BeginX(FirstEl), Capacity(static_cast<Size_T>(TotalCapacity)) {}

  __declspec(noinline) void* MallocForGrow(void* FirstEl, size_t MinSize,
                                           size_t TSize, size_t& NewCapacity) {
    size_t CurCapacity = this->Capacity;
    NewCapacity = 2 * CurCapacity + 1;
    if (NewCapacity < MinSize) NewCapacity = MinSize;

    void* NewElts = std::malloc(NewCapacity * TSize);
    if (!NewElts) {
      sv_terminate("malloc failed");
    }
    return NewElts;
  }

  __declspec(noinline) void GrowPod(void* FirstEl, size_t MinSize,
                                    size_t TSize) {
    size_t NewCapacity;
    void* NewElts = MallocForGrow(FirstEl, MinSize, TSize, NewCapacity);
    std::memcpy(NewElts, BeginX, Size * TSize);
    if (BeginX != FirstEl) std::free(BeginX);
    BeginX = NewElts;
    Capacity = static_cast<Size_T>(NewCapacity);
  }

 public:
  size_t size() const { return Size; }
  size_t capacity() const { return Capacity; }
  bool empty() const { return !Size; }

 protected:
  void set_size(size_t N) {
    SV_ASSERT(N <= capacity(), "size exceeds capacity");
    Size = static_cast<Size_T>(N);
  }

  void set_allocation_range(void* Begin, size_t N) {
    BeginX = Begin;
    Capacity = static_cast<Size_T>(N);
  }
};

/**
 * @brief Common functionality for SmallVector
 */
template <typename T>
class SmallVectorTemplateCommon : public SmallVectorBase<uint32_t> {
 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = size_t;

 protected:
  void* getFirstEl() const {
    return const_cast<void*>(
        reinterpret_cast<const void*>(reinterpret_cast<const char*>(this) +
                                      sizeof(SmallVectorBase<uint32_t>)));
  }

  SmallVectorTemplateCommon(size_t TotalCapacity)
      : SmallVectorBase<uint32_t>(getFirstEl(), TotalCapacity) {}

  bool isSmall() const { return BeginX == getFirstEl(); }

  void resetToSmall() {
    BeginX = getFirstEl();
    Size = Capacity = 0;
  }

 public:
  iterator begin() { return static_cast<iterator>(BeginX); }
  const_iterator begin() const { return static_cast<const_iterator>(BeginX); }
  iterator end() { return begin() + size(); }
  const_iterator end() const { return begin() + size(); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  size_t size_in_bytes() const { return size() * sizeof(T); }
  size_t capacity_in_bytes() const { return capacity() * sizeof(T); }

  pointer data() { return pointer(begin()); }
  const_pointer data() const { return const_pointer(begin()); }

  reference operator[](size_t idx) {
    SV_ASSERT(idx < size(), "index out of range");
    return begin()[idx];
  }
  const_reference operator[](size_t idx) const {
    SV_ASSERT(idx < size(), "index out of range");
    return begin()[idx];
  }

  reference front() {
    SV_ASSERT(!empty(), "front on empty vector");
    return begin()[0];
  }
  const_reference front() const {
    SV_ASSERT(!empty(), "front on empty vector");
    return begin()[0];
  }

  reference back() {
    SV_ASSERT(!empty(), "back on empty vector");
    return end()[-1];
  }
  const_reference back() const {
    SV_ASSERT(!empty(), "back on empty vector");
    return end()[-1];
  }
};

/**
 * @brief Template base for non-trivially copyable types
 */
template <typename T, bool = std::is_trivially_copyable_v<T>>
class SmallVectorTemplateBase : public SmallVectorTemplateCommon<T> {
 protected:
  using Base = SmallVectorTemplateCommon<T>;
  using Base::BeginX;
  using Base::Capacity;
  using Base::Size;

  static void destroy_range(T* S, T* E) {
    while (S != E) {
      --E;
      E->~T();
    }
  }

  template <typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
    std::uninitialized_move(I, E, Dest);
  }

  template <typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    std::uninitialized_copy(I, E, Dest);
  }

  __declspec(noinline) void grow(size_t MinSize = 0) {
    size_t NewCapacity;
    T* NewElts = static_cast<T*>(this->MallocForGrow(
        this->getFirstEl(), MinSize, sizeof(T), NewCapacity));

    uninitialized_move(this->begin(), this->end(), NewElts);
    destroy_range(this->begin(), this->end());

    if (!this->isSmall()) std::free(BeginX);

    this->set_allocation_range(NewElts, NewCapacity);
  }

 public:
  SmallVectorTemplateBase(size_t N) : Base(N) {}

  void push_back(const T& Elt) {
    if (Size >= Capacity) {
      grow();
    }
    new (static_cast<void*>(this->end())) T(Elt);
    ++Size;
  }

  void push_back(T&& Elt) {
    if (Size >= Capacity) {
      grow();
    }
    new (static_cast<void*>(this->end())) T(std::move(Elt));
    ++Size;
  }

  void pop_back() {
    SV_ASSERT(Size > 0, "pop_back on empty vector");
    --Size;
    this->end()->~T();
  }
};

/**
 * @brief Specialization for trivially copyable types
 */
template <typename T>
class SmallVectorTemplateBase<T, true> : public SmallVectorTemplateCommon<T> {
 protected:
  using Base = SmallVectorTemplateCommon<T>;
  using Base::BeginX;
  using Base::Capacity;
  using Base::Size;

  static void destroy_range(T*, T*) {}

  template <typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
    uninitialized_copy(I, E, Dest);
  }

  template <typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    if constexpr (std::is_pointer_v<It1> && std::is_pointer_v<It2> &&
                  std::is_same_v<std::remove_cv_t<std::remove_pointer_t<It1>>,
                                 T> &&
                  std::is_same_v<std::remove_cv_t<std::remove_pointer_t<It2>>,
                                 T>) {
      if (I != E) {
        std::memcpy(reinterpret_cast<void*>(Dest), I, (E - I) * sizeof(T));
      }
    } else {
      std::uninitialized_copy(I, E, Dest);
    }
  }

  void grow(size_t MinSize = 0) {
    this->GrowPod(this->getFirstEl(), MinSize, sizeof(T));
  }

 public:
  SmallVectorTemplateBase(size_t N) : Base(N) {}

  void push_back(const T& Elt) {
    if (Size >= Capacity) {
      grow();
    }
    std::memcpy(this->end(), &Elt, sizeof(T));
    ++Size;
  }

  void pop_back() {
    SV_ASSERT(Size > 0, "pop_back on empty vector");
    --Size;
  }
};
/**
 * @brief Implementation class for SmallVector
 */
template <typename T>
class SmallVectorImpl : public SmallVectorTemplateBase<T> {
  using Base = SmallVectorTemplateBase<T>;

 public:
  using typename Base::const_iterator;
  using typename Base::iterator;
  using typename Base::reference;
  using typename Base::size_type;

  explicit SmallVectorImpl(unsigned N) : Base(N) {}

  ~SmallVectorImpl() {
    if (!this->isSmall() && this->BeginX) {
      std::free(this->BeginX);
    }
  }

  void clear() {
    Base::destroy_range(this->begin(), this->end());
    this->Size = 0;
  }

  void resize(size_type N) {
    if (N == this->size()) return;

    if (N < this->size()) {
      Base::destroy_range(this->begin() + N, this->end());
      this->Size = N;
      return;
    }

    this->reserve(N);
    for (auto I = this->end(), E = this->begin() + N; I != E; ++I)
      new (&*I) T();
    this->Size = N;
  }

  void reserve(size_type N) {
    if (this->capacity() < N) this->grow(N);
  }

  template <typename ItTy>
    requires std::input_iterator<ItTy>
  void append(ItTy in_start, ItTy in_end) {
    size_type NumInputs = std::distance(in_start, in_end);
    this->reserve(this->size() + NumInputs);
    Base::uninitialized_copy(in_start, in_end, this->end());
    this->Size += NumInputs;
  }

  void append(size_type NumInputs, const T& Elt) {
    this->reserve(this->size() + NumInputs);
    std::uninitialized_fill_n(this->end(), NumInputs, Elt);
    this->Size += NumInputs;
  }

  void append(std::initializer_list<T> IL) { append(IL.begin(), IL.end()); }

  void assign(size_type count, const T& value) {
    clear();
    append(count, value);
  }

  template <typename ItTy>
    requires std::input_iterator<ItTy>
  void assign(ItTy first, ItTy last) {
    clear();
    append(first, last);
  }

  void assign(std::initializer_list<T> ilist) {
    clear();
    append(ilist.begin(), ilist.end());
  }

  template <typename... ArgTypes>
  reference emplace_back(ArgTypes&&... Args) {
    if (this->size() >= this->capacity()) this->grow();
    new (static_cast<void*>(this->end())) T(std::forward<ArgTypes>(Args)...);
    this->Size++;
    return this->back();
  }

  bool operator==(const SmallVectorImpl& RHS) const {
    if (this->size() != RHS.size()) return false;
    return std::equal(this->begin(), this->end(), RHS.begin());
  }

  bool operator!=(const SmallVectorImpl& RHS) const { return !(*this == RHS); }
};

/**
 * @brief Storage for inline elements
 */
template <typename T, unsigned N>
struct SmallVectorStorage {
  alignas(T) char InlineElts[N * sizeof(T)];
};

template <typename T>
struct SmallVectorStorage<T, 0> {
  alignas(T) char dummy;
};

/**
 * @brief Main SmallVector class with small buffer optimization
 */
export template <typename T, unsigned N = 8>
class SmallVector : public SmallVectorImpl<T>, SmallVectorStorage<T, N> {
 public:
  SmallVector() : SmallVectorImpl<T>(N) {}

  explicit SmallVector(size_t Size) : SmallVectorImpl<T>(N) {
    this->resize(Size);
  }

  SmallVector(size_t Size, const T& Value) : SmallVectorImpl<T>(N) {
    this->assign(Size, Value);
  }

  template <typename ItTy>
  SmallVector(ItTy S, ItTy E) : SmallVectorImpl<T>(N) {
    this->append(S, E);
  }

  SmallVector(std::initializer_list<T> IL) : SmallVectorImpl<T>(N) {
    this->append(IL.begin(), IL.end());
  }

  SmallVector(const SmallVector& RHS) : SmallVectorImpl<T>(N) {
    if (!RHS.empty()) this->append(RHS.begin(), RHS.end());
  }

  SmallVector& operator=(const SmallVector& RHS) {
    if (this != &RHS) {
      this->clear();
      this->append(RHS.begin(), RHS.end());
    }
    return *this;
  }

  SmallVector(SmallVector&& RHS) noexcept : SmallVectorImpl<T>(N) {
    if (!RHS.empty()) {
      if (RHS.isSmall()) {
        this->append(std::make_move_iterator(RHS.begin()),
                     std::make_move_iterator(RHS.end()));
        RHS.clear();
      } else {
        this->BeginX = RHS.BeginX;
        this->Size = RHS.Size;
        this->Capacity = RHS.Capacity;
        RHS.resetToSmall();
      }
    }
  }

  SmallVector& operator=(SmallVector&& RHS) noexcept {
    if (this != &RHS) {
      this->clear();
      if (!this->isSmall() && this->BeginX) std::free(this->BeginX);

      if (RHS.isSmall()) {
        this->resetToSmall();
        this->append(std::make_move_iterator(RHS.begin()),
                     std::make_move_iterator(RHS.end()));
        RHS.clear();
      } else {
        this->BeginX = RHS.BeginX;
        this->Size = RHS.Size;
        this->Capacity = RHS.Capacity;
        RHS.resetToSmall();
      }
    }
    return *this;
  }

  ~SmallVector() = default;
};
