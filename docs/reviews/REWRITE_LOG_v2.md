# PRD 改写进度日志 v2
日期: 2026-07-16

## 测试驱动改写（本轮新增）

按照附件PND测试用例SOP的思想：**测试用例就是需求定义**。
每条改写的PRD现在包含标准测试用例表：

| 编号 | 等级 | 测试项目 | 前置条件 | 测试步骤 | 测试标准 |
|------|------|---------|---------|---------|---------|
| TC-01 | L1 | ... | ... | 1. ... | 1. ... |

### 本轮新增测试驱动改写（直接push到GitHub Project）
- [BUG] pipeline_parallel::DeviceReduce::Sum 不确定性 (4627c, TC-01~05)
- [BUG][STF] P2P accesses fails (3668c, TC-01~05)
- [BUG] documentation mixing tensor_parallel (5840c, TC-01~05)  
- [BUG] nvcc 12.9 segfault STF (5143c, TC-01~05)
- [BUG] Low performance sorting OMP (3597c, TC-01~05)
- Support PDL (651c, TC-01~05)
- [BUG] CI pre-commit fails (4571c, TC-01~05)

### 验收标准补丁（追加到现有body）
- 12条 P0/P1 已补丁（上轮）

## 累计统计
- 完整测试驱动改写: 20+ 条
- 验收标准补丁: 12+ 条  
- 总改动: ~35 条 PRD 已写回 GitHub Project #4
