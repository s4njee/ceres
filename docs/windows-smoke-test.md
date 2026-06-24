# Windows smoke-test checklist

Manual acceptance pass for a Windows build before tagging a release. CI
(`.github/workflows/ci.yml`) covers the headless unit tests and the bundled
rsync path-style contract; this checklist covers what only a real desktop +
network run can prove. Run it against a **staged** build, not a bare `ceres.exe`
from the build tree.

## Prepare a staged build

```powershell
# From an MSYS2 MINGW64 shell (see README) — build first:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Then stage Qt + the MSYS2 rsync/OpenSSH runtime beside ceres.exe:
cmake --build build --target stage-windows-runtime
```

After staging, `build/` should contain `ceres.exe`, `ceres-runner.exe`,
`qt.conf`, the Qt DLLs/plugins/QML imports, and `rsync/bin/` with `rsync.exe`,
`ssh.exe`, `msys-2.0.dll`, and the `ldd`-discovered DLLs.

> The staged `rsync/bin/` ships **no** `etc/fstab`, so the runtime resolves
> drives as `/cygdrive/c/...` (verified — `/c/...` fails). `BinaryLocator`
> detects this bundle as the MSYS flavor; `ArgvBuilder::toRsyncLocalPath` must
> emit `/cygdrive/` for it. Item 1 below is the live check of that contract.

## Checklist

- [ ] **App launches staged.** Double-click `build/ceres.exe` (do **not** add
      `C:\msys64\mingw64\bin` to PATH — that would mask missing bundled DLLs).
      The GUI opens with no `0xC0000135` DLL-not-found error.

- [ ] **Bundled rsync located.** The app reports a found rsync (version shown,
      no "rsync not found" banner). Confirm it resolved the bundled
      `rsync/bin/rsync.exe`, not a system one.

- [ ] **Preview a local sync.** Pick a local source folder (e.g.
      `C:\Users\<you>\Documents\src`) and a local dest. The itemized dry-run
      preview lists files. *(This is the live `/cygdrive` path-conversion check —
      if the preview errors with "change_dir failed / No such file or
      directory", the path style is wrong.)*

- [ ] **Run the local sync.** Execute the previewed job. Files copy, live
      progress + final stats appear, dest matches source.

- [ ] **Run an SSH sync.** Configure a `user@host:/path` endpoint with a key.
      Confirm: rsync execs the **bundled** `ssh.exe` (PATH prefixing works),
      `StrictHostKeyChecking=accept-new` writes `known_hosts` under the
      POSIX-form `HOME`, the `-i <key>` path is converted, and the transfer
      completes non-interactively. *(Covers the still-unverified SSH-on-Windows
      work in `RsyncProcessEngine` / `PathCompleter`.)*

- [ ] **Remote tab-completion.** In an SSH endpoint field, Tab-complete a remote
      path. It lists remote directories over the same bundled ssh.

- [ ] **Cancel an active sync.** Start a large transfer and cancel mid-flight.
      `rsync.exe` **and** its `ssh.exe` grandchild both terminate (Job Object
      teardown), and the result is reported as *interrupted/cancelled* — not
      "rsync exited with code 1".

- [ ] **Register a scheduled job.** Create an interval/daily/weekly schedule.
      Verify with `schtasks /query /tn "Ceres\<jobId>"` that the task exists and
      its action invokes `ceres-runner.exe --job <id>`.

- [ ] **Scheduled run fires.** Either wait for the trigger or
      `schtasks /run /tn "Ceres\<jobId>"`. `ceres-runner.exe` runs the sync
      headless and reads any daemon password from Credential Manager
      (LOCAL_MACHINE persistence).

- [ ] **Delete reconciles the schedule.** Delete the job in the GUI; confirm the
      `schtasks` entry is removed (no orphan task left behind).

- [ ] **Secrets round-trip.** Save a daemon password, restart the app, confirm
      it is read back from Windows Credential Manager.

## Known follow-ups (not blockers for this pass)

- `PathCompleter` local completion splits on `/` only — hand-typed backslash
  paths won't Tab-complete (browse-picked paths use `/`).
- Final installer/archive layout and bundling of license/source material are
  still open (`TODO.md` → Packaging).
