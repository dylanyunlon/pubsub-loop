/******************************************************************************
 * governance.h — Motion request governance for world individuals
 *
 * Integrates agent-governance-toolkit policy concepts into CyberRT transport.
 * Every MotionRequest passes through GovernanceFilter before resolution.
 *
 * Design from agent-governance-toolkit:
 *   - TrustRing: ring-based trust levels (kernel/system/user/untrusted)
 *   - PolicyEngine: rule-based allow/deny/modify decisions
 *   - AuditLog: every decision logged for replay/debug
 *****************************************************************************/

#ifndef CYBER_TRANSPORT_GOVERNANCE_H_
#define CYBER_TRANSPORT_GOVERNANCE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cyber/base/bounded_queue.h"
#include "cyber/common/log.h"
#include "cyber/transport/individual_state.h"

namespace world {
namespace cyber {
namespace transport {

/**
 * Trust rings — from agent-governance-toolkit's TrustManager
 * Determines what an individual is allowed to do
 */
enum class TrustRing : uint8_t {
  kKernel = 0,    // World engine itself — unrestricted
  kSystem = 1,    // Core infrastructure nodes
  kUser = 2,      // Normal individual agents
  kUntrusted = 3, // Sandbox / quarantine
};

/**
 * Governance decision — matches proto GovernanceVerdict
 */
enum class Decision : uint8_t {
  kAllow = 0,
  kDeny = 1,
  kModify = 2,   // Allow but clamp/adjust the request
  kDefer = 3,    // Need more context, decide next tick
};

/**
 * Policy rule — a single governance check
 */
struct PolicyRule {
  std::string name;
  TrustRing min_trust;  // Minimum trust to pass
  
  // Predicate: (individual_id, motion_request_data) → decision
  std::function<Decision(uint64_t, const void*, size_t)> evaluate;
};

/**
 * Governance verdict with reason
 */
struct GovernanceResult {
  Decision decision;
  std::string reason;
  uint64_t policy_id;
  uint64_t timestamp_ns;
};

/**
 * Audit entry — every governance decision is logged
 */
struct AuditEntry {
  uint64_t individual_id;
  uint64_t tick_id;
  Decision decision;
  std::string rule_name;
  uint64_t latency_ns;
  uint64_t timestamp_ns;
};

/**
 * GovernanceFilter — evaluates motion requests against policies
 *
 * Called in the hot path: must be fast.
 * Rules are evaluated in priority order; first non-ALLOW stops.
 */
class GovernanceFilter {
 public:
  GovernanceFilter() = default;

  /**
   * Add a policy rule
   */
  void AddRule(PolicyRule rule) {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    rules_.push_back(std::move(rule));
  }

  /**
   * Set trust level for an individual
   */
  void SetTrust(uint64_t individual_id, TrustRing ring) {
    std::lock_guard<std::mutex> lock(trust_mutex_);
    trust_map_[individual_id] = ring;
  }

  /**
   * Evaluate a motion request — the hot path
   * Returns governance decision.
   */
  GovernanceResult Evaluate(uint64_t individual_id,
                            const void* request_data,
                            size_t request_size) {
    auto t0 = std::chrono::steady_clock::now();

    // Get trust level
    TrustRing trust = TrustRing::kUser;  // default
    {
      std::lock_guard<std::mutex> lock(trust_mutex_);
      auto it = trust_map_.find(individual_id);
      if (it != trust_map_.end()) trust = it->second;
    }

    // Kernel trust bypasses all rules
    if (trust == TrustRing::kKernel) {
      return {Decision::kAllow, "kernel_bypass", 0, now_ns()};
    }

    // Evaluate rules
    GovernanceResult result{Decision::kAllow, "", 0, now_ns()};
    {
      std::lock_guard<std::mutex> lock(rules_mutex_);
      for (size_t i = 0; i < rules_.size(); ++i) {
        auto& rule = rules_[i];
        
        // Trust check
        if (trust > rule.min_trust) {
          result.decision = Decision::kDeny;
          result.reason = "trust_insufficient:" + rule.name;
          result.policy_id = i;
          break;
        }

        // Rule evaluation
        Decision d = rule.evaluate(individual_id, request_data, request_size);
        if (d != Decision::kAllow) {
          result.decision = d;
          result.reason = rule.name;
          result.policy_id = i;
          break;
        }
      }
    }

    auto t1 = std::chrono::steady_clock::now();
    uint64_t lat = std::chrono::duration_cast<std::chrono::nanoseconds>(
        t1 - t0).count();

    // Audit log (lock-free push)
    AuditEntry entry{individual_id, 0, result.decision,
                     result.reason, lat, result.timestamp_ns};
    audit_log_.Enqueue(entry);

    total_evals_.fetch_add(1, std::memory_order_relaxed);
    if (result.decision == Decision::kDeny) {
      total_denies_.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
  }

  // Metrics
  uint64_t total_evaluations() const {
    return total_evals_.load(std::memory_order_relaxed);
  }
  uint64_t total_denies() const {
    return total_denies_.load(std::memory_order_relaxed);
  }

 private:
  static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }

  std::mutex rules_mutex_;
  std::vector<PolicyRule> rules_;

  std::mutex trust_mutex_;
  std::unordered_map<uint64_t, TrustRing> trust_map_;

  base::BoundedQueue<AuditEntry> audit_log_{65536};  // Ring buffer

  std::atomic<uint64_t> total_evals_{0};
  std::atomic<uint64_t> total_denies_{0};
};

}  // namespace transport
}  // namespace cyber
}  // namespace world

#endif  // CYBER_TRANSPORT_GOVERNANCE_H_
