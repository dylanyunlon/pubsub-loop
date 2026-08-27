/******************************************************************************
 * binary.h — World process binary name metadata
 *
 * Ported from: apollo/cyber/binary.h
 * Namespace:   world::cyber::binary
 *****************************************************************************/

#ifndef CYBER_BINARY_H_
#define CYBER_BINARY_H_

#include <string>

namespace world {
namespace cyber {
namespace binary {

std::string GetName();
void SetName(const std::string& name);

}  // namespace binary
}  // namespace cyber
}  // namespace world

#endif  // CYBER_BINARY_H_
