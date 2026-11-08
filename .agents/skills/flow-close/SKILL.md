---
name: flow-close
description: >-
  Wrap up a spec effort after its last task lands: run the spec checks and the main
  user flow, get user sign-off, write the closeout note, and mark the spec done.
argument-hint: "<effort>"
---

# Flow: Close

Only for efforts with a spec. A spec-less effort ends when its last task lands.

## 1. Check readiness

`.workflow/specs/<effort>.md` exists, `.workflow/tasks/<effort>/` has no open tasks, and every task has landed. If `closeout.md` already exists (reopened effort), review only tasks added since the last close.

## 2. Run checks on what ships

On the landing branch head, built the way users get it, run every `W` check under `## How we will know it works` and walk the main user flow end to end. Run `flow-verify` in effort close mode. Small fixes may land with the close; larger gaps go through `flow-break` amend.

## 3. Get sign-off

Show the check results, the user-flow run, and review status. If the same friction recurred across tasks, suggest `flow-retro` in one line. The spec stays `in progress` until the user explicitly accepts.

## 4. Record and mark done

Write `.workflow/done/<effort>/closeout.md`. On a reopened effort, append a `## Reopened` section with a new `**Accepted:**` line instead:

```markdown
# Closeout: <effort>

**Accepted:** <YYYY-MM-DD> by <who accepted>

## Shipped
<what shipped, with W-check results>

## Key decisions & plan changes
<notable decisions or plan changes, or None>

## Remaining debt & follow-ups
<open technical-debt.md items or follow-up tasks, or None>
```

Set `**Status:** done` in the spec, run `.workflow/bin/workflow validate`, commit `closeout.md` together with the spec, and land, including the integration branch if the effort has one (CONVENTIONS §7).
