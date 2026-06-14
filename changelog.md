# Changelog

## Unreleased

- Added a C++ destructive-run gate: `--delete` jobs must match the last successful preview before a real run starts, and the preview is cleared after real syncs.
- Centralized endpoint parsing in `EndpointParser` and routed C++, QML, runner, argv building, and remote completion through the same local/SSH/daemon classification.
- Hardened rsync SSH execution by shell-quoting unsafe `-e ssh` command parts and enabling `--protect-args` for modern SSH-backed rsync targets.
- Added safe job ID validation before profile files or scheduler units are read, written, removed, registered, or discovered.
- Replaced macOS daemon-password saves through `security -w` with native Keychain API calls so secrets are no longer exposed as process arguments.
- Made `JobController` easier to test by allowing a `SyncEngine`, stores, scheduler, capabilities, and network-services mode to be injected.
- Split reusable QML UI pieces into `Theme`, `Chip`, `FlatButton`, `Field`, and `DeleteConfirmDialog`, and added validators for numeric/time fields.
- Bounded log and change-list memory growth by keeping a capped in-memory log and a capped displayed change model while retaining logical totals.
- Tightened rsync binary probing to require normal process exit, a parseable version, and timeout cleanup before a binary is treated as usable.
- Clarified documentation and build comments so `ceres_core` is described as non-GUI Qt Core/Network code rather than Qt-Core-only.
