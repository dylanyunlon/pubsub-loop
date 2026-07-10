import json, urllib.request, uuid, time, sys

ORG = "70d4e66a-35b3-4ab1-8a1f-117c29c35eb8"
COOKIE = 'CH-prefers-color-scheme=dark; ajs_anonymous_id=claudeai.v1.cd7d3e3a-3c6b-473a-a9c5-67df7c2c0ce8; user-sidebar-pinned=false; share-session=qnomtg01f35s01djuhs4mph77i1rozpo; lastActiveOrg=70d4e66a-35b3-4ab1-8a1f-117c29c35eb8; user-sidebar-visible-on-load=false; _dd_s=aid=77e52800-bc89-4a98-a2c8-0d0f5a783e8b&rum=2&id=fd7a9fc6-3b7e-425a-b93b-f96be5814649&created=1783665771122&expire=1783666840306'

HEADERS = {
    "content-type": "application/json",
    "cookie": COOKIE,
    "origin": "https://claude.hk.cn",
    "referer": "https://claude.hk.cn/new",
    "anthropic-client-platform": "web_claude_ai",
    "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
    "sec-ch-ua": '"Google Chrome";v="149"',
    "sec-ch-ua-mobile": "?0",
    "sec-ch-ua-platform": '"Windows"',
    "sec-fetch-dest": "empty",
    "sec-fetch-mode": "cors",
    "sec-fetch-site": "same-origin",
}

def create_conv(name):
    h = dict(HEADERS); h["accept"] = "application/json"
    req = urllib.request.Request(
        f"https://claude.hk.cn/api/organizations/{ORG}/chat_conversations",
        json.dumps({"name": name, "model": "claude-sonnet-4-6",
                     "include_conversation_preferences": True, "is_temporary": False}).encode(),
        headers=h)
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read())["uuid"]

def fire_message(conv_id, prompt, timeout=15):
    """Send message, read just enough to confirm it started, then return"""
    h = dict(HEADERS); h["accept"] = "text/event-stream"
    data = {
        "prompt": prompt, "timezone": "Asia/Shanghai", "locale": "en-US",
        "model": "claude-sonnet-4-6", "effort": "high", "thinking_mode": "off",
        "tools": [{"type": "repl_v0", "name": "repl"}],
        "turn_message_uuids": {
            "human_message_uuid": str(uuid.uuid4()),
            "assistant_message_uuid": str(uuid.uuid4())
        },
        "attachments": [], "files": [], "sync_sources": [],
        "rendering_mode": "messages"
    }
    req = urllib.request.Request(
        f"https://claude.hk.cn/api/organizations/{ORG}/chat_conversations/{conv_id}/completion",
        json.dumps(data).encode(), headers=h)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            chunk = resp.read(3000).decode("utf-8", errors="replace")
            if "message_start" in chunk:
                return "STARTED"
            return chunk[:200]
    except Exception as e:
        if "timed out" in str(e).lower() or "timeout" in str(e).lower():
            return "STARTED (timeout=working)"
        return f"ERROR: {e}"

def run(batch_name, token):
    batch_file = f"/home/claude/pubsub-loop/docs/project-plan/data/batches/{batch_name}.json"
    with open(batch_file) as f:
        batch = json.load(f)
    
    samples = [i for i in batch["items"] if i["body_len"] > 2000][:2]
    needs_work = [i for i in batch["items"] if i["body_len"] < 1000][:15]
    
    # Create conv
    conv_id = create_conv(f"pubsub-loop-{batch_name}")
    print(f"Conv: https://claude.hk.cn/chat/{conv_id}")
    
    # MSG 1: Context
    msg1 = f"""你是 pub/sub-loop 项目的产品需求构建 agent。这是一个类区块链的世界模型个体系统，基于 Cyber RT。

你的批次：{batch_name}，模块 {', '.join(batch['modules'])}，共 {batch['total_items']} 条目。

质量对标 NVIDIA/CCCL。每个需求含：Description + Proposed Design + Dependencies + Sub-tasks + Acceptance Criteria + 代码示例。

参考高质量需求：
{json.dumps(samples, ensure_ascii=False)[:5000]}

请确认你理解了。"""

    r = fire_message(conv_id, msg1)
    print(f"  Msg1: {r}")
    time.sleep(20)
    
    # MSG 2: Token + items
    msg2 = f"""GitHub Project ID: PVT_kwHODJPfps4Bc4xI
GitHub Token: {token}

用 repl (code exec) 执行 curl GraphQL 请求，更新以下条目的 body。每个条目生成 CCCL 级别的需求描述。

mutation 格式：
curl -s -H "Authorization: bearer TOKEN" -H "Content-Type: application/json" -X POST https://api.github.com/graphql -d '{{"query":"mutation {{ updateProjectV2DraftIssue(input: {{ projectId: \\\\"PVT_kwHODJPfps4Bc4xI\\\\", draftIssueId: \\\\"ITEM_ID\\\\", body: \\\\"NEW_BODY\\\\" }}) {{ draftIssue {{ id }} }} }}"}}'

需要更新的条目：
{json.dumps(needs_work, ensure_ascii=False)[:8000]}

直接开始执行，不问确认。"""

    r = fire_message(conv_id, msg2)
    print(f"  Msg2: {r}")
    return conv_id

if __name__ == "__main__":
    batch = sys.argv[1]
    token = sys.argv[2]
    cid = run(batch, token)
    print(f"\n✅ {batch} → https://claude.hk.cn/chat/{cid}")

