# Building on macOS

The `macos-arm64` CMake preset builds native Apple Silicon applications using
Xcode's compiler and Qt supplied by vcpkg. Qt Creator and a separate Qt SDK are
not required.

## Prerequisites

- Xcode with its command-line tools selected (`xcode-select -p`).
- CMake 3.29 or newer and Ninja.
- A recent, bootstrapped vcpkg checkout providing Qt 6.9 or newer.
- Git, Python 3, pkg-config, autoconf, autoconf-archive, automake, GNU Make,
  and GNU libtool for building dependencies.

With Homebrew, the build tools can be installed using:

```sh
brew install cmake ninja pkgconf python autoconf autoconf-archive automake make libtool
```

Set `VCPKG_ROOT` to your vcpkg checkout before configuring. The first configure
builds Qt and the other dependencies from source and can take considerable time.
The preset selects the optional `qt` manifest feature and an arm64 triplet with
shared libraries and release-only dependencies. vcpkg's binary cache can be
reused by subsequent builds.

The preset builds all end-user applications and tests. It disables the optional
code/example generators and JBIG2 developer viewer with
`PDF4QT_BUILD_DEVELOPER_TOOLS=OFF`.

## Build, test, and install

Run from the repository root:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64 --parallel
ctest --preset macos-arm64
cmake --install build/macos-arm64
```

The installed applications are in `build/macos-arm64/install`:

- `Pdf4QtEditor.app`
- `Pdf4QtViewer.app`
- `Pdf4QtPageMaster.app`
- `Pdf4QtDiff.app`
- `Pdf4QtLaunchPad.app`

Installation deploys runtime libraries and Qt plugins into each application
bundle. Editor plugins and translations are included as well. Copy the apps to
a directory of your choice; keep them together if you use LaunchPad. Open the
Editor with:

```sh
open build/macos-arm64/install/Pdf4QtEditor.app
```

Use the installed bundles for distribution; build-tree bundles still reference
the build directory and vcpkg dependencies. The bundles are signed ad hoc for
local use, not signed with a Developer ID or notarized. Finder PDF document
associations are not yet provided; use the application's Open dialog.

The command-line `PdfTool` and development libraries are also installed outside
the bundles. They are not standalone distributable packages.

To use a separately installed Qt, omit the preset and `VCPKG_MANIFEST_FEATURES=qt`
and configure with your Qt prefix as usual. On macOS, both
`PDF4QT_INSTALL_DEPENDENCIES` and `PDF4QT_INSTALL_QT_DEPENDENCIES` must be enabled
to deploy standalone bundles at install time.

## Unsupported features on macOS

- Document scanning: the Scanner plugin has Windows WIA and optional Linux SANE
  backends, but no macOS backend. Connecting a scanner does not enable this
  feature. Scan with Image Capture or the scanner's software and open the
  resulting PDF in PDF4QT instead.
- Audiobook export: the AudioBook plugin uses Windows Speech API and is not
  implemented on macOS. This is separate from the Editor/Viewer read-aloud
  feature, which uses the bundled native Qt speech backend.

The plugin interfaces may still be visible, but these two features report that
they are unsupported or that no backend is available.
