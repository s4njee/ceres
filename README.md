# Ceres

A sleek, cross-platform GUI for **rsync**, built with native C++ / Qt 6 (Qt Quick).

It wraps the real `rsync` binary (not librsync) so it speaks the genuine rsync wire
protocol — local, SSH (`user@host:`), and daemon (`rsync://`) targets all work. The aim
is a calm, opinionated front-end with a real **before-you-sync preview**, first-class live
progress, and safe defaults — over an advanced tier for full flag control later.

> **Status: Milestone 1 (engine spike).** Boots a QML window, runs a local `--dry-run`
> through `QProcess`, and streams parsed `--itemize-changes` output into the UI. This
> proves the process + parser core before the rest of the UX is built.

## Architecture

```
QML (Qt Quick Controls 2, Material)        — UI shell
  └─ JobController (QObject)               — exposed to QML
       ├─ ChangeListModel (QAbstractListModel)
       └─ SyncEngine (abstract)            — the portability seam
            └─ RsyncProcessEngine          — QProcess + the real rsync binary
                 ├─ ArgvBuilder            — SyncJob -> argv (capability-aware)
                 ├─ OutputParser           — progress2 / itemize / stats / log
                 └─ BinaryLocator          — finds rsync, detects its capabilities
```

`ceres_core` (everything below the QML layer) is a Qt-Core-only static library with no
GUI/QML dependency, so the parser and arg builder are unit-tested headless and a future
Windows engine (cwRsync / WSL) can reuse it behind `SyncEngine`.

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

The tests cover the two pure pieces that are easy to get subtly wrong: `OutputParser`
(itemize parsing, `\r`/`\n` chunk-boundary handling, progress2 with/without `to-chk`) and
`ArgvBuilder` (capability gating, filter ordering, delete/dry-run, SRC/DEST placement).

## Roadmap

See the design doc for the full plan. Next milestones: local sync MVP (real runs, JSON
profiles) → SSH + daemon → Advanced tier (KDDockWidgets) → scheduling → packaging
(signed macOS DMG, Linux AppImage) → Windows (cwRsync / WSL engines).
