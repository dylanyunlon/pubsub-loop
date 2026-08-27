/******************************************************************************
 * context.h — Type-erased key-value context for individual execution state
 *
 * Ported from: apollo/cyber/context/context.h
 * Namespace:   world::cyber::context
 *
 * Provides a per-individual (or per-node) bag of typed state that can be
 * passed through the scheduler/component pipeline without coupling callers
 * to concrete types.  Thread-safe variants use a mutex; unsynchronized
 * variants are available for single-writer / single-reader hot paths.
 *****************************************************************************/

#ifndef CYBER_CONTEXT_CONTEXT_H_
#define CYBER_CONTEXT_CONTEXT_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace world {
namespace cyber {
namespace context {

class Context {
 public:
  Context() = default;
  ~Context() = default;

  /// Set a shared_ptr value (unsynchronized — caller must ensure exclusion).
  template <typename T>
  void Set(const std::string& key, const std::shared_ptr<T>& value) {
    store_[key] = value;
  }

  /// Set a value by copy (unsynchronized).
  template <typename T>
  void Set(const std::string& key, const T& value) {
    store_[key] = std::make_shared<T>(value);
  }

  /// Get a typed value, or nullptr if absent (unsynchronized).
  template <typename T>
  std::shared_ptr<T> Get(const std::string& key) const {
    auto it = store_.find(key);
    if (it != store_.end()) {
      return std::static_pointer_cast<T>(it->second);
    }
    return nullptr;
  }

  /// Thread-safe Set (mutex-guarded).
  template <typename T>
  void SafeSet(const std::string& key, const std::shared_ptr<T>& value) {
    std::lock_guard<std::mutex> lock(mu_);
    Set(key, value);
  }

  /// Thread-safe Set by copy (mutex-guarded).
  template <typename T>
  void SafeSet(const std::string& key, const T& value) {
    std::lock_guard<std::mutex> lock(mu_);
    Set(key, value);
  }

  /// Thread-safe Get (mutex-guarded).
  template <typename T>
  std::shared_ptr<T> SafeGet(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mu_);
    return Get<T>(key);
  }

  bool Has(const std::string& key) const {
    return store_.count(key) > 0;
  }

  void Remove(const std::string& key) {
    store_.erase(key);
  }

  void Clear() { store_.clear(); }

 private:
  std::map<std::string, std::shared_ptr<void>> store_;
  mutable std::mutex mu_;
};

}  // namespace context
}  // namespace cyber
}  // namespace world

#endif  // CYBER_CONTEXT_CONTEXT_H_
