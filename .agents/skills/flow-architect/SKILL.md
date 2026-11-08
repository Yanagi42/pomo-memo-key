---
name: flow-architect
description: >-
  Audit high-churn code for duplication, dead code, leaky boundaries, or
  unneeded wrappers. Read-only; reports actionable cleanup items.
disable-model-invocation: true
---

# Flow: Architect

Audit high-churn code for maintenance friction, dead code, or unneeded abstractions. Report only; edit nothing until the user picks items. Prefer deleting code over adding abstractions (CONVENTIONS §8).

## 1. Check high-churn files first

Focus on files that change often rather than quiet legacy code:
```sh
git log --oneline -60
git log --format= --name-only -60 | sort | uniq -c | sort -rn | head -30
```

## 2. Look for real maintenance cost

Check callers before calling anything dead, and report only evidence-backed issues:
- **Duplication:** same logic maintained in multiple places.
- **Dead code:** unused exports, dead flags, single-implementation interfaces, passthrough wrappers, or obsolete compatibility shims.
- **Reinvented basics:** custom code or single-use dependencies replaceable by stdlib or platform features.
- **Tight coupling:** leaked module internals or business logic tangled with I/O without a clean seam.

## 3. Report ranked findings

Rank items as **Fix now**, **Worth doing**, or **Only if it grows**. Skip stylistic preferences that neither shrink code nor make upcoming changes easier:

```markdown
# Architecture review: <area> (<date>)

## Hotspots
<files absorbing repeated changes>

## Findings

### 1. <Concrete symptom> [Fix now]
**Where:** <files>
**Problem:** <evidence of friction or bug risk>
**Fix:** <plain-language change and rough line/dependency delta>

## Leave alone
<code that looks rough but is low-churn and not worth touching>
```

## 4. Record user choices

- **Accepted:** fix directly if it is a small change (CONVENTIONS §3), or cut tasks with `flow-break`.
- **Deferred:** append to `.workflow/technical-debt.md` (`fix-now`, `worth-doing`, `only-if-grows`).
- **Declined:** remove from `.workflow/technical-debt.md` if listed; log in `.workflow/decisions.md` (CONVENTIONS §1) only if future audits would likely repeat the same proposal.
