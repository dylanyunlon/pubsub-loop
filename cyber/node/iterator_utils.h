/******************************************************************************
 * Copyright 2024 The World Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#ifndef CYBER_NODE_ITERATOR_UTILS_H_
#define CYBER_NODE_ITERATOR_UTILS_H_

/**
 * @file iterator_utils.h
 * @brief C++20 sentinel-aware distance() for channel iteration.
 *
 * Fixes: cyber::node::distance() now accepts heterogeneous
 * iterator/sentinel pairs (e.g. std::ranges::iota_view::iterator +
 * iota_view::sentinel).  Delegates to std::ranges::distance().
 *
 * PRD #592: cyber::node::distance does not work with
 *           std::ranges::iota_view in pub/sub-loop channel iteration.
 */

#include <concepts>
#include <iterator>
#include <ranges>

namespace world {
namespace cyber {
namespace node {

/// Primary overload: heterogeneous iterator + sentinel pair (C++20).
/// Accepts any (Iterator, Sentinel) pair satisfying sentinel_for.
template <std::input_or_output_iterator Iterator,
          std::sentinel_for<Iterator> Sentinel>
std::iter_difference_t<Iterator>
distance(Iterator first, Sentinel last) {
  return std::ranges::distance(first, last);
}

/// Backward-compatible overload: same-type endpoints.
/// Resolves without ambiguity via requires clause.
template <std::input_or_output_iterator Iterator>
std::iter_difference_t<Iterator>
distance(Iterator first, Iterator last)
    requires std::sentinel_for<Iterator, Iterator>
{
  return std::ranges::distance(first, last);
}

}  // namespace node
}  // namespace cyber
}  // namespace world

#endif  // CYBER_NODE_ITERATOR_UTILS_H_
