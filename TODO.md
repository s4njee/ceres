# TODO

## Windows

- [x] Implement `SecretStore` on Windows with Credential Manager.
  - `CredWriteW`/`CredReadW`/`CredDeleteW` (`advapi32`), persisted
    `LOCAL_MACHINE` so the scheduled `ceres-runner` can read daemon passwords.
  - Round-trip covered by `tst_secretstore` (skips where no backend exists).

## Windows bundled rsync support

- [x] Decide the project license before packaging Windows builds.
  - Chose GPLv3, which keeps bundled rsync distribution simpler.
- [x] Add `LICENSE` with the chosen Ceres license. (Canonical GPLv3 text.)
- [x] Add `THIRD_PARTY_NOTICES.md` covering bundled components.
  - Skeleton in place (Qt, rsync, OpenSSH, Cygwin/MSYS runtime). Version/source
    fields are marked TODO — fill them in once the exact binaries are picked.
- [x] Choose the Windows rsync distribution to bundle.
  - Use MSYS2 `msys` packages for the first Windows release: `rsync` plus
    `openssh`, keeping `rsync.exe`, `ssh.exe`, `msys-2.0.dll`, and their runtime
    DLL dependencies together in the bundled `rsync/bin/` directory.
  - Track required runtime DLLs and their licenses.
  - Then fill in the version/source `TODO`s in `THIRD_PARTY_NOTICES.md`.
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
- [x] Add Windows process-tree cancellation.
  - `RsyncProcessEngine` assigns the child to a kill-on-close Job Object on
    start and calls `TerminateJobObject` in `cancel()`, so rsync and its ssh
    grandchild go down together. Compiles cross-platform; still needs a Windows
    build + runtime check (esp. that the ssh child lands in the job).
- [x] Implement Windows scheduled jobs with Task Scheduler.
  - Register `ceres-runner.exe --job <id>` for interval, daily, and weekly jobs.
  - Remove or reconcile orphaned scheduled tasks when jobs are deleted.
  - Pure `windowsTaskXml` generator is unit-tested; the `schtasks` register/
    remove/query/reconcile wiring still needs a Windows build + runtime check.
- [ ] Package `ceres.exe`, `ceres-runner.exe`, rsync, runtime DLLs, licenses, and notices.
  - [x] Add `stage-msys2-rsync` target to copy MSYS2 `rsync.exe`, `ssh.exe`,
    `msys-2.0.dll`, and `ldd`-discovered DLLs into `rsync/bin/` beside
    `ceres.exe`.
  - [x] Add `stage-qt-runtime` / `stage-windows-runtime` targets to copy Qt DLLs,
    plugins, QML imports, and `qt.conf` beside `ceres.exe`.
  - [ ] Add final installer/archive layout.
  - [ ] Copy license texts and corresponding source/written-offer material into
    the final package.
- [x] Add Windows CI or a repeatable manual smoke-test checklist.
  - [x] Cross-platform CI (`.github/workflows/ci.yml`): Linux/macOS/Windows
    build + ctest; the Windows job also stages the bundled rsync and asserts it
    resolves `/cygdrive` paths (guards the path-style contract).
  - [x] Manual GUI/network checklist (`docs/windows-smoke-test.md`): locate
    bundled rsync, preview/run local sync, run SSH sync, cancel an active sync,
    register + trigger + reconcile a scheduled sync, secrets round-trip.
