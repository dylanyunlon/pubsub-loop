#!/usr/bin/env python3
"""
Generate high-quality PRDs for pub/sub-loop project and push to GitHub Project #4.
Quality benchmark: NVIDIA/cccl level PRDs with full API surface, code examples, 
dependency tracking, and acceptance criteria.
"""

import json
import os
import sys
import time
import subprocess

TOKEN = os.environ.get("GITHUB_TOKEN", "")
PROJECT_ID = "PVT_kwHODJPfps4Bc4xI"

# Field IDs
FIELD_IDS = {
    "Status": "PVTSSF_lAHODJPfps4Bc4xIzhXeTvo",
    "Module": "PVTSSF_lAHODJPfps4Bc4xIzhXeVzQ",
    "Sprint": "PVTSSF_lAHODJPfps4Bc4xIzhXeVzU",
    "Claude": "PVTSSF_lAHODJPfps4Bc4xIzhXeVzY",
    "Language": "PVTSSF_lAHODJPfps4Bc4xIzhXeVzc",
    "Priority": "PVTSSF_lAHODJPfps4Bc4xIzhXeV1Q",
}

# Option IDs
STATUS_OPTS = {"Todo": "f75ad846", "In Progress": "47fc9ee4", "Needs Triage": "4a692029"}
PRIORITY_OPTS = {"P0": "e776b235", "P1": "ff4d1119", "P2": "012ba1af", "P3": "ba3190c6"}
MODULE_OPTS = {
    "base": "906b730e", "common": "0d9aa4e3", "component": "172fa262",
    "data": "0953bf92", "transport": "b28cb884", "scheduler": "a3a86687",
    "context": "73cd4e08", "profiler": "75f85b2d", "message": "5dcf4f88",
    "mainboard": "d98dafbd", "croutine": "b5401cff", "node": "74d96618",
    "io": "76f13f54", "tools": "560b7d14", "logger": "b719f463",
    "record": "249535f3", "service_discovery": "18affb3b", "statistics": "fb1bfa35",
    "plugin_manager": "7491b8c9", "blocker": "fe329177", "event": "271d97aa",
    "time": "445c2018", "task": "6e368b16", "service": "1bc8bb94",
    "parameter": "9605e53d", "sysmo": "e983b93f",
}
SPRINT_OPTS = {"Current": "60c8c15c", "Next": "4b59b246", "Phase 1: Skeleton": "e586be79"}
CLAUDE_OPTS = {
    "Claude-A": "d9b45e12", "Claude-D": "756df240", "Claude-E": "05b56fe6", "Manager": "12586bbe"
}
LANG_OPTS = {"C++/CUDA": "d20b6441", "Python": "ebef2c1f", "Mixed": "843358a5"}


def graphql(query):
    """Execute a GraphQL query."""
    import urllib.request
    req = urllib.request.Request(
        "https://api.github.com/graphql",
        data=json.dumps({"query": query}).encode(),
        headers={
            "Authorization": f"bearer {TOKEN}",
            "Content-Type": "application/json",
        }
    )
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())


def create_draft_issue(title, body):
    """Create a draft issue in the project."""
    # Escape body for GraphQL
    body_escaped = body.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    title_escaped = title.replace('"', '\\"')
    
    query = f'''mutation {{
        addProjectV2DraftIssue(input: {{
            projectId: "{PROJECT_ID}",
            title: "{title_escaped}",
            body: "{body_escaped}"
        }}) {{
            projectItem {{ id }}
        }}
    }}'''
    
    result = graphql(query)
    if 'errors' in result:
        print(f"  ERROR creating: {result['errors']}")
        return None
    return result['data']['addProjectV2DraftIssue']['projectItem']['id']


def set_field(item_id, field_id, option_id):
    """Set a single-select field value on a project item."""
    query = f'''mutation {{
        updateProjectV2ItemFieldValue(input: {{
            projectId: "{PROJECT_ID}",
            itemId: "{item_id}",
            fieldId: "{field_id}",
            value: {{ singleSelectOptionId: "{option_id}" }}
        }}) {{
            projectV2Item {{ id }}
        }}
    }}'''
    result = graphql(query)
    if 'errors' in result:
        print(f"  ERROR setting field: {result['errors']}")
        return False
    return True


def create_prd(prd):
    """Create a PRD in the project with all fields set."""
    print(f"  Creating: {prd['title'][:80]}...")
    
    item_id = create_draft_issue(prd['title'], prd['body'])
    if not item_id:
        return False
    
    # Set fields
    if prd.get('module') and prd['module'] in MODULE_OPTS:
        set_field(item_id, FIELD_IDS['Module'], MODULE_OPTS[prd['module']])
    
    if prd.get('priority') and prd['priority'] in PRIORITY_OPTS:
        set_field(item_id, FIELD_IDS['Priority'], PRIORITY_OPTS[prd['priority']])
    
    if prd.get('sprint') and prd['sprint'] in SPRINT_OPTS:
        set_field(item_id, FIELD_IDS['Sprint'], SPRINT_OPTS[prd['sprint']])
    
    if prd.get('status') and prd['status'] in STATUS_OPTS:
        set_field(item_id, FIELD_IDS['Status'], STATUS_OPTS[prd['status']])
    
    if prd.get('claude') and prd['claude'] in CLAUDE_OPTS:
        set_field(item_id, FIELD_IDS['Claude'], CLAUDE_OPTS[prd['claude']])
    
    if prd.get('language') and prd['language'] in LANG_OPTS:
        set_field(item_id, FIELD_IDS['Language'], LANG_OPTS[prd['language']])
    
    print(f"  ✓ Created: {item_id}")
    time.sleep(0.5)  # Rate limiting
    return True


def load_prds_from_file(filepath):
    """Load PRDs from a JSON file."""
    with open(filepath) as f:
        return json.load(f)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 generate_and_push_prds.py <prds_file.json>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    prds = load_prds_from_file(filepath)
    
    print(f"Loaded {len(prds)} PRDs from {filepath}")
    
    success = 0
    fail = 0
    for i, prd in enumerate(prds):
        print(f"\n[{i+1}/{len(prds)}]")
        if create_prd(prd):
            success += 1
        else:
            fail += 1
    
    print(f"\n=== Done: {success} created, {fail} failed ===")
