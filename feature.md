# Feature checklist — SCP/SFTP client

A working checklist of capabilities a modern SCP/SFTP file-transfer client is
expected to have. FTP/FTPS is intentionally out of scope — it's rare in 2026 and
not worth the surface area.

Legend: `[x]` already in Ceres · `[ ]` not yet · _(italic notes for caveats)_

---

## Connection & authentication
- [x] Connect to an SSH host by `user@host` (or bare `host`)
- [x] Custom SSH port
- [x] Public-key authentication (agent / default key)
- [x] Explicit private-key file selection
- [x] Password / keyboard-interactive authentication
- [x] Save hosts for quick reconnect
- [x] Store passwords in the OS keychain (macOS Keychain / Windows Credential Manager)
- [t] Per-host saved username, key, and port as a connection profile
- [ ] Encrypted-key passphrase prompt (separate from login password)
- [ ] SSH agent forwarding toggle
- [ ] Jump host / `ProxyJump` (bastion) support
- [x] Connection timeout / keepalive configuration
- [t] Reconnect on dropped connection
- [t] Import hosts from `~/.ssh/config`

## Host-key / trust management
- [x] Accept and pin new host keys (`accept-new`)
- [x] Detect changed host key and prompt before replacing
- [ ] Show host-key fingerprint on first connect for verification
- [ ] Manage / view known_hosts entries in-app

## Remote file browsing
- [x] List a remote directory (name, size, modified date, type)
- [x] Navigate into folders / up to parent
- [x] Jump to an arbitrary path
- [x] Remote path auto-completion
- [x] Tilde (`~`) home-directory expansion
- [x] Show hidden (dotfiles) entries
- [x] Sort by name / size / date / type
- [x] Filter / search within a directory
- [x] Follow / display symlink targets
- [ ] Show / edit POSIX permissions (chmod) and ownership (chown)
- [t] Bookmarks / favorite remote paths
- [x] Breadcrumb path bar

## Local file browsing
- [x] Local file pane (browse the local filesystem)
- [x] Dual-pane (local ⇆ remote) layout
- [x] Sort / filter local pane
- [x] Open local item in system file manager

## Transfers
- [x] Download remote → local
- [x] Upload local → remote
- [x] Recursive directory transfer
- [x] Drag-and-drop (between panes and from the OS file manager)
- [x] Transfer queue with a concurrency cap
- [x] Pause / resume / cancel individual transfers
- [x] Per-file progress + aggregate progress (speed, %)
- [x] Up-front file list shown at 0%, filled in as files complete
- [x] Delta/incremental transfer (rsync) — skip unchanged files
- [x] On-the-wire compression
- [x] Preserve attributes / timestamps (archive mode)
- [x] Resume an interrupted partial transfer (`--partial` / restart)
- [t] Overwrite policy (skip / overwrite / rename / newer-only) prompts
- [x] Bandwidth / transfer-rate limit
- [t] Per-transfer ETA and overall throughput summary
- [t] Transfer history / log of completed transfers
- [x] Retry failed transfers
- [t] Verify integrity after transfer (checksum compare)

## Remote file operations
- [x] Create folder
- [x] Rename
- [x] Delete (recursive)
- [ ] Move / copy within the remote host
- [ ] Duplicate
- [ ] Multi-select batch operations
- [x] Free-space / disk-usage display for the remote
- [x] Calculate remote folder size

## Editing & viewing
- [t] Quick-view a remote file (text/image preview)
- [t] Edit a remote file in place (download → edit → re-upload on save)
- [t] Configurable external editor association

## Synchronization
- [x] Mirror / `--delete` mode with a safety gate
- [x] Exclude patterns
- [x] Dry-run / preview before a real sync
- [ ] Two-way / bidirectional sync
- [ ] Scheduled / automated syncs
- [ ] Saved sync jobs / profiles
- [ ] Compare directories (diff view of what would change)

## Security & privacy
- [x] Secrets never passed on the process command line
- [x] Strict host-key checking with explicit trust-on-first-use
- [ ] Optional master password to unlock saved credentials
- [ ] Configurable ciphers / KEX / MAC algorithms
- [x] Redact credentials from logs

## UX & quality of life
- [x] LAN peer discovery
- [x] Cross-platform (macOS / Windows / Linux)
- [ ] Light / dark theme follow-system
- [ ] Keyboard navigation & shortcuts throughout the browser
- [t] Transfer notifications (system notification on completion)
- [ ] Drag a remote file out to the desktop to download
- [t] Tabbed / multiple simultaneous host sessions
- [t] Error toasts with actionable detail
- [ ] Localization / i18n

## Advanced / power-user
- [ ] Built-in terminal / open SSH shell to the host
- [ ] Synchronized browsing (mirror navigation across panes)
- [ ] Command queue / scripting of operations
- [ ] CLI / headless mode for automation
- [ ] Per-host custom rsync/ssh options
