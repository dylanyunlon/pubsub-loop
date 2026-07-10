# pub/sub-loop 产品需求构建 — 分发日志

> 分发时间：2026-07-10

## 对话列表

| 批次 | 模块 | 条目数 | 需细化 | 对话链接 | 状态 |
|------|------|--------|--------|----------|------|
| batch2_core | transport, data, scheduler, croutine | 195 | 147 | https://claude.hk.cn/chat/c2ae7cc0-ac1e-49a4-96c6-6a343c6a6fdd | ✅ dispatched |
| batch4_upper | component, context, time, message, node | 127 | 118 | https://claude.hk.cn/chat/6c812b84-6299-411d-8bc5-fe27409bf6bb | ✅ dispatched |
| batch5_aux | profiler + 小模块 | 84 | 70 | https://claude.hk.cn/chat/cbf65d8c-ce1d-4f1a-9ed7-c5c26815ce8b | ✅ dispatched |
| batch3_infra | base, io | 352 | 344 | https://claude.hk.cn/chat/d0c51155-7bc0-459f-afe0-80951053611d | ⚠️ msg1 rate-limited |
| batch1_common_tools | common, tools | 868 | 680 | https://claude.hk.cn/chat/2efa486a-1686-4852-b923-4007581c1fee | ✅ dispatched |

## 检查进度

打开上面的对话链接查看各 agent 的执行情况。如果某个 agent 中断了，可以在对话中发送 "Continue" 让它继续。

## 补发指令

如果某个 batch 的 agent 没有正确理解任务，可以用以下脚本重新发送：

```bash
python3 dispatch_fire.py <batch_name> <github_token>
```

## 注意

- batch3_infra 的第一条消息被限流，agent 可能缺少完整上下文。建议打开对话手动补充项目背景。
- 各 agent 使用 claude-sonnet-4-6 模型，effort=high。
- 每个 agent 首轮处理 15 个条目，需要在对话中发 "Continue" 让它继续处理剩余条目。
