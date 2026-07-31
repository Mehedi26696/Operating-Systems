# Source Inventory

## Scope

`rg --files` reports more than two thousand paths in the workspace. A large fraction are compiled binaries, object files, generated install copies, manpage copies, cross-compiler files, and disk images. The meaningful source inventory outside generated build/install output is:

| Area | Approx. count | Role |
| --- | ---: | --- |
| `src/userland` | 284 files | User programs, libc, headers, tests, and filesystem utilities. |
| `src/kern` | 198 files | Kernel source, architecture support, drivers, VFS, filesystems, tests, config. |
| `src/man` | 179 files | Source manpages and documentation. |
| `src/common` | 36 files | Shared libc/string/printf code and GCC millicode helpers. |
| `src/mk` | 17 files | OS/161 makefile framework and build helper scripts. |
| `src/testscripts` | 3 files | Python test runner scripts. |
| `src/design` | 3 files | Assignment/design notes. |

By extension, the non-generated `src` tree is mostly C and header code:

| Extension | Approx. count | Meaning |
| --- | ---: | --- |
| `.c` | 267 | Kernel, userland, libc, tests, tools. |
| `.html` | 169 | Manpages/documentation. |
| `.h` | 146 | Kernel and userland interfaces. |
| no extension | 100 | Scripts, config files, generated-style source inputs. |
| `.mk` | 16 | Build framework includes. |
| `.S` | 9 | MIPS assembly entry, context switching, syscall stubs. |
| `.sh` | 5 | Build/generation scripts. |
| `.txt` | 4 | Assignment/design text. |
| `.py` | 2 | Test runner scripts. |

## Top-Level Layout

### `src/`

Canonical source tree.

- `src/kern/`: OS kernel source.
- `src/userland/`: commands, tests, libc, and sbin utilities.
- `src/common/`: code shared between kernel and userland builds.
- `src/mk/`: reusable OS/161 make rules.
- `src/man/`: documentation/manpages.
- `src/design/`: assignment design documents.
- `src/testscripts/`: Python runner scripts.
- `src/build/`: generated build/install output, not primary source.

### `root/`

Installed runtime tree for the simulator. It contains kernel images, user binaries, libraries, includes, manpages, test binaries, host helper programs, `sys161.conf`, and disk images such as `LHD0.img` and `LHD1.img`.

Treat `root/` as deployment output. It mirrors built artifacts from `src/` and provides files consumed by System/161.

### `tools/`

Bundled System/161 and MIPS cross-toolchain area. It includes simulator binaries (`sys161`, `hub161`, `disk161`, `trace161`, `stat161`), MIPS binutils/GCC wrappers, libraries, manpages, locale files, linker scripts, and documentation.

Treat `tools/` as external dependency/tooling, not application logic.

### `.sockets/` and `.vscode/`

Local runtime/editor support. These are not core project source.

### `analysis/`

Generated analysis notes for this request.

## Generated and Binary Areas

The following directories/files should normally be excluded from semantic source review:

- `src/build/`: object files, installed manpages, built userland programs, host tools.
- `src/kern/compile/DUMBVM/`: configured kernel build directory, objects, generated `autoconf.c`, generated option headers, kernel binary.
- `root/bin`, `root/sbin`, `root/testbin`, `root/hostbin`, `root/lib`, `root/kernel-DUMBVM`: installed compiled artifacts.
- `tools/bin`, `tools/lib`, `tools/libexec`, `tools/mips-harvard-os161`, `tools/share/locale`: bundled cross-toolchain/simulator artifacts.
- `root/*.img`: disk images.

These files are still part of the workspace, but their purpose is build/runtime support rather than editable project logic.
