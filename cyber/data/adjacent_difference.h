/******************************************************************************
 * adjacent_difference.h — Adjacent individual state delta computation
 *
 * PRD #158: SubtractLeft (forward diff) and SubtractRight (backward diff)
 *           tag-based adjacent_difference for ChannelBuffer data.
 *
 * Single-source: out[i] = transform(buf[i+1] - buf[i])  or  buf[i] - buf[i+1]
 * Dual-source:   out[i] = transform(buf_a[i] - buf_b[i])  (per-element)
 *
 * Namespace: world::cyber::data
 *****************************************************************************/

#ifndef CYBER_DATA_ADJACENT_DIFFERENCE_H_
#define CYBER_DATA_ADJACENT_DIFFERENCE_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "cyber/data/channel_buffer.h"

namespace world {
namespace cyber {
namespace data {

// ── Direction tags ──────────────────────────────────────────────────────────

struct SubtractLeft {};   ///< out[i] = buf[i+1] - buf[i]  (forward diff)
struct SubtractRight {};  ///< out[i] = buf[i] - buf[i+1]  (backward diff)

namespace detail {

/// Helper: compute a - b for types that support operator-.
template <typename T>
T Subtract(const T& a, const T& b) {
  return a - b;
}

}  // namespace detail

// ── Single-source adjacent_difference ───────────────────────────────────────

/// Compute adjacent differences from a single ChannelBuffer.
/// SubtractLeft:  out[i] = transform(input[i+1] - input[i]),  i in [0, N-2)
/// SubtractRight: out[i] = transform(input[i] - input[i+1]),  i in [0, N-2)
///
/// @param input   Source buffer to read from.
/// @param output  Destination vector (resized to N-1 elements).
/// @param transform  Optional unary transform applied after subtraction.
template <typename Direction, typename T,
          typename UnaryOp = std::identity>
void adjacent_difference(
    const std::vector<std::shared_ptr<T>>& input,
    std::vector<std::shared_ptr<T>>& output,
    UnaryOp transform = {}) {
  if (input.size() < 2) {
    output.clear();
    return;
  }

  size_t n = input.size() - 1;
  output.resize(n);

  for (size_t i = 0; i < n; ++i) {
    std::shared_ptr<T> delta;
    if constexpr (std::is_same_v<Direction, SubtractLeft>) {
      delta = std::make_shared<T>(
          detail::Subtract(*input[i + 1], *input[i]));
    } else {
      delta = std::make_shared<T>(
          detail::Subtract(*input[i], *input[i + 1]));
    }
    if constexpr (!std::is_same_v<UnaryOp, std::identity>) {
      *delta = transform(*delta);
    }
    output[i] = std::move(delta);
  }
}

// ── Dual-source adjacent_difference ─────────────────────────────────────────

/// Compute element-wise differences between two equally-sized buffers.
/// SubtractLeft:  out[i] = transform(input_a[i] - input_b[i])
/// SubtractRight: out[i] = transform(input_b[i] - input_a[i])
///
/// @return false if sizes don't match (output is not modified).
template <typename Direction, typename T,
          typename UnaryOp = std::identity>
[[nodiscard]] bool adjacent_difference(
    const std::vector<std::shared_ptr<T>>& input_a,
    const std::vector<std::shared_ptr<T>>& input_b,
    std::vector<std::shared_ptr<T>>& output,
    UnaryOp transform = {}) {
  if (input_a.size() != input_b.size()) {
    return false;
  }
  if (input_a.empty()) {
    output.clear();
    return true;
  }

  size_t n = input_a.size();
  output.resize(n);

  for (size_t i = 0; i < n; ++i) {
    std::shared_ptr<T> delta;
    if constexpr (std::is_same_v<Direction, SubtractLeft>) {
      delta = std::make_shared<T>(
          detail::Subtract(*input_a[i], *input_b[i]));
    } else {
      delta = std::make_shared<T>(
          detail::Subtract(*input_b[i], *input_a[i]));
    }
    if constexpr (!std::is_same_v<UnaryOp, std::identity>) {
      *delta = transform(*delta);
    }
    output[i] = std::move(delta);
  }
  return true;
}

}  // namespace data
}  // namespace cyber
}  // namespace world

#endif  // CYBER_DATA_ADJACENT_DIFFERENCE_H_
