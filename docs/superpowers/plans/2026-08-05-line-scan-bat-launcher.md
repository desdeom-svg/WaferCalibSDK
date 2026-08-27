# Line Scan BAT Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a double-clickable batch launcher for X-only line-scan calibration and application with result-image and metric output.

**Architecture:** The batch file locates Release sample executables relative to the SDK root, prompts for paths, then invokes calibration and analysis samples. A Chinese Markdown guide documents menu inputs and outputs.

**Tech Stack:** Windows CMD batch, existing C++ Release samples, Markdown.

## Global Constraints

- Do not modify SDK public APIs or JSON schema.
- Use the analysis sample for every validation so correction results and before/after metrics are both produced.

---

### Task 1: Interactive launcher and guide

**Files:**
- Create: `tools/run_line_scan_x_only.bat`
- Create: `docs/线扫X方向畸变离线操作说明.md`

- [ ] Implement menu option 1 to invoke `--calibrate`, then analyze the same image.
- [ ] Implement menu option 2 to apply and analyze an existing JSON.
- [ ] Verify a menu exit path returns cleanly and missing executables receive a clear message.
