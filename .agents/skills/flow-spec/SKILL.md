---
name: flow-spec
description: >-
  Write a short technical spec from settled decisions for multi-task work. Run
  flow-grill first if core flows or trade-offs are still open.
---

# Flow: Spec

For multi-task or architecturally open work. Clear 1–3 task work goes straight to `flow-break`.

## 1. Confirm scope is settled

Core user flows, failure handling, and non-goals must be decided. If real trade-offs remain, run a short `flow-grill` round instead of inventing answers. Reuse `.workflow/decisions.md` and `.workflow/glossary.md`.

## 2. Write `.workflow/specs/<effort>.md`

Omit sections like `What it must not do`, `What was considered and rejected`, and `Open questions` when empty—never pad a spec with `None` or `TBD`:

```markdown
# <Title>

**Status:** proposed
**Priority:** normal

## What this is for
The user problem and why it matters.

## What it does
Concrete, observable behavior.

## How it is built
Modules, boundaries, data shapes, interfaces, and failure handling. No line-by-line code.

## What it must not do
Out-of-scope items, with short reasons.

## What was considered and rejected
- **<Option>.** The strongest case for it, and why it lost here.

## How we will know it works
- **W1.** <command or action, and the passing result>
- **W2.** <command or action, and the passing result>

## Open questions
Remaining choices and how they will be settled.
```

At least one `W` check runs the real user entry point end to end. Keep `W` IDs stable.

## 3. Hand off

Get approval, then continue to `flow-break`; one confirmation can cover the spec and the task list. Later changes: edit the spec in place, and once tasks exist, also run `flow-break` amend.
