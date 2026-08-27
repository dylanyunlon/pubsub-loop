/******************************************************************************
 * string_view.h — world::base string_view alias
 *
 * PRD #53: Provides world::base::string_view as the sole string_view alias.
 * No reserved __string_view identifier per ISO C++ [lex.name].
 *
 * Namespace: world::base (aliased into world::cyber::base for compat)
 *****************************************************************************/

#ifndef CYBER_BASE_STRING_VIEW_H_
#define CYBER_BASE_STRING_VIEW_H_

#include <string_view>

namespace world {
namespace base {

using string_view = std::string_view;

}  // namespace base

// Compat alias: code using cyber::base::string_view resolves here
namespace cyber {
namespace base {
using string_view = std::string_view;
}  // namespace base
}  // namespace cyber

}  // namespace world

#endif  // CYBER_BASE_STRING_VIEW_H_
