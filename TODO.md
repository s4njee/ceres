# TODO

## Windows bundled rsync support

- [ ] Decide the project license before packaging Windows builds.
  - Candidate: GPLv3, which keeps bundled rsync distribution simpler.
- [ ] Add `LICENSE` with the chosen Ceres license.
- [ ] Add `THIRD_PARTY_NOTICES.md` covering bundled components.
- [ ] Choose the Windows rsync distribution to bundle.
  - Prefer a native `rsync.exe` package over WSL for the first Windows release.
  - Track required runtime DLLs and their licenses.
- [x] Fix `EndpointParser` so Windows drive paths classify as Local, not SSH. **(Do
      first — gates every local sync on Windows, and is unit-testable cross-platform.)**
  - Qt's folder picker returns forward-slash paths (`C:/Users/me`), which the old
    heuristic read as an SSH host named `C`. Treat a single-letter host before
    `:/`, `:\`, or end-of-string as a local drive.
- [x] Add a Windows rsync lookup path in `BinaryLocator`.
  - Prefer bundled `rsync.exe` next to the app.
  - Fall back to `PATH` when no bundled copy exists.
- [x] Add Windows local path conversion for the selected rsync runtime.
  - Detect the runtime flavor from the bundled DLL (`cygwin1.dll` → `/cygdrive/c/...`,
    `msys-2.0.dll` → `/c/...`) and convert `C:\Users\name\folder` accordingly.
    (More reliable than `rsync --version`, which doesn't name its runtime.)
  - Convert local endpoints only — never rewrite remote (SSH/daemon) paths.
  - Convert the SSH key path passed to `-i` too: the bundled `ssh.exe` is a
    Cygwin/MSYS binary and expects a converted path.
- [ ] Verify SSH support on Windows.
  - Decide whether to bundle or require `ssh.exe`.
  - Ensure rsync-over-SSH can use configured keys and non-interactive auth.
  - Ensure a writable `HOME` for the bundled Cygwin/OpenSSH `ssh.exe` so
    `known_hosts` works (the engine uses `StrictHostKeyChecking=accept-new`).
- [ ] Add Windows process-tree cancellation.
  - Current Unix process-group termination does not cover child processes on Windows.
- [x] Implement Windows scheduled jobs with Task Scheduler.
  - Register `ceres-runner.exe --job <id>` for interval, daily, and weekly jobs.
  - Remove or reconcile orphaned scheduled tasks when jobs are deleted.
  - Pure `windowsTaskXml` generator is unit-tested; the `schtasks` register/
    remove/query/reconcile wiring still needs a Windows build + runtime check.
- [ ] Package `ceres.exe`, `ceres-runner.exe`, rsync, runtime DLLs, licenses, and notices.
- [ ] Add Windows CI or a repeatable manual smoke-test checklist.
  - Locate bundled rsync.
  - Preview local sync.
  - Run local sync.
  - Run SSH sync.
  - Cancel an active sync.
  - Trigger a scheduled sync.
