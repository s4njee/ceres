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
- **Version bundled:** MSYS2 `msys` package `rsync 3.4.4-1`.
- **Notes:** GPLv3 — distributing the binary obligates us to offer the
  corresponding source for that exact build. Use the MSYS2 source package for
  the shipped package version.
- **Source:** https://packages.msys2.org/packages/rsync and
  https://rsync.samba.org/

## OpenSSH

- **Use:** Transport for rsync-over-SSH. Bundled as `ssh.exe` on Windows when
  the chosen rsync runtime does not already provide one.
- **License:** BSD-style (OpenSSH license) plus the licenses of its components.
- **Version bundled:** MSYS2 `msys` package `openssh 10.3p1-2`.
- **Notes:** Permissive; redistribution requires retaining the copyright and
  license notice. The bundled `ssh.exe` shares the MSYS2 runtime DLL with
  `rsync.exe` (see below).
- **Source:** https://packages.msys2.org/packages/openssh and
  https://www.openssh.com/

## Cygwin / MSYS2 runtime DLL

- **Use:** C runtime for the bundled `rsync.exe` / `ssh.exe`. One of
  `cygwin1.dll` (Cygwin) or `msys-2.0.dll` (MSYS2), depending on which rsync
  package is bundled. Ceres detects which one is present to decide local-path
  conversion (`/cygdrive/c/...` vs `/c/...`).
- **License:** MSYS2 runtime terms, derived from Cygwin; keep the MSYS2 package
  license files with the redistributed DLLs.
- **Version bundled:** MSYS2 `msys2-runtime 3.6.9-2` (`msys-2.0.dll`).
- **Source:** https://packages.msys2.org/packages/msys2-runtime and
  https://www.msys2.org/

## MSYS2 runtime dependency DLLs

- **Use:** DLL dependencies loaded by the bundled MSYS2 `rsync.exe` and
  `ssh.exe`.
- **Version bundled:** `gcc-libs 15.3.0-1`, `heimdal-libs 7.8.0-5`,
  `libiconv 1.19-1`, `libintl 0.22.5-1`, `liblz4 1.10.0-1`,
  `libopenssl 3.6.3-1`, `libsqlite 3.53.2-1`, `libxcrypt 4.5.2-1`,
  `libxxhash 0.8.3-1`, `libzstd 1.5.7-1`, `popt 1.19-1`, and `zlib 1.3.2-1`.
- **Notes:** Re-run `ldd` against the final bundled `rsync.exe` and `ssh.exe`
  before each release, then update this list if MSYS2 package dependencies
  change.
- **Source:** https://packages.msys2.org/

---

## Compliance checklist for a redistributed (Windows) build

- [ ] Include this file and a copy of [LICENSE](LICENSE) in the package.
- [ ] Include the LGPLv3 text for Qt and the MSYS2 runtime/package licenses.
- [ ] Fill in every `TODO` version/source field above for the shipped binaries.
- [ ] Make corresponding source available for the GPL/LGPL components
      (rsync, Qt, MSYS2 runtime/dependencies) — a written offer or bundled
      source archive.
- [ ] Ship Qt as relinkable shared libraries (LGPL requirement).
