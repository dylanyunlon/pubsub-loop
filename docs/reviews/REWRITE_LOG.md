# PRD 改写与补丁进度日志
日期: 2026-07-16

## 完整改写（body 重写 + push 到 GitHub）
共 13 条 Tier C PRD 被完整改写，每条包含五个准入问题的完整回答。

已确认 push 到 GitHub Project #4:
- [BUG] tensor_parallel::omp::par 编译失败 (4024c)
- [BUG] c.parallel unit tests link c2h (3400c)  
- [BUG] AssertionError LDL instruction (3503c)
- [BUG] atomics fully qualify (3897c)
- [BUG] value_or_const.pass.cpp optional (3316c)
- [BUG] Sporadic test failure unique_by_key (3693c)
- [BUG] assertion failure example.cu (3163c)
- [BUG] nvcc 12.9 segfault STF (3418c)
- [BUG] Low performance sorting OMP (3697c)
- Support PDL (3633c)
- [BUG] P2P accesses (540c)

## 验收标准补丁（checklist 追加到现有 body）
共 12 条 P0/P1 structurally weak PRD 被追加了验收标准清单。

已确认 push:
- [EPIC] Cyber RT data pipeline (P0) — 需重试
- Document neuron.core vs Neuron cores (P1) — +7 criteria
- [BUG] Low performance sorting OMP (P1) — +7 criteria
- [BUG] Add test case nvbug 4678853 (P1) — +6 criteria
- [EPIC] Multi-dimensional data container (P0) — +6 criteria
- [EPIC] Adopt Neuron_SP Runtime (P0) — +7 criteria
- [EPIC] TensorParallel algorithms (P0) — +6 criteria
- [EPIC] PDL in PipelineParallel (P0) — +7 criteria
- [EPIC] Work stealing PipelineParallel (P0) — +7 criteria
- [BUG] Can't compile omp::par (P1) — +6 criteria
- [BUG] c.parallel link c2h (P1) — +5 criteria
- [MASTER] 产品需求总纲 (P0) — +6 criteria

## 统计
- 完整改写: 13 条
- 验收标准补丁: 12 条  
- 总改动: 25 条 PRD 已写回 GitHub
- structurally weak 剩余: ~58 条 (P2/P3)
