#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /path/to/package-root" >&2
    exit 2
fi

PACKAGE_ROOT="$1"
INSTALLER="${PACKAGE_ROOT}/Install EON-Vari mu DSP.command"

[[ -x "${INSTALLER}" ]] || { echo "Installer is missing or not executable" >&2; exit 1; }
[[ -f "${PACKAGE_ROOT}/README.txt" ]] || { echo "README.txt is missing" >&2; exit 1; }
[[ -d "${PACKAGE_ROOT}/AU/EON-Vari mu DSP.component" ]] || { echo "AU payload is missing" >&2; exit 1; }
[[ -d "${PACKAGE_ROOT}/VST3/EON-Vari mu DSP.vst3" ]] || { echo "VST3 payload is missing" >&2; exit 1; }

INSTALL_TEST_HOME="$(mktemp -d "${TMPDIR:-/tmp}/eon-vari-mu-installer-test.XXXXXX")"
cleanup() { rm -rf "${INSTALL_TEST_HOME}"; }
trap cleanup EXIT

HOME="${INSTALL_TEST_HOME}" "${INSTALLER}"

[[ -d "${INSTALL_TEST_HOME}/Library/Audio/Plug-Ins/Components/EON-Vari mu DSP.component" ]] || {
    echo "AU was not installed" >&2
    exit 1
}
[[ -d "${INSTALL_TEST_HOME}/Library/Audio/Plug-Ins/VST3/EON-Vari mu DSP.vst3" ]] || {
    echo "VST3 was not installed" >&2
    exit 1
}

echo "PASS"
