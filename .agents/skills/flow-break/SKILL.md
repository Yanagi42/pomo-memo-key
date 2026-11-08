---
name: flow-break
description: >-
  Split a settled spec or clear scope into small task files with dependencies and
  runnable checks. Use for "break this down", "split into tasks", or to amend a plan.
---

# Flow: Break

Turn settled scope into task files under `.workflow/tasks/<effort>/` (fields: CONVENTIONS §2.1).

## 1. Size tasks

Each task delivers observable behavior in one session and has its own runnable `Check`. Task `01` is the thinnest end-to-end slice through the real entry point. Add a preparatory refactor only when it directly simplifies the requested change.

## 2. Confirm the list once

Show the tasks, their `Blocked by` order, and `Check` commands in brief, together with spec approval if the spec is new.

## 3. Write and validate

Number tasks in dependency order (`01`, `02`, ...); never reuse a number:

```markdown
# <NN>: <Title>

**Effort:** <effort>
**Blocked by:** None | <NN>, <effort>/<NN>
**Files:** <where work happens>
**Read first:** <spec sections, glossary terms, files>
**Check:** `<command>` → <result that means it works>

- [ ] <acceptance point>
```

Add `**Task type:** bugfix | refactor | spike` when it is not a `feature`, and `**Covers:** W1, W2` when the effort has a spec (every spec `W` check is covered by a task or by `flow-close`). `Files` and `Read first` can be omitted when obvious, though `Files` lets `workflow next` flag shared-file overlaps.

When the effort has a spec, set `**Status:** in progress` in `.workflow/specs/<effort>.md`. Run `.workflow/bin/workflow validate`, then commit the plan on the landing branch before cutting task branches.

## 4. Amend

When a task is `blocked` or an assumption changed:
1. Read the task's `## Blocked` and `## Progress`, plus any updated spec or decisions.
2. Edit affected tasks, delete obsolete ones, repair `Blocked by` links, and give new tasks the next unused number (never renumber). Set a `done` spec back to `in progress`.
3. Run `.workflow/bin/workflow validate` and commit the plan on the landing branch.
