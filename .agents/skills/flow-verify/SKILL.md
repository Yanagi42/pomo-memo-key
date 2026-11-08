---
name: flow-verify
description: >-
  Review a change in a fresh subagent for requirement completion, evidence, and
  code quality. Use before landing or when the user says "review this".
---

# Flow: Verify

Review in a fresh subagent, never in the context that wrote the change (CONVENTIONS §5).

## 1. Pin the base

Use the task's `Base commit`, or `git merge-base <landing branch> HEAD` after a rebase; without a task, `main` / `origin/HEAD`. Confirm there is something to review: `git diff <base>`, `git status --short`, `git log <base>..HEAD --oneline`.

## 2. Spawn reviewers

- Spawn one fresh `workflow-reviewer` covering both requirements & proof and code quality. Pass the base SHA, the task or spec path, `.workflow/standards.md`, and any untracked files. For a large cross-module diff, run requirements & proof and code quality as two parallel subagents.
- When the change touches a trust boundary, also spawn `security-reviewer` on the changed boundary files and their direct callers and callees.
- If the host has no named agents, spawn a general read-only subagent with `.agents/agents/<name>.md` as its brief.

## 3. Act on findings

Record the base SHA and outcome (`pass` or `blocked`) in `## Progress`. Any `blocking` finding means not ready.
- Fix the whole round's findings together, then rerun the affected checks once.
- Rerun the reviewer only if a `blocking` finding was fixed or the fixes added files, callers, or trust boundaries.
- **Round cap & escalation:** cap review-fix cycles at at most 2 rounds (initial review + one re-review). If blocking findings remain after round 2, stop automatically: do not keep looping or guessing. Present the remaining blocking findings, evidence, and failed attempts to the user. Do not land or proceed until the user either waives the finding (recorded in `.workflow/decisions.md` with `Decided by: user` per CONVENTIONS §5), provides explicit direction, or marks the task `paused` / `blocked` to replan.

## 4. Effort close mode

When called from `flow-close`, run `workflow-reviewer` on requirements & proof plus cross-task integration, from the first task's base commit. Reuse recorded reviews for tasks unchanged since. Run `security-reviewer` if the effort as a whole touched a trust boundary.
