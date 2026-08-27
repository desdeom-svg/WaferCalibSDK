# Line Scan Portable Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a portable line-scan X-only tool folder that runs after being copied to another Windows PC.

**Architecture:** The launcher resolves sample EXEs beside itself first and falls back to the development build tree. The packager copies the two EXEs, SDK, OpenCV 4.1.0 VC15 runtime and local VC runtime DLLs into one output directory.

**Tech Stack:** Windows CMD batch, OpenCV 4.1.0 VC15 runtime, MSVC 2022 runtime.

## Global Constraints

- Do not alter calibration algorithms or JSON schema.
- Package only x64 Release executables and the DLLs reported by `dumpbin /dependents`.

---

### Task 1: Portable launcher and packaging script

**Files:**
- Modify: `tools/run_line_scan_x_only.bat`
- Create: `tools/package_line_scan_x_only.bat`
- Modify: `docs/线扫X方向畸变离线操作说明.md`

- [ ] Prefer EXEs next to the launcher, then retain project-tree fallback.
- [ ] Copy verified dependencies to `output/LineScanXOnlyTool`.
- [ ] Smoke test the packaged launcher with menu option 3.
