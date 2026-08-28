/******************************************************************************
 * strided_ndarray_iterator.h — Context-scoped strided N-D array iterator
 *
 * PRD #234: Zero-copy traversal of non-contiguous IndividualState data.
 * Eliminates intermediate buffer copies when algorithms need a single field
 * (e.g., position_x) from an array of structs.
 *
 * Key features:
 *   - N-dimensional strided access (default N=1 for column projection)
 *   - project_field(&Struct::member) factory for struct-of-arrays style
 *   - Random-access iterator compatible with std algorithms and Thrust
 *   - __host__ __device__ annotations for CUDA kernel use
 *
 * Thread safety: Iterator instances are independent (no shared state).
 * Multiple threads can hold and advance separate iterators over the same
 * underlying data concurrently (read-only). Mutable access requires
 * external synchronization on the underlying data.
 *****************************************************************************/

#ifndef CYBER_CONTEXT_STRIDED_NDARRAY_ITERATOR_H_
#define CYBER_CONTEXT_STRIDED_NDARRAY_ITERATOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>

// CUDA host/device annotation macros
#if defined(__CUDACC__)
#define WORLD_HOST_DEVICE __host__ __device__
#else
#define WORLD_HOST_DEVICE
#endif

namespace world {
namespace cyber {
namespace context {

// ═══════════════════════════════════════════════════════════════════════════════
// StridedNdArrayIterator<T, N>
// ═══════════════════════════════════════════════════════════════════════════════

template <typename T, int N = 1>
class StridedNdArrayIterator {
 public:
  // ── Iterator traits (Thrust / STL compatible) ──
  using iterator_category = std::random_access_iterator_tag;
  using value_type        = T;
  using difference_type   = std::ptrdiff_t;
  using pointer           = T*;
  using reference         = T&;

  // ── Construction ──

  StridedNdArrayIterator() noexcept : base_(nullptr), linear_index_(0) {
    shape_.fill(0);
    strides_bytes_.fill(0);
  }

  /**
   * Construct from raw pointer, shape, and byte strides.
   *
   * @param base           Pointer to the first element of dimension 0
   * @param shape          Number of elements along each dimension
   * @param strides_bytes  Byte stride between consecutive elements per dim
   */
  StridedNdArrayIterator(T* base,
                          const std::array<int, N>& shape,
                          const std::array<std::ptrdiff_t, N>& strides_bytes)
      noexcept
      : base_(reinterpret_cast<uint8_t*>(base)),
        shape_(shape),
        strides_bytes_(strides_bytes),
        linear_index_(0) {
    compute_total_elements();
  }

  // ── 1D convenience factory ──

  /**
   * Create a 1D strided iterator (most common: column projection).
   */
  static StridedNdArrayIterator make_1d(T* base, int n,
                                         std::ptrdiff_t stride_bytes) noexcept {
    static_assert(N == 1, "make_1d only available for N=1");
    return StridedNdArrayIterator(base, {n}, {stride_bytes});
  }

  // ── Field projection factory ──

  /**
   * Project a single field from an array of structs.
   *
   * Example:
   *   auto it = StridedNdArrayIterator<float, 1>::project_field(
   *       states, count, &IndividualState::position, &Vec3f::x);
   *
   * For direct member pointers:
   *   auto it = project_field(states, count, &MyStruct::score);
   *
   * Returns a 1D iterator that walks &states[0].field, &states[1].field, ...
   */
  template <typename Struct>
  static StridedNdArrayIterator<T, 1> project_field(
      Struct* data, int count, T Struct::*member_ptr) noexcept {
    static_assert(N == 1, "project_field only produces 1D iterators");

    // Compute byte offset of the member within the struct
    Struct dummy{};
    auto base_addr = reinterpret_cast<const uint8_t*>(&dummy);
    auto field_addr = reinterpret_cast<const uint8_t*>(&(dummy.*member_ptr));
    std::ptrdiff_t offset = field_addr - base_addr;

    T* field_base = reinterpret_cast<T*>(
        reinterpret_cast<uint8_t*>(data) + offset);

    return StridedNdArrayIterator<T, 1>::make_1d(
        field_base, count,
        static_cast<std::ptrdiff_t>(sizeof(Struct)));
  }

  // ── Element access ──

  WORLD_HOST_DEVICE reference operator*() const noexcept {
    return *element_ptr(linear_index_);
  }

  WORLD_HOST_DEVICE reference operator[](difference_type n) const noexcept {
    return *element_ptr(linear_index_ + n);
  }

  WORLD_HOST_DEVICE T& device_deref() const noexcept {
    return *element_ptr(linear_index_);
  }

  // ── Pointer to current element ──

  WORLD_HOST_DEVICE pointer ptr() const noexcept {
    return element_ptr(linear_index_);
  }

  // ── Arithmetic ──

  StridedNdArrayIterator& operator++() noexcept {
    ++linear_index_;
    return *this;
  }

  StridedNdArrayIterator operator++(int) noexcept {
    auto copy = *this;
    ++linear_index_;
    return copy;
  }

  StridedNdArrayIterator& operator--() noexcept {
    --linear_index_;
    return *this;
  }

  StridedNdArrayIterator operator--(int) noexcept {
    auto copy = *this;
    --linear_index_;
    return copy;
  }

  StridedNdArrayIterator& operator+=(difference_type n) noexcept {
    linear_index_ += n;
    return *this;
  }

  StridedNdArrayIterator& operator-=(difference_type n) noexcept {
    linear_index_ -= n;
    return *this;
  }

  StridedNdArrayIterator operator+(difference_type n) const noexcept {
    auto copy = *this;
    copy.linear_index_ += n;
    return copy;
  }

  StridedNdArrayIterator operator-(difference_type n) const noexcept {
    auto copy = *this;
    copy.linear_index_ -= n;
    return copy;
  }

  difference_type operator-(const StridedNdArrayIterator& other) const
      noexcept {
    return linear_index_ - other.linear_index_;
  }

  friend StridedNdArrayIterator operator+(
      difference_type n, const StridedNdArrayIterator& it) noexcept {
    return it + n;
  }

  // ── Comparison ──

  bool operator==(const StridedNdArrayIterator& other) const noexcept {
    return base_ == other.base_ && linear_index_ == other.linear_index_;
  }

  bool operator!=(const StridedNdArrayIterator& other) const noexcept {
    return !(*this == other);
  }

  bool operator<(const StridedNdArrayIterator& other) const noexcept {
    return linear_index_ < other.linear_index_;
  }

  bool operator<=(const StridedNdArrayIterator& other) const noexcept {
    return linear_index_ <= other.linear_index_;
  }

  bool operator>(const StridedNdArrayIterator& other) const noexcept {
    return linear_index_ > other.linear_index_;
  }

  bool operator>=(const StridedNdArrayIterator& other) const noexcept {
    return linear_index_ >= other.linear_index_;
  }

  // ── Introspection ──

  int total_elements() const noexcept { return total_elements_; }
  const std::array<int, N>& shape() const noexcept { return shape_; }
  const std::array<std::ptrdiff_t, N>& strides_bytes() const noexcept {
    return strides_bytes_;
  }

  /**
   * Create a "past-the-end" iterator for range-based use:
   *   for (auto it = begin; it != end(begin); ++it) { ... }
   */
  StridedNdArrayIterator end() const noexcept {
    auto copy = *this;
    copy.linear_index_ = total_elements_;
    return copy;
  }

 private:
  /**
   * Compute the byte offset for a given linear index.
   * For N=1: offset = index * strides_bytes_[0]
   * For N>1: decompose linear index into multi-dimensional coordinates
   *          and sum the byte offsets.
   */
  WORLD_HOST_DEVICE T* element_ptr(difference_type linear_idx) const noexcept {
    if constexpr (N == 1) {
      // Fast path: single dimension
      return reinterpret_cast<T*>(base_ + linear_idx * strides_bytes_[0]);
    } else {
      // N-dimensional: row-major decomposition
      std::ptrdiff_t byte_offset = 0;
      difference_type remaining = linear_idx;
      for (int d = N - 1; d >= 0; --d) {
        int coord = static_cast<int>(remaining % shape_[d]);
        remaining /= shape_[d];
        byte_offset += coord * strides_bytes_[d];
      }
      return reinterpret_cast<T*>(base_ + byte_offset);
    }
  }

  void compute_total_elements() noexcept {
    total_elements_ = 1;
    for (int d = 0; d < N; ++d) {
      total_elements_ *= shape_[d];
    }
  }

  uint8_t* base_;
  std::array<int, N> shape_;
  std::array<std::ptrdiff_t, N> strides_bytes_;
  difference_type linear_index_;
  int total_elements_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Free function factory
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Create a strided iterator from raw components.
 */
template <typename T, int N = 1>
StridedNdArrayIterator<T, N> make_strided_iterator(
    T* base,
    const std::array<int, N>& shape,
    const std::array<std::ptrdiff_t, N>& strides_bytes) {
  return StridedNdArrayIterator<T, N>(base, shape, strides_bytes);
}

/**
 * Create a 1D strided iterator for a single field of an AoS layout.
 */
template <typename T, typename Struct>
StridedNdArrayIterator<T, 1> make_field_iterator(
    Struct* data, int count, T Struct::*member_ptr) {
  return StridedNdArrayIterator<T, 1>::project_field(data, count, member_ptr);
}

}  // namespace context
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CONTEXT_STRIDED_NDARRAY_ITERATOR_H_
