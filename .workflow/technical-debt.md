# Technical Debt

Track code friction worth fixing later, backed by concrete evidence. Remove entries in the commit that fixes them. If the user declines a fix, delete the entry; log in `decisions.md` (`Decided by: user`) only if future agents are likely to re-propose the same cleanup.

## Format

Add `## <Concrete symptom>` headings under `## Entries`:

```markdown
## Order total computed in three places

- **Where:** <files or modules>
- **Added:** <YYYY-MM-DD>
- **Priority:** fix-now | worth-doing | only-if-grows
- **Problem:** <what is wrong, with evidence>
- **Solution:** <plain-language fix and rough cost>
```

## Entries

<!-- Add yours below. Delete this comment. -->

## Media panel play/pause glyph starts in the wrong direction

- **Where:** `src/main.c:1095` (glyph), `src/main.c:1502-1507` (toggle and transient label)
- **Added:** 2026-11-08
- **Priority:** only-if-grows
- **Problem:** `PLAYING` / `PAUSED` after a `PLAY/PAUSE` press is the intended W5 transient feedback (task 15 spec), so the label itself is not a defect. The residual ambiguity is that `ui_media_playing` starts true and flips on every press without any host feedback: against a host that is currently paused, the first press resumes playback while the panel reads `PAUSED`, and the glyph shows pause while the host plays. Evidence: independent review of task `pose-driven-ui/12`, 2026-11-08.
- **Solution:** Start the flag neutral/false or label the first press neutrally. Cosmetic, single-file; revisit only if a host reports a confusing glyph.

## Side media panel shows the pairing identity only in UART

- **Where:** `src/main.c:1101-1103`, `src/main.c:681`, `src/main.c:1483`
- **Added:** 2026-11-08
- **Priority:** only-if-grows
- **Problem:** The panel renders `CONNECTED` / `OFFLINE` and `BLUETOOTH DISCONNECTED`; the advertising name `POMO_MEMO_HID` appears only in the UART log. Task 12's checklist wording asked the panel to show the BLE pairing/device name, and the user accepted the current display on the real board on 2026-11-08.
- **Solution:** Append the advertising name to the offline feedback label when disconnected (e.g. `PAIR POMO_MEMO_HID`). Revisit only if the pairing hint is wanted on screen.
