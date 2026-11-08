# Workflow Conventions

Shared reference for the `flow-*` skills. Read only the section the current step needs. `.workflow/standards.md` overrides this file.

## 1. Ask or decide

Search code, tests, configs, docs, and git history before asking. Never invent product requirements.
- **Reversible details:** pick a sensible default and move on.
- **User-owned choices** (goals, scope, risk, costly trade-offs): ask in batches of up to 5, most impactful first, each with options and `My pick: <option>, because <reason>`.
- **Logging:** add a choice to `decisions.md` only when it is costly to reverse and a real alternative was rejected.

## 2. File layout

```
AGENTS.md                        workflow block points here
.agents/skills/<name>/SKILL.md   skill steps
.agents/agents/<name>.md         review agents
.workflow/
  bin/workflow                   project CLI
  bin/workflow-core.py           validate and next (Python 3 stdlib)
  CONVENTIONS.md                 this file
  STYLE.md                       writing and reporting rules
  standards.md                   project commands, layout, and Landing mode
  glossary.md                    project terms
  decisions.md                   append-only decision log
  technical-debt.md              tracked code debt
  specs/<effort>.md              spec for multi-task efforts
  tasks/<effort>/<NN>-<slug>.md  open task files
  maps/<slug>.md                 notes on unknowns that span sessions
  done/<effort>/                 archived tasks and closeout.md
  worktrees/<effort>-<NN>/       parallel task worktrees (gitignored)
```

- Run `.workflow/bin/workflow <command>` from the repo root. Keep `standards.md` under one screen.
- An `<effort>` groups `tasks/<effort>/`, `done/<effort>/`, and an optional `specs/<effort>.md`. A spec has `**Status:** proposed | in progress | done`, `**Priority:** critical | high | normal | low`, and optionally `**Integration branch:**` (§7). Edit specs in place when facts change.
- `decisions.md` is append-only; its header defines the entry format. `decisions.md` and `glossary.md` use git union merge.
- Run `git status --short` before editing. Never touch or stage files that were already dirty without asking; stage with `git add -- <paths>`.

### 2.1 Task files

`workflow validate` checks these fields. The template is in `flow-break`.
- **Path and heading:** `tasks/<effort>/<NN>-<slug>.md` with `# <NN>: <Title>` (at least 2 digits); cite as `<effort>/<NN>`.
- **Required:** `Effort` (slug `[a-z0-9]+(-[a-z0-9]+)*`), `Blocked by` (`None`, or a comma list of `NN` / `<effort>/NN`), `Check` (command and expected result, or a `## Check` section).
- **Optional:** `Delivers`, `Files`, `Read first`, `- [ ]` items, `Task type` (`feature` by default, `bugfix`, `refactor`, `spike`), `Covers` (spec `W` IDs), `Base commit`, `Base branch` (an unlanded blocker's branch to build on), `Status` (`active | paused | blocked`).
- **Proof by task type**, recorded as `Check before:` / `Check after:` in `## Progress`:
  - `bugfix`: fails before, passes after.
  - `refactor`: passes before and after.
  - `feature`: passes after, plus one run through the real entry point.
  - `spike`: runs a bounded experiment to answer an open question; keeps throwaway code off main, and records `## Findings` instead of `Check after:` when archived.
- Tests using mocks verify only isolated logic. Check behavior that depends on external services or real runtimes against those real environments, and note what remains mocked.
- **Archive:** tick the `- [x]` items, drop `Status`, move the file to `done/<effort>/`, and run `workflow validate`.

## 3. Sizing and routing

Size by blast radius and ease of revert, after reading the direct callers:
- **Question:** answer directly.
- **Small change or bugfix:** edit, run tests and a real-path check, then commit (§6 for bugs, `security-reviewer` at trust boundaries). No task file and no `flow-verify`. Log in `decisions.md` only if a temporary shortcut was taken; open a `bugfix` task if the root cause remains.
- **1–3 clear tasks:** `flow-break` → `flow-implement`. No spec and no `flow-close`.
- **Multi-task or contract/schema/boundary change:** `flow-spec` → `flow-break` → `flow-implement` → `flow-close`.
- **Unclear scope or open trade-offs (any size):** run `flow-grill` first, then follow the matching path above.

| Situation | Skill |
|-----------|-------|
| Start, resume, or lost context | `flow-start` |
| Unclear requirements or open trade-offs | `flow-grill` |
| Multi-task design settled; needs a spec | `flow-spec` |
| Clear scope or settled spec; needs task files | `flow-break` |
| Ready task or queue to build | `flow-implement` |
| Review changes before landing | `flow-verify` |
| Last task of a spec effort landed | `flow-close` |
| A session hit avoidable friction | `flow-retro` |
| High change friction or code debt audit | `flow-architect` |

**Unknowns.** `flow-grill` settles what the code and the user can answer in chat. An unknown that needs an experiment becomes a `spike` task. One that spans sessions goes to `.workflow/maps/<slug>.md` (`workflow next` lists open maps), which is deleted once it turns into a spec or tasks.

**Replan.** When a plan assumption fails, set `Status: blocked`, add `## Blocked` (assumed vs. observed, decision needed), update the spec or `decisions.md`, and run `flow-break` amend. Fix the plan instead of coding around it.

## 4. State and handoff

Plan in one session when practical; implement from committed files. Resume `paused` tasks before starting new ones. A new task under a `done` spec returns it to `in progress`.

**Handoff.** Before pausing or leaving an unfinished task, set `Status: paused`, record verified work, failed attempts, the branch or worktree, and the next step under `## Progress`, then commit. If working without a task file, record the same state in `.workflow/HANDOFF.md` instead (left uncommitted; `flow-start` loads and removes it on resume).

## 5. Review

Never review a change in the context that wrote it. `flow-verify` runs the reviewers (`.agents/agents/`):
- `workflow-reviewer` checks requirements and code quality. One fresh subagent per change; split into two only for large cross-module diffs.
- `security-reviewer` runs only when the change touches auth, secrets, untrusted external input, network exposure, or permissions.
- Findings are `blocking` or `nonblocking`. Only the user can waive a `blocking` finding, recorded in `decisions.md` with `Decided by: user`.
- **Review-fix cap:** cap review-fix cycles at 2 rounds. If blocking findings remain after two rounds, stop: do not continue looping. Escalate to the user with the findings and evidence. The user decides whether to waive the finding (recorded in `decisions.md` with `Decided by: user`), provide direction, or mark the task blocked/paused for replanning.

## 6. Fixing bugs

1. **Reproduce** with the exact command or input. If it won't reproduce, report that and stop.
2. **Pin** it with a failing check or test.
3. **State the root cause** in one or two lines (`## Progress` or the commit message).
4. **Fix the cause** and any sibling callers with the same flaw.
5. **Verify** the check passes and the real path works. After two failed fix attempts, stop guessing: re-check the root cause, report what you observed, or open a `spike` if an isolated experiment is needed.

## 7. Branches and parallel work

`Landing:` in `standards.md` selects one mode:
- `direct`: commit on the current branch.
- `local-merge`: cut `<effort>/<NN>-<slug>` (or `small/<slug>`, `hotfix/<slug>`) from the landing branch, merge locally after verification, then delete the branch and its worktree.
- `pr`: push the task branch and land through a pull request; `git fetch` first.

Rules for every mode:
- Commit planning docs on the landing branch before cutting task branches.
- Before landing, check whether the landing branch moved (`git merge-base <landing branch> HEAD`). If it did, rebase and rerun the checks.
- **Integration branch:** when tasks cannot land on main one at a time, the spec sets `**Integration branch:** <effort>/integration`. Tasks branch from and merge into it; `flow-close` lands it on main.
- **Parallel tasks:** one worktree per task (`git worktree add .workflow/worktrees/<effort>-<NN> -b <effort>/<NN>-<slug> <landing branch>`). The `.workflow/worktrees/` directory is ignored by Git (via `.workflow/.gitignore`). Never share a worktree, ports, or test databases. Subagents run `Check` before returning; the main session confirms `Check`, the real-path run, and `flow-verify` before landing. Remove the worktree (`git worktree remove .workflow/worktrees/<effort>-<NN>`) after landing.

## 8. Keep implementations lean

Report every real bug reachable in supported use; keep the fix minimal.
- **Reuse first:** check repo code → stdlib → platform → installed dependencies before writing new code.
- **Threat model:** assume a trusted local user unless `standards.md` or an exposed interface accepts untrusted input. Skip guards for impossible local states.
- **No dead weight:** no speculative caching, guards for impossible states, single-use interfaces, passthrough wrappers, or unrequested flags and compatibility layers.
- **Checks:** skip rerunning checks when inputs haven't changed; never skip the task `Check` or the real-path run before landing.
