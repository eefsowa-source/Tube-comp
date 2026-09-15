# Local ZIP Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Distribute a personal-use EON-Vari mu DSP ZIP with a double-click macOS installer and manual installation guide.

**Architecture:** The existing package script stages AU and VST3 bundles in a temporary payload directory. It will additionally stage an executable `.command` file and `README.txt`; the installer resolves that directory at runtime and copies only the two explicit same-name bundles into the current user's Audio Plug-Ins directories. A shell test runs the installer with an isolated HOME directory.

**Tech Stack:** Bash, macOS `ditto`, `codesign`, CMake-built JUCE AU/VST3 bundles.

---

### Task 1: Package-content regression test

**Files:**
- Create: `Tests/LocalZipInstallerTest.sh`

- [ ] **Step 1: Write the failing test**

```bash
[[ -x "${PACKAGE_ROOT}/Install EON-Vari mu DSP.command" ]] || exit 1
[[ -f "${PACKAGE_ROOT}/README.txt" ]] || exit 1
HOME="${INSTALL_TEST_HOME}" "${PACKAGE_ROOT}/Install EON-Vari mu DSP.command"
[[ -d "${INSTALL_TEST_HOME}/Library/Audio/Plug-Ins/Components/EON-Vari mu DSP.component" ]] || exit 1
[[ -d "${INSTALL_TEST_HOME}/Library/Audio/Plug-Ins/VST3/EON-Vari mu DSP.vst3" ]] || exit 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./Tests/LocalZipInstallerTest.sh /tmp/eon-vari-mu-package-does-not-exist`

Expected: `Installer is missing or not executable` and exit status 1.

- [ ] **Step 3: Commit**

Do not commit because this workspace has no project commit history; preserve existing user changes.

### Task 2: Runtime installer and user guide

**Files:**
- Create: `Packaging/Install EON-Vari mu DSP.command`
- Create: `Packaging/README.txt`
- Test: `Tests/LocalZipInstallerTest.sh`

- [ ] **Step 1: Write minimal installer**

```bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_NAME="EON-Vari mu DSP"
ditto "${SCRIPT_DIR}/AU/${PRODUCT_NAME}.component" "${HOME}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"
ditto "${SCRIPT_DIR}/VST3/${PRODUCT_NAME}.vst3" "${HOME}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
codesign --verify --deep --strict "${HOME}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"
codesign --verify --deep --strict "${HOME}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
```

The actual script also validates its two source bundle directories, Info.plists,
and executable paths before copying. It creates only the two explicit target
parent directories and does not delete any differently named bundle.

- [ ] **Step 2: Write README.txt**

Include the exact double-click procedure, two manual destination paths, first-run
macOS Privacy & Security allowance instructions, and arm64 Apple Silicon scope.

- [ ] **Step 3: Run test to verify it passes**

Run: `./Tests/LocalZipInstallerTest.sh /tmp/eon-vari-mu-package-test`

Expected: `PASS`, with both plug-in bundles installed under the isolated HOME.

### Task 3: Add installer assets to generated archive

**Files:**
- Modify: `scripts/package_macos.sh:100-160`
- Modify: `Packaging/README.md`
- Test: `Tests/LocalZipInstallerTest.sh`

- [ ] **Step 1: Stage the installer and README**

```bash
ditto "${PROJECT_DIR}/Packaging/Install EON-Vari mu DSP.command" "${PAYLOAD_ROOT}/Install EON-Vari mu DSP.command"
ditto "${PROJECT_DIR}/Packaging/README.txt" "${PAYLOAD_ROOT}/README.txt"
chmod +x "${PAYLOAD_ROOT}/Install EON-Vari mu DSP.command"
```

- [ ] **Step 2: Create a fresh package without touching live installation**

Run: `./scripts/package_macos.sh --skip-install --sign adhoc`

Expected: one timestamped ZIP and `.sha256` file under `Packaging/dist/`.

- [ ] **Step 3: Extract and run the isolated installer test**

Run: `ditto -x -k Packaging/dist/<newest-archive>.zip /tmp/eon-vari-mu-package-test && ./Tests/LocalZipInstallerTest.sh /tmp/eon-vari-mu-package-test/EON-Vari-mu-DSP-macOS`

Expected: `PASS`.

- [ ] **Step 4: Verify archive contents and checksum**

Run: `shasum -a 256 -c Packaging/dist/<newest-archive>.zip.sha256 && unzip -Z1 Packaging/dist/<newest-archive>.zip`

Expected: checksum `OK`; listing contains `Install EON-Vari mu DSP.command`, `README.txt`, one AU bundle, and one VST3 bundle.

- [ ] **Step 5: Document the new package layout**

Update `Packaging/README.md` to state that the archive contains the double-click installer, explain `--skip-install`, and retain the distinction between local ad-hoc packages and Developer ID releases.

### Task 4: Full regression verification

**Files:**
- Test: `CMakeLists.txt`, `Tests/*.cpp`, `Tests/LocalZipInstallerTest.sh`

- [ ] **Step 1: Build and run compiled tests**

Run: `cmake -S . -B Build && cmake --build Build --config Release -j 4 && ctest --test-dir Build --output-on-failure`

Expected: build exit status 0 and all four registered tests pass.

- [ ] **Step 2: Validate the final installed bundles**

Run: `codesign --verify --deep --strict "$HOME/Library/Audio/Plug-Ins/Components/EON-Vari mu DSP.component" && codesign --verify --deep --strict "$HOME/Library/Audio/Plug-Ins/VST3/EON-Vari mu DSP.vst3"`

Expected: both commands exit status 0.

- [ ] **Step 3: Commit**

Do not commit because this workspace has no project commit history; preserve existing user changes.
