---
name: verify
summary: Build and drive the native Qt workbench for runtime verification.
---

## VsDB runtime verification

1. Configure with the installed Qt kit:
   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.9.2\mingw_64"
   cmake --build build
   ```
2. Launch `build\VsDB.exe` with `C:\Qt\6.9.2\mingw_64\bin` and `C:\Qt\Tools\mingw1310_64\bin` prepended to `PATH`.
3. Drive the window: execute the seeded query with `Ctrl+Enter`, create a query tab with `Ctrl+N`, use the explorer search, and check the Explorer/Inspector/Results/Status regions.
4. Capture a native desktop screenshot for review, then stop the process after the smoke test.

Qt test executables also require the same two runtime directories on `PATH`; use `QT_QPA_PLATFORM=offscreen` for headless execution.
