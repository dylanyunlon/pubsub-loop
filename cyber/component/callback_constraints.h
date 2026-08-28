/******************************************************************************
 * callback_constraints.h — Compile-time callback signature validation
 *
 * PRD #226: Turns runtime "message silently dropped because callback
 * signature doesn't match" into clear compile-time errors.
 *
 * Strategy:
 *   - C++20 compilers: `requires` clauses (concepts) for clean errors
 *   - C++17 compilers: `static_assert` with diagnostic messages
 *   - Both paths: `diagnose_reader_callback_error<F, MsgT>()` provides
 *     targeted guidance (by-value → "use shared_ptr", by-ref → same, etc.)
 *
 * Usage:
 *   template <typename MsgT, typename CallbackT>
 *   auto CreateReader(const std::string& topic, CallbackT&& cb) {
 *       constexpr_validate_reader_callback<CallbackT, MsgT>();
 *       // ... actual registration ...
 *   }
 *****************************************************************************/

#ifndef CYBER_COMPONENT_CALLBACK_CONSTRAINTS_H_
#define CYBER_COMPONENT_CALLBACK_CONSTRAINTS_H_

#include <functional>
#include <memory>
#include <type_traits>

namespace world {
namespace cyber {
namespace component {

// ═══════════════════════════════════════════════════════════════════════════════
// C++20 concepts (preferred — clean compiler errors)
// ═══════════════════════════════════════════════════════════════════════════════

#if __cplusplus >= 202002L

/// Reader callback must accept shared_ptr<const MsgT>
template <typename F, typename MsgT>
concept ReaderCallback =
    std::is_invocable_v<F, std::shared_ptr<const MsgT>>;

/// Tick callback takes no arguments
template <typename F>
concept TickCallback = std::is_invocable_v<F>;

/// Service callback receives (shared_ptr<const Req>, shared_ptr<Resp>)
template <typename F, typename ReqT, typename RespT>
concept ServiceCallback =
    std::is_invocable_v<F, std::shared_ptr<const ReqT>,
                           std::shared_ptr<RespT>>;

#endif  // C++20

// ═══════════════════════════════════════════════════════════════════════════════
// C++17-compatible constexpr validation (fallback / universal)
// ═══════════════════════════════════════════════════════════════════════════════

/// Signature diagnostic: what kind of mistake did the developer make?
enum class CallbackMismatch {
  kNone,           // Signature is correct
  kByValue,        // Passed MsgT by value instead of shared_ptr
  kByConstRef,     // Passed const MsgT& instead of shared_ptr
  kByRef,          // Passed MsgT& instead of shared_ptr
  kNonConstPtr,    // Passed shared_ptr<MsgT> instead of shared_ptr<const MsgT>
  kUnknown,        // Completely wrong signature
};

/// Detect the specific callback mismatch for targeted error messaging.
template <typename F, typename MsgT>
constexpr CallbackMismatch detect_reader_callback_mismatch() {
  if constexpr (std::is_invocable_v<F, std::shared_ptr<const MsgT>>) {
    return CallbackMismatch::kNone;
  } else if constexpr (std::is_invocable_v<F, MsgT>) {
    return CallbackMismatch::kByValue;
  } else if constexpr (std::is_invocable_v<F, const MsgT&>) {
    return CallbackMismatch::kByConstRef;
  } else if constexpr (std::is_invocable_v<F, MsgT&>) {
    return CallbackMismatch::kByRef;
  } else if constexpr (std::is_invocable_v<F, std::shared_ptr<MsgT>>) {
    return CallbackMismatch::kNonConstPtr;
  } else {
    return CallbackMismatch::kUnknown;
  }
}

/// Validate a Reader callback at compile time.
/// Produces clear static_assert messages for each mismatch variant.
///
/// Call this at the top of CreateReader<MsgT>():
///   constexpr_validate_reader_callback<CallbackT, MsgT>();
///
template <typename F, typename MsgT>
constexpr void constexpr_validate_reader_callback() {
  constexpr auto mismatch = detect_reader_callback_mismatch<F, MsgT>();

  static_assert(mismatch != CallbackMismatch::kByValue,
      "Reader callback receives MsgT by value. "
      "Change parameter to std::shared_ptr<const MsgT>. "
      "See: world/component/docs/callback_signatures.md");

  static_assert(mismatch != CallbackMismatch::kByConstRef,
      "Reader callback receives MsgT by const reference. "
      "Change parameter to std::shared_ptr<const MsgT>. "
      "See: world/component/docs/callback_signatures.md");

  static_assert(mismatch != CallbackMismatch::kByRef,
      "Reader callback receives MsgT by mutable reference. "
      "Change parameter to std::shared_ptr<const MsgT>. "
      "Messages are immutable after publish. "
      "See: world/component/docs/callback_signatures.md");

  static_assert(mismatch != CallbackMismatch::kNonConstPtr,
      "Reader callback receives shared_ptr<MsgT> (non-const). "
      "Change to std::shared_ptr<const MsgT>. "
      "Messages are immutable in the pub/sub pipeline. "
      "See: world/component/docs/callback_signatures.md");

  static_assert(mismatch != CallbackMismatch::kUnknown,
      "Reader callback signature does not match any known pattern. "
      "Expected: void(std::shared_ptr<const MsgT>). "
      "See: world/component/docs/callback_signatures.md");
}

/// Validate a Tick callback at compile time.
template <typename F>
constexpr void constexpr_validate_tick_callback() {
  static_assert(std::is_invocable_v<F>,
      "Tick callback must be invocable with zero arguments. "
      "Expected signature: void(). "
      "See: world/component/docs/callback_signatures.md");
}

/// Validate a Service callback at compile time.
template <typename F, typename ReqT, typename RespT>
constexpr void constexpr_validate_service_callback() {
  // Check correct signature first
  constexpr bool correct = std::is_invocable_v<
      F, std::shared_ptr<const ReqT>, std::shared_ptr<RespT>>;

  // Check for common Req/Resp swap mistake
  constexpr bool swapped = !correct && std::is_invocable_v<
      F, std::shared_ptr<const RespT>, std::shared_ptr<ReqT>>;

  static_assert(!swapped,
      "Service callback has Request and Response types swapped. "
      "Expected: void(shared_ptr<const ReqT>, shared_ptr<RespT>). "
      "The first parameter is the immutable request, "
      "the second is the mutable response to fill. "
      "See: world/component/docs/callback_signatures.md");

  static_assert(correct || swapped,
      "Service callback signature does not match. "
      "Expected: void(shared_ptr<const ReqT>, shared_ptr<RespT>). "
      "See: world/component/docs/callback_signatures.md");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Trait aliases for external use
// ═══════════════════════════════════════════════════════════════════════════════

/// True if F is a valid reader callback for MsgT
template <typename F, typename MsgT>
inline constexpr bool is_reader_callback_v =
    std::is_invocable_v<F, std::shared_ptr<const MsgT>>;

/// True if F is a valid tick callback
template <typename F>
inline constexpr bool is_tick_callback_v = std::is_invocable_v<F>;

/// True if F is a valid service callback for ReqT/RespT
template <typename F, typename ReqT, typename RespT>
inline constexpr bool is_service_callback_v =
    std::is_invocable_v<F, std::shared_ptr<const ReqT>,
                           std::shared_ptr<RespT>>;

}  // namespace component
}  // namespace cyber
}  // namespace world

#endif  // CYBER_COMPONENT_CALLBACK_CONSTRAINTS_H_
