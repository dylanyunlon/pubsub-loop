#!/usr/bin/env python3
import os
"""
pub/sub-loop 项目多维度评估工具

从 GitHub Project #4 拉取全部需求条目，生成多维度评估报告。
用法: python3 scripts/project_eval.py
"""

import json
import urllib.request
import time
from collections import Counter
from datetime import datetime

TOKEN = os.environ.get("GITHUB_TOKEN", "")
URL = "https://api.github.com/graphql"
PROJECT_NUMBER = 4

def graphql_request(query, variables=None):
    payload = {"query": query}
    if variables:
        payload["variables"] = variables
    req = urllib.request.Request(URL,
        data=json.dumps(payload).encode(),
        headers={"Authorization": f"bearer {TOKEN}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())

def fetch_all_items():
    """Fetch all project items with pagination"""
    all_items = []
    cursor = None
    
    while True:
        after = f', after: "{cursor}"' if cursor else ''
        query = f'''query {{
          user(login: "dylanyunlon") {{
            projectV2(number: {PROJECT_NUMBER}) {{
              items(first: 100{after}) {{
                pageInfo {{ hasNextPage endCursor }}
                nodes {{
                  id
                  content {{
                    ... on DraftIssue {{ title body }}
                  }}
                  fieldValues(first: 10) {{
                    nodes {{
                      ... on ProjectV2ItemFieldSingleSelectValue {{ name field {{ ... on ProjectV2FieldCommon {{ name }} }} }}
                      ... on ProjectV2ItemFieldTextValue {{ text field {{ ... on ProjectV2FieldCommon {{ name }} }} }}
                    }}
                  }}
                }}
              }}
            }}
          }}
        }}'''
        
        result = graphql_request(query)
        nodes = result['data']['user']['projectV2']['items']['nodes']
        page_info = result['data']['user']['projectV2']['items']['pageInfo']
        
        for node in nodes:
            item = {"id": node["id"]}
            content = node.get("content") or {}
            item["title"] = content.get("title", "")
            item["body"] = content.get("body", "")
            for fv in node.get("fieldValues", {}).get("nodes", []):
                if fv and "field" in fv:
                    fn = fv["field"].get("name", "")
                    val = fv.get("name") or fv.get("text") or ""
                    if fn and val:
                        item[fn] = val
            all_items.append(item)
        
        if not page_info["hasNextPage"]:
            break
        cursor = page_info["endCursor"]
        time.sleep(0.2)
    
    return all_items

def evaluate(items):
    """Multi-dimensional evaluation"""
    report = []
    report.append(f"# pub/sub-loop 项目评估报告")
    report.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    report.append(f"总需求条目: {len(items)}")
    report.append("")
    
    # 1. 内容质量维度
    has_body = sum(1 for i in items if len(i.get("body", "")) > 100)
    report.append(f"## 1. 内容质量")
    report.append(f"- 有详细描述（>100字）: {has_body} ({has_body*100//len(items)}%)")
    report.append(f"- 缺少描述: {len(items)-has_body} ({(len(items)-has_body)*100//len(items)}%)")
    report.append("")
    
    # 2. 模块覆盖维度
    modules = Counter(i.get("Module", "unknown") for i in items)
    report.append(f"## 2. 模块覆盖")
    for m, c in modules.most_common():
        bar = "█" * (c // 10)
        report.append(f"- {m:20s}: {c:4d} {bar}")
    report.append("")
    
    # 3. 优先级分布
    priorities = Counter(i.get("Priority", "unknown") for i in items)
    report.append(f"## 3. 优先级分布")
    for p, c in priorities.most_common():
        report.append(f"- {p}: {c}")
    report.append("")
    
    # 4. 阶段分布
    sprints = Counter(i.get("Sprint", "unknown") for i in items)
    report.append(f"## 4. 阶段分布")
    for s, c in sprints.most_common():
        report.append(f"- {s}: {c}")
    report.append("")
    
    # 5. 状态分布
    statuses = Counter(i.get("Status", "unknown") for i in items)
    report.append(f"## 5. 状态分布")
    for s, c in statuses.most_common():
        report.append(f"- {s}: {c}")
    report.append("")
    
    # 6. Claude 工作负载
    claudes = Counter(i.get("Claude", "unknown") for i in items)
    report.append(f"## 6. Claude 工作负载")
    for cl, c in claudes.most_common():
        report.append(f"- {cl}: {c}")
    report.append("")
    
    # 7. 需求类型
    types = Counter()
    for i in items:
        t = i.get("title", "")
        for prefix in ["EPIC", "FEA", "BUG", "THEME", "STF", "DOC", "WIP", "PoC"]:
            if t.startswith(f"[{prefix}]"):
                types[prefix] += 1
                break
        else:
            types["Other"] += 1
    report.append(f"## 7. 需求类型")
    for t, c in types.most_common():
        report.append(f"- {t}: {c}")
    report.append("")
    
    # 8. needle-tools 关联度
    needle_mentions = sum(1 for i in items if "needle" in i.get("body", "").lower())
    report.append(f"## 8. 个体输出动态关联")
    report.append(f"- 提及 needle-tools: {needle_mentions} ({needle_mentions*100//len(items)}%)")
    report.append(f"- 提及 GLB: {sum(1 for i in items if 'GLB' in i.get('body', ''))}")
    report.append(f"- 提及个体动态输出: {sum(1 for i in items if '个体动态输出' in i.get('body', ''))}")
    
    return "\n".join(report)

if __name__ == "__main__":
    print("Fetching project items...")
    items = fetch_all_items()
    print(f"Fetched {len(items)} items")
    
    report = evaluate(items)
    print(report)
    
    with open("docs/project-plan/EVALUATION_REPORT.md", "w") as f:
        f.write(report)
    print("\nReport saved to docs/project-plan/EVALUATION_REPORT.md")
