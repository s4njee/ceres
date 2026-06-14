# Ceres

A sleek, cross-platform GUI for **rsync**, built with native C++ / Qt 6 (Qt Quick).

It wraps the real `rsync` binary (not librsync) so it speaks the genuine rsync wire
protocol — local, SSH (`user@host:`), and daemon (`rsync://`) targets all work. The aim
is a calm, opinionated front-end with a real **before-you-sync preview**, first-class live
progress, and safe defaults — over an advanced tier for full flag control later.

> **Status: prototype.** Boots a QML window, previews and runs rsync jobs through
> `QProcess`, stores JSON profiles, can register launchd/systemd schedules, and streams
> parsed `--itemize-changes` output into the UI.

## Architecture

```
QML (Qt Quick Controls 2, Basic)           — UI shell
  └─ JobController (QObject)               — exposed to QML
       ├─ ChangeListModel (QAbstractListModel)
       ├─ ProfileStore / SecretStore       — JSON profiles + OS keychain/libsecret
       ├─ Scheduler / DiscoveryService     — launchd/systemd + LAN beacons
       └─ SyncEngine (abstract)            — the portability seam
            └─ RsyncProcessEngine          — QProcess + the real rsync binary
                 ├─ ArgvBuilder            — SyncJob -> argv (capability-aware)
                 ├─ OutputParser           — progress2 / itemize / stats / log
                 └─ BinaryLocator          — finds rsync, detects its capabilities
```

`ceres_core` (everything below the QML layer) is a non-GUI Qt Core/Network static
library with no Quick/QML dependency, so parser, arg builder, profile, scheduler,
and controller behavior are unit-tested headless. A future Windows engine
(cwRsync / WSL) can reuse it behind `SyncEngine`.

## Prerequisites

- **Qt 6.5+** — `brew install qt`
- **CMake 3.21+** — `brew install cmake`
- **A modern GNU rsync (recommended).** macOS now ships **openrsync** (2.6.9-compatible),
  which lacks `--info=progress2` / `--outbuf` / `--no-inc-recursive`. Ceres detects this
  and degrades gracefully (you'll still get the itemized preview, just no live progress
  bar), but for the full experience install GNU rsync:

  ```sh
  brew install rsync   # /opt/homebrew/bin/rsync — picked up automatically
  ```

## Build & run

```sh
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/ceres            # the GUI
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

The tests cover the pieces that are easy to get subtly wrong: `OutputParser`
(itemize parsing, `\r`/`\n` chunk-boundary handling, progress2 with/without `to-chk`),
`ArgvBuilder`/`EndpointParser` (capability gating, SSH/daemon detection, quoting,
delete/dry-run, SRC/DEST placement), storage/scheduler safety, binary probing,
path completion, discovery beacons, and the controller's destructive-run gate.

## Roadmap

See the design doc for the full plan. Next milestones: harden SSH/daemon flows,
polish the preview UX, expand the advanced options tier, package signed macOS/Linux
builds, and add Windows support through cwRsync or WSL engines.
