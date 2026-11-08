# 08: Correct rotated spatial UI and control layout

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/07
**Files:** `src/main.c`, UI-facing project docs and evidence
**Read first:** Spec `pose-driven-ui.md`; screenshots from 2026-11-06; `.workflow/done/pose-driven-ui/06-acceptance-and-docs.md`; SDK LVGL v8 rotation and FT6146 input paths
**Task type:** bugfix
**Covers:** W2
**Check:** Build and flash the real board; inspect upright, left, right, and flat. Each page must fill the panel with no clipping or overlap; tap one visible control per page and observe its application command.
- [ ] Use one coordinate model for LVGL logical resolution, rotated LCD flush, panel size/offsets, and FT6146 points.
- [ ] Lay out timer controls with readable state and visible margins.
- [ ] Lay out left/right media pages in the lock-screen style with all controls and a neutral “媒体控制” title; Bluetooth state is added in task 12.
- [ ] Lay out recorder controls and TF status without clipping; reserve room for live and final recording duration.
- [ ] Make visible-page controls emit commands; action behavior is completed in tasks 10–12.
- [ ] Update screenshot/evidence only from observed board behavior.
