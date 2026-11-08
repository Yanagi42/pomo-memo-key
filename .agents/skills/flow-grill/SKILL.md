---
name: flow-grill
description: >-
  Settle open requirements and trade-offs before planning or building. Entry point
  whenever scope has unconfirmed choices or unresolved unknowns.
---

# Flow: Grill

Settle open product and design choices with the user before any spec or task is written.

## 1. Find what is actually open

Read the relevant code, tests, configs, and `.workflow/decisions.md` first; never ask for facts the repo answers (CONVENTIONS §1). If `.workflow/maps/<slug>.md` exists for this topic, resume from it.

Look for open choices in four areas:
1. **User flow:** how the feature is triggered and used.
2. **Failure behavior:** invalid input, errors, limits.
3. **State and data:** what persists; schema or API changes.
4. **Non-goals:** what is out of scope.

## 2. Ask in short rounds

Ask up to 5 user-owned questions per round, each with options and a recommendation so that "yes" is a complete answer:

```
Q1 - <title>: <1-2 sentences of context; 2-3 options>
My pick: <option>, because <reason>
```

Ask another round only if an answer opens a new real trade-off. Stop once scope is clear or you hit something the chat cannot answer.

## 3. Exit

- **Settled, 1–3 clear tasks:** go to `flow-break`.
- **Settled, multi-task design:** go to `flow-spec`.
- **Cannot settle now** (needs prototype data, outside information, or more sessions): write or update `.workflow/maps/<slug>.md` with `# Map: <goal>`, `## Settled & ruled out`, and `## Open questions` (`- [ ]` items). If an experiment can run now, cut a `spike` task with `flow-break`.

Before leaving: if new domain terms or costly-to-reverse choices were settled, add them to `.workflow/glossary.md` or `.workflow/decisions.md`. Delete the map once its topic becomes a spec or tasks, and commit planning files if the session stops here.
