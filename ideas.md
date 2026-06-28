# Ceres — Ambitious Ideas

A scratchpad for where Ceres could go. Today it's a polished rsync-over-SSH GUI:
dual-pane browser, a concurrent transfer queue, saved hosts, LAN peer discovery,
and a headless-testable core (`ceres_core`) cleanly separated from the QML shell.
That separation, plus the `SyncEngine` abstraction and the existing
discovery/beacon stack, is a much stronger foundation than "an rsync front-end" —
these ideas lean into it.

**North star:** *the sync tool that's as trustworthy as rsync, as effortless as
AirDrop, and as durable as Time Machine — without a cloud account or a server you
don't control.*

---

## 1. Live continuous sync (folder watching)

Turn one-shot transfers into a **living mirror**: watch a local folder and push
changes within seconds of them happening.

- **Why ambitious:** this is the leap from "file transfer tool" to "sync client"
  (the Syncthing / Dropbox category) — but built on rsync's correctness and your
  own infra, no third-party daemon.
- **Grounds in what exists:** `QFileSystemWatcher` is already wired for
  edit-in-place re-upload (`BrowseController::onEditedFileChanged`). Generalize it:
  recursive watch → debounce/coalesce a burst of changes → enqueue a scoped rsync
  through the existing `TransferManager`. `--partial` resume and the verify toggle
  are already there.
- **Hard parts worth solving:** debounce windows, watch-descriptor exhaustion on
  huge trees (fall back to periodic rescan), and *delete propagation* (a watched
  delete must become `--delete` scoped to that path, gated by the existing
  destructive-run preview).

## 2. Time-machine snapshots (`--link-dest` history)

> **Status: started.** `--link-dest` is wired (`SyncJob.linkDest` → `ArgvBuilder`),
> `core/Snapshot` does the timestamp naming + newest-prior selection (tested), and
> "Snapshot to remote" on a local folder creates `<base>/<timestamp>/` snapshots that
> hardlink from the latest prior one and repoint a `latest` symlink. Snapshots are
> browsable/downloadable for restore, and a **timeline strip** lets you jump between
> snapshots (sticky to the base, active one highlighted). **Remaining:** a
> pruning/retention policy, and one-click "restore to here".

Every sync to a destination becomes a **browsable, restorable point in time**,
sharing unchanged files via hardlinks so 50 snapshots cost ~1 copy of the data.

- **Why ambitious:** real versioned backup with instant restore and near-zero
  storage overhead — the thing people pay Backblaze/Time Machine for — over plain
  SSH to any box with a disk.
- **Grounds in what exists:** `ArgvBuilder` already composes the rsync command;
  add a snapshot layout (`dest/snapshots/2026-06-27T14-03/` + `--link-dest` to the
  previous one) and a `latest` symlink. The browse tab already renders remote
  trees and symlink targets — a **timeline scrubber** that re-points the remote
  pane at a chosen snapshot is a small UI on top.
- **Payoff feature:** "restore this folder to how it looked last Tuesday" = a
  reverse transfer from a snapshot dir.

## 3. Peer-to-peer device mesh (no server)

> **Status: started.** Trust-on-first-use pairing is in: a discovered peer can be
> paired after confirming a shared six-digit verification code (`core/PairingCode`,
> safety-number style), and paired devices persist across restarts in
> `PairedDeviceStore` (keyed by the stable machine id), badged in the sidebar.
> **Remaining:** a background agent that *accepts* incoming sync (so it's truly
> serverless, not "you still configure sshd"), continuous mirroring to a paired
> device, and NAT-traversal/relay for off-LAN peers.

The LAN discovery already finds other Ceres instances. Make them **first-class
sync peers**: pick a peer, pick folders, and they stay mirrored — no SSH config,
no central server.

- **Why ambitious:** this is AirDrop-for-folders that also keeps syncing. It turns
  Ceres from a client into a small distributed system.
- **Grounds in what exists:** `Beacon` / `DiscoveryService` / `PeerModel` already
  advertise and list LAN peers (the sidebar's "ON YOUR NETWORK"). Add a tiny
  per-device agent that accepts authenticated rsync-over-SSH (or a direct rsync
  daemon) with **trust-on-first-use device pairing** (show a short emoji/number
  code on both ends, like Syncthing's device IDs), keys stored in the existing
  `SecretStore`.
- **Stretch:** NAT traversal / relay so two paired devices sync across networks,
  not just the LAN.

## 4. True bidirectional sync with conflict handling

rsync is one-directional. Add a **two-way mode** that detects changes on *both*
sides since the last run and merges them, surfacing genuine conflicts instead of
silently clobbering.

- **Why ambitious:** correct bidirectional sync is genuinely hard (this is what
  Unison is famous for). Doing it with a clear, honest conflict UI would set Ceres
  apart from every rsync GUI.
- **Grounds in what exists:** the headless `ceres_core` is the right home for a
  small **state database** (a SQLite or JSON snapshot of last-known size+mtime+hash
  per path, alongside the existing `AppSettings`/history stores). Two `--itemize`
  dry-runs (local→remote, remote→local) diffed against that snapshot classify each
  path as: only-A-changed (push), only-B-changed (pull), both-changed (conflict).
- **Conflict UI:** reuse the "Ask"-style prompt pattern (keep both / keep newer /
  pick per file) that already exists in spirit from the overwrite-policy work.

## 5. Durable job store + rules engine (bring back scheduling, smarter)

The old `Scheduler`/`ProfileStore` were removed when the app pivoted to ad-hoc
browsing. Reintroduce them as a **persistent automation layer**, not a static job
list:

- **Saved sync jobs** that survive restarts, with the existing per-host profiles.
- **Triggers:** on a schedule (cron, via the OS schedulers the old code already
  modeled — launchd/systemd/Task Scheduler), on folder change (idea #1), on a peer
  appearing (idea #3), on network/AC state.
- **Why ambitious:** "set it and forget it" backups are the difference between a
  toy and something people trust their data to.
- **Grounds in what exists:** the durable-queue mechanics (`TransferManager`'s
  pause/resume/retry, history persistence) are already here; this is mostly a
  trigger/condition layer + a headless agent (idea #9) to run them when the GUI is
  closed.

## 6. Pluggable backends beyond rsync+SSH

`SyncEngine` is already an interface (`RsyncProcessEngine` is one implementation).
Add **alternative backends** behind the same UI:

- **rclone backend** → S3, Backblaze B2, Google Drive, WebDAV, SFTP-native, and
  dozens more, instantly making Ceres a universal sync front-end.
- **age/rclone-crypt wrapper** → end-to-end-encrypted backups to a destination you
  *don't* trust (a cheap VPS, a friend's NAS). You hold the key; the remote sees
  ciphertext.
- **Why ambitious:** decouples Ceres from "must have rsync + SSH on both ends" and
  opens the entire cloud-storage market while keeping the local/SSH path for power
  users.

## 7. Visual pre-flight: the sync as a reviewable changeset

Before a destructive or first-time sync, show a **git-diff-style review** of
exactly what will be created / updated / deleted, with bytes and per-item
include/exclude toggles — then commit.

- **Why ambitious:** removes the #1 fear of sync tools ("did I just delete the
  wrong side?"). Treats a sync like a code review.
- **Grounds in what exists:** `OutputParser` already parses `--itemize-changes`
  into structured `ChangeItem`s, and there's a destructive-run preview gate and a
  `ChangeListModel`. This is largely a richer, groupable, filterable view over data
  you already produce, with a "stage/unstage" layer feeding `--files-from`.

## 8. Background agent + menu-bar / tray presence

Run Ceres **headless** with a lightweight status surface (macOS menu bar, Linux
tray, Windows notification area): live transfers, last-sync times, pause-all,
"sync now". The GUI becomes one optional view onto a daemon.

- **Why ambitious:** real sync clients run in the background; a window you have to
  keep open is a non-starter for "always-on backup".
- **Grounds in what exists:** `ceres_core` is GUI-free by design, and `Notifier`
  already shells out to native notifications. Split the process: a long-lived agent
  owning `TransferManager` + jobs, and a thin UI that attaches over a local socket.

## 9. Bandwidth & power awareness

Make transfers **considerate**: detect metered/Wi-Fi/AC state, auto-throttle or
pause on cellular/battery, ramp up on a fast LAN, and schedule big jobs for idle
hours.

- **Grounds in what exists:** the rate limit (`--bwlimit`) and pause/resume plumbing
  already exist; this adds a policy layer that flips them based on
  `QNetworkInformation` (metered/reachability) and power state.

## 10. Intelligence layer

- **Natural-language transfers:** "back up my photos from last month to the NAS" →
  resolve folder + date filter + saved host → a previewed job. The
  capability/endpoint plumbing to execute it already exists.
- **Anomaly flags:** "this sync wants to delete 4,300 files / move 80 GB — unusual
  vs. history." The transfer history store is already there to baseline against.
- **Smart excludes:** offer to skip `node_modules/`, caches, `.git` objects,
  thumbnails — learned from what the user repeatedly excludes.

---

## Suggested arc

A credible path that compounds rather than scattering:

1. **Snapshots (#2)** — highest value-to-effort; mostly `ArgvBuilder` + a timeline
   view, and it makes Ceres a *backup* tool, not just a copier.
2. **Live watch (#1)** + **durable jobs/triggers (#5)** + **agent (#8)** — together
   these turn it into an always-on sync client.
3. **Peer mesh (#3)** — the differentiator; builds directly on the discovery stack.
4. **Bidirectional (#4)** and **pluggable backends (#6)** — the deep, market-opening
   bets once the foundation is durable.

Everything here rides on two existing strengths worth protecting: the
**headless, tested `ceres_core`** (so logic stays verifiable as features grow) and
the **`SyncEngine` seam** (so new backends and modes don't fork the UI).
