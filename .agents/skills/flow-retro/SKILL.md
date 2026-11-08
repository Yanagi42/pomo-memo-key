---
name: flow-retro
description: >-
  Review a rough coding session and propose practical setup improvements:
  automated checks, layout pointers, or leaner agent rules. Proposes only.
disable-model-invocation: true
---

# Flow: Retro

Improve agent setup after a session that hit avoidable friction. Propose changes to automated checks, layout pointers, or project rules—never product code. Skip smooth sessions.

## 1. Find concrete friction

Inspect the session (or user-named task `## Progress` / review findings) alongside repo scripts, CI config, `.workflow/standards.md`, `AGENTS.md`, and `.workflow/bin/workflow hook status`. Every proposal must cite a specific failure or slowdown that actually happened—skip generic best practices.

## 2. Pick the lightest fix

Keep `AGENTS.md` short and `.workflow/standards.md` under one screen (CONVENTIONS §2):

| What went wrong | Fix to propose |
| :--- | :--- |
| Bug or formatting/type error a tool can catch | Wire an existing script, linter rule, test, CI step, or `workflow hook install` instead of adding prose rules |
| Reviewer missed a project-specific convention | Add 1–2 clear lines under `## Code conventions` in `.workflow/standards.md` |
| Slow file search or non-obvious directory | Add a one-line pointer under `## Layout` in `.workflow/standards.md` |
| Bloated or ignored rules in `AGENTS.md` / `CLAUDE.md` | Delete unused lines or move enforceable rules into automated checks |

## 3. Propose and wait for approval

List proposals ordered by impact: (1) what went wrong in the session, (2) target file or check, and (3) exact diff or command. **Change nothing until the user selects which items to apply.**
