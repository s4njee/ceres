# Third-Party Notices

Ceres is distributed under the GNU General Public License v3.0 (see
[LICENSE](LICENSE)). It builds on, and on some platforms bundles, the
third-party components listed below. Each remains under its own license; those
licenses are reproduced or linked here as required.

This file is the authoritative list for redistributed binaries (notably the
Windows package, which ships `rsync.exe`, `ssh.exe`, and their runtime DLLs).
Keep it in sync whenever a bundled component is added, removed, or upgraded —
record the exact version and source for each.

---

## Qt

- **Use:** Application framework (Core, Gui, Qml, Quick, Network).
- **License:** GNU Lesser General Public License v3.0 (LGPLv3), with a
  commercial option from The Qt Company.
- **Version bundled:** _TODO: record the Qt version shipped with each release._
- **Notes:** LGPLv3 compliance requires that the user be able to relink the
  application against a modified Qt. Ship Qt as separate shared libraries (do
  not statically link under LGPL) and include the LGPLv3 text with the package.
- **Source:** https://www.qt.io/ — source for the exact version must be made
  available alongside any binary release.

## rsync

- **Use:** The file-transfer engine Ceres drives. Bundled as `rsync.exe` on
  Windows; provided by the system on Linux/macOS.
- **License:** GNU General Public License v3.0 (GPLv3).
- **Version bundled:** _TODO: record the rsync build and its origin
  (e.g. cwRsync / MSYS2 / Cygwin package + version)._
- **Notes:** GPLv3 — distributing the binary obligates us to offer the
  corresponding source for that exact build. Record the upstream source URL and
  any patches applied by the packager.
- **Source:** https://rsync.samba.org/

## OpenSSH

- **Use:** Transport for rsync-over-SSH. Bundled as `ssh.exe` on Windows when
  the chosen rsync runtime does not already provide one.
- **License:** BSD-style (OpenSSH license) plus the licenses of its components.
- **Version bundled:** _TODO: record the ssh.exe build and origin._
- **Notes:** Permissive; redistribution requires retaining the copyright and
  license notice. If `ssh.exe` comes from the Cygwin/MSYS rsync package, it
  shares that package's runtime DLL (see below).
- **Source:** https://www.openssh.com/

## Cygwin / MSYS2 runtime DLL

- **Use:** C runtime for the bundled `rsync.exe` / `ssh.exe`. One of
  `cygwin1.dll` (Cygwin) or `msys-2.0.dll` (MSYS2), depending on which rsync
  package is bundled. Ceres detects which one is present to decide local-path
  conversion (`/cygdrive/c/...` vs `/c/...`).
- **License:** GNU Lesser General Public License v3.0 (Cygwin) /
  GPL-family terms (MSYS2). Cygwin's runtime carries a linking exception that
  permits distribution alongside GPL-compatible software such as Ceres.
- **Version bundled:** _TODO: record the DLL and its source package version._
- **Source:** https://www.cygwin.com/ or https://www.msys2.org/

---

## Compliance checklist for a redistributed (Windows) build

- [ ] Include this file and a copy of [LICENSE](LICENSE) in the package.
- [ ] Include the LGPLv3 text for Qt and the Cygwin runtime.
- [ ] Fill in every `TODO` version/source field above for the shipped binaries.
- [ ] Make corresponding source available for the GPL/LGPL components
      (rsync, Qt, Cygwin runtime) — a written offer or bundled source archive.
- [ ] Ship Qt as relinkable shared libraries (LGPL requirement).
