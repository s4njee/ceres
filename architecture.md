# Ceres — Architecture

Ceres is a Qt 6 / QML desktop GUI over `rsync`-on-`ssh`. It does not reimplement
the transfer protocol: it builds an `rsync` argv, runs the real binary as a child
process, and parses its output into live UI state. This document describes how the
pieces fit together and the decisions that shape them.

## Design goals

- **Lean on `rsync`.** Delta transfer, resume (`--partial`), compression, and
  hardlink snapshots (`--link-dest`) come from rsync itself. Ceres orchestrates and
  visualizes; it never moves bytes on its own.
- **Keep the engine headless and testable.** Everything below the QML layer is a
  GUI-free static library so the parser, arg builder, queue, and controllers can be
  unit-tested without a display, a network, or even a real rsync binary.
- **Never block the GUI thread.** Filesystem walks, directory listings, and remote
  SSH calls run off-thread or asynchronously; results hop back to the GUI thread to
  mutate models.
- **Secrets stay in the OS keychain.** Passwords never land in JSON stores, log
  output, or the exportable config bundle.

## The two-target split

The single most important structural decision is the split between the engine
library and the GUI shell (see [CMakeLists.txt](CMakeLists.txt)):

| Target | Links | Contains |
| --- | --- | --- |
| **`ceres_core`** (static lib) | `Qt6::Core`, `Qt6::Network` only | engine, controllers, models, app services, core utilities |
| **`ceres`** (executable) | `ceres_core` + `Qt6::Gui/Qml/Quick/QuickControls2/Widgets` | `main.cpp`, the QML UI, embedded icons |

`ceres_core` has **no Quick/QML/Widgets dependency**. That is what lets the unit
tests run headless and what would let a future non-rsync engine (cwRsync/WSL on
Windows) slot in behind the `SyncEngine` seam. The C++ models are `QAbstractListModel`
subclasses (Core), not QML types — `main.cpp` registers them as *uncreatable* QML
types and hands instances to QML via context properties.

## Layered component map

```
QML (Qt Quick Controls 2, "Basic" style + custom Theme.qml)        — UI shell
  │   context properties: controller, completer, sessions, transfers, notifier
  │
  ├─ JobController                  — the "Sync" tab: preview/run a configured sync,
  │    │                              saved hosts, LAN peers, destructive-run gate
  │    ├─ ChangeListModel           — itemized dry-run / live changes
  │    ├─ SshHostListModel          — saved SSH hosts sidebar
  │    ├─ PeerModel ← DiscoveryService ← Beacon   — LAN discovery (multicast JSON)
  │    └─ PairedDeviceStore         — TOFU mesh pairing (6-digit code)
  │
  ├─ BrowseSessions                 — tab strip; owns N independent browse sessions
  │    └─ BrowseController (per session)   — the dual-pane (local ⇆ remote) browser
  │         ├─ FileListModel ×2     — local pane (QDir) + remote pane (RemoteFs)
  │         ├─ RemoteFs             — list/mkdir/rm/rename over ssh (POSIX sh script)
  │         ├─ PathCompleter        — path tab-completion (local sync, remote async)
  │         └─ → TransferManager    — hands downloads/uploads off as ad-hoc SyncJobs
  │
  ├─ TransferManager                — concurrency-capped queue of ad-hoc transfers
  │    └─ TransfersModel            — one row per transfer + a per-file progress tree
  │
  └─ Notifier                       — native desktop notifications (osascript/notify-send)

Engine seam (shared by JobController and TransferManager)
  SyncEngine (abstract)
    └─ RsyncProcessEngine          — QProcess + the real rsync binary
         ├─ ArgvBuilder            — SyncJob → argv (capability-aware)
         ├─ OutputParser           — --info=progress2 / --itemize-changes / --stats
         └─ BinaryLocator          — finds rsync, probes its capabilities

Core utilities (no Qt GUI)
  SecretStore (keychain: macOS Security / Linux secret-tool / Windows CredMan),
  SshHostStore, SshKnownHosts, SshConfigImport, Snapshot, PairingCode,
  ConfigBundle, Endpoint, Format, AppSettings, SshCommand (shared ssh plumbing)
```

## Process & threading model

- **One rsync process per transfer.** Each `SyncJob` becomes a single
  `rsync -a … <src> <dst>` invocation that recursively handles the whole tree in one
  streaming session over one SSH connection. Ceres does **not** exec rsync per file.
- **Per-transfer engine instances.** `TransferManager` admits at most
  `maxConcurrent` transfers and queues the rest; each admitted transfer gets its own
  `SyncEngine` (rsync is stateless per run, so independent engines are safe). Engines
  are created via an injectable `EngineFactory` — production yields
  `RsyncProcessEngine`, tests yield fakes — so the queue logic is unit-tested with no
  real processes.
- **Progress streaming.** `RsyncProcessEngine` runs rsync with `--outbuf=L` (line
  buffering) so `OutputParser` turns each line into aggregate progress, per-file
  progress, itemized changes, or the final `--stats` summary — pushed into the models
  live.
- **Off-thread filesystem work.** The local upload pre-walk
  (`BrowseController::walkLocal`, via `std::filesystem::recursive_directory_iterator`)
  and the local directory listing (`localRefresh`) run on `QThreadPool`; results are
  applied on the GUI thread via `QMetaObject::invokeMethod`, with stale results
  dropped when the user has navigated on.
- **Async SSH.** `RemoteFs` and `PathCompleter` run their ssh calls asynchronously
  (QProcess + signals), so directory navigation never blocks the UI.
- **The SSH_ASKPASS hook.** When rsync's child `ssh` needs a password, it re-execs
  the Ceres binary; `main()` detects `CERES_ASKPASS` in the environment, prints the
  password (passed in-env, never on argv), and exits before booting the GUI. This
  keeps passwords off every process's command line.

## Key data flows

**Browse → transfer.** `BrowseController` lists local (QDir, off-thread) and remote
(`RemoteFs` over ssh) panes into two `FileListModel`s. A download/upload becomes an
ad-hoc `SyncJob` handed to the shared `TransferManager`; the controller never runs
rsync itself. For uploads it also seeds `TransfersModel` with the file list from a
local walk so the whole tree shows at 0% immediately rather than trickling in.

**Transfer queue → UI.** `TransferManager` is the sole writer of `TransfersModel`:
it adds a `Queued` row on enqueue, flips to `Active` when `pump()` starts an engine,
and to `Done`/`Failed`/`Cancelled` from the engine's finished/failed handlers. It
also exposes `pauseAll`/`resumeAll` and a `pausedCount` for the tray.

**Sync tab safety gate.** `JobController` requires a matching preview (dry-run)
fingerprint — a SHA-256 of the job's sync-relevant fields — before a real run when
`--delete` is enabled, preventing accidental mass-deletion if the source/destination
changed after previewing.

**Snapshots.** "Snapshot to remote" backs a local folder into a timestamped remote
dir (`Snapshot` naming), hardlinking unchanged files from the previous snapshot via
`--link-dest` so each snapshot costs only its delta, then repoints a `latest`
symlink. Snapshots are plain browsable directories; the browse tab shows a timeline.

**Mesh.** `Beacon` multicasts a JSON presence packet; `DiscoveryService` collects
peers into `PeerModel`. Pairing is trust-on-first-use: `PairingCode` derives a
6-digit code (SHA-256) shown on both devices to confirm, after which
`PairedDeviceStore` remembers the device by stable machine id.

**Config portability.** `ConfigBundle` exports saved hosts, paired devices, and
bookmarks to one portable JSON file and merge-imports it on another machine.
**Secrets are deliberately excluded** — they live only in `SecretStore` (the OS
keychain) — which is exactly why the bundle is safe to share.

## Performance notes

The transfers UI is the hot path during a large, deeply-nested transfer, and several
choices target it specifically:

- **`TransfersModel` per-file updates are O(1).** Each transfer row keeps a
  `QHash<path,int>` index into its file list plus a cached `leafCount`, so per-file
  progress updates and folder-ancestor synthesis don't linearly scan the list
  (which made a whole transfer O(N²)).
- **The rendered file tree is capped** (2000 rows) so a transfer of tens of
  thousands of files stays responsive; the count badge still shows the true total.
- **The per-file tree is built only while a row is expanded.** It lives behind a
  QML `Loader` keyed on the row's expanded state, so the continuous per-file progress
  ticks don't rebuild thousands of QML items for collapsed rows (the common case).
- **Local listing and the upload walk are off the GUI thread** (see threading
  model above), and the walk uses `std::filesystem`'s cached entry type to avoid a
  `stat()` per entry.

## Testing

`ceres_core` is exercised by a suite of headless unit tests (`tests/`,
`ctest`): the output parser, argv builder, models, transfer-manager queue,
controllers, remote-fs script building, path completer, config bundle, pairing, and
beacon — none of which need a display or a live rsync/ssh. Engine and store
dependencies are injected so tests run with fakes.

## Packaging

CPack produces a macOS `.dmg` (`.app` bundle with the Qt runtime deployed via
`qt_generate_deploy_qml_app_script`), a Windows NSIS installer (with MSYS2 rsync/ssh
staged beside the exe), and a Linux AppImage/TGZ. See the README "Packaging" section
and [.github/workflows/release.yml](.github/workflows/release.yml).
