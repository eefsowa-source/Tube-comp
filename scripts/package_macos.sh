#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
PRODUCT_NAME="EON-Vari mu DSP"
SIGN_MODE="adhoc"
SIGN_IDENTITY="${SIGN_IDENTITY:-}"
SKIP_BUILD=0
SKIP_INSTALL=0
RUN_AUVAL=0

usage() {
    cat <<'EOF'
Usage: scripts/package_macos.sh [options]

Build, sign, verify, install, and package the EON AU/VST3 bundles.

Options:
  --build-dir PATH       CMake build directory (default: ./build)
  --product-name NAME    Bundle name (default: EON-Vari mu DSP)
  --sign MODE            adhoc, developer-id, or none (default: adhoc)
  --identity NAME        Developer ID Application identity
  --skip-build           Use existing build artifacts
  --skip-install         Do not copy plugins into ~/Library/Audio/Plug-Ins
  --auval                Run auval after installation
  -h, --help             Show this help

For Developer ID signing, pass --sign developer-id and either --identity
"Developer ID Application: ..." or set SIGN_IDENTITY to that exact value.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for --build-dir" >&2; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --product-name)
            [[ $# -ge 2 ]] || { echo "Missing value for --product-name" >&2; exit 2; }
            PRODUCT_NAME="$2"
            shift 2
            ;;
        --sign)
            [[ $# -ge 2 ]] || { echo "Missing value for --sign" >&2; exit 2; }
            SIGN_MODE="$2"
            shift 2
            ;;
        --identity)
            [[ $# -ge 2 ]] || { echo "Missing value for --identity" >&2; exit 2; }
            SIGN_IDENTITY="$2"
            shift 2
            ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --skip-install) SKIP_INSTALL=1; shift ;;
        --auval) RUN_AUVAL=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "${SIGN_MODE}" in
    adhoc|developer-id|none) ;;
    *) echo "Invalid signing mode: ${SIGN_MODE}" >&2; exit 2 ;;
esac

if [[ "${SIGN_MODE}" == "developer-id" && -z "${SIGN_IDENTITY}" ]]; then
    echo "Developer ID signing requires --identity or SIGN_IDENTITY." >&2
    exit 2
fi

if [[ "${SKIP_BUILD}" -eq 0 ]]; then
    cmake --build "${BUILD_DIR}" --target eonchild_AU eonchild_VST3
fi

AU_SOURCE="${BUILD_DIR}/eonchild_artefacts/AU/${PRODUCT_NAME}.component"
VST3_SOURCE="${BUILD_DIR}/eonchild_artefacts/VST3/${PRODUCT_NAME}.vst3"

for bundle in "${AU_SOURCE}" "${VST3_SOURCE}"; do
    [[ -d "${bundle}" ]] || { echo "Missing plugin bundle: ${bundle}" >&2; exit 1; }
    [[ -f "${bundle}/Contents/Info.plist" ]] || { echo "Missing Info.plist: ${bundle}" >&2; exit 1; }
    executable_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${bundle}/Contents/Info.plist")"
    [[ -x "${bundle}/Contents/MacOS/${executable_name}" ]] || {
        echo "Missing executable in bundle: ${bundle}" >&2
        exit 1
    }
done

STAGING_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/eon-vari-mu-package.XXXXXX")"
cleanup() {
    rm -rf "${STAGING_ROOT}"
}
trap cleanup EXIT

SAFE_PRODUCT_NAME="${PRODUCT_NAME// /-}"
PAYLOAD_ROOT="${STAGING_ROOT}/${SAFE_PRODUCT_NAME}-macOS"
STAGING_AU_DIR="${PAYLOAD_ROOT}/AU"
STAGING_VST3_DIR="${PAYLOAD_ROOT}/VST3"
INSTALLER_TEMPLATE="${PROJECT_DIR}/Packaging/Install EON-Vari mu DSP.command"
README_TEMPLATE="${PROJECT_DIR}/Packaging/README.txt"
[[ -f "${INSTALLER_TEMPLATE}" ]] || { echo "Missing installer template: ${INSTALLER_TEMPLATE}" >&2; exit 1; }
[[ -f "${README_TEMPLATE}" ]] || { echo "Missing README template: ${README_TEMPLATE}" >&2; exit 1; }
mkdir -p "${STAGING_AU_DIR}" "${STAGING_VST3_DIR}"
ditto "${AU_SOURCE}" "${STAGING_AU_DIR}/${PRODUCT_NAME}.component"
ditto "${VST3_SOURCE}" "${STAGING_VST3_DIR}/${PRODUCT_NAME}.vst3"
ditto "${INSTALLER_TEMPLATE}" "${PAYLOAD_ROOT}/Install ${PRODUCT_NAME}.command"
chmod +x "${PAYLOAD_ROOT}/Install ${PRODUCT_NAME}.command"
ditto "${README_TEMPLATE}" "${PAYLOAD_ROOT}/README.txt"

STAGED_AU="${STAGING_AU_DIR}/${PRODUCT_NAME}.component"
STAGED_VST3="${STAGING_VST3_DIR}/${PRODUCT_NAME}.vst3"

sign_bundle() {
    local bundle="$1"
    case "${SIGN_MODE}" in
        adhoc)
            codesign --force --deep --sign - "${bundle}"
            ;;
        developer-id)
            security find-identity -v -p codesigning | grep -F -- "${SIGN_IDENTITY}" >/dev/null || {
                echo "Signing identity not found in keychain: ${SIGN_IDENTITY}" >&2
                exit 1
            }
            codesign --force --deep --options runtime --timestamp --sign "${SIGN_IDENTITY}" "${bundle}"
            ;;
        none) ;;
    esac
}

for bundle in "${STAGED_AU}" "${STAGED_VST3}"; do
    sign_bundle "${bundle}"
    if [[ "${SIGN_MODE}" != "none" ]]; then
        codesign --verify --deep --strict --verbose=2 "${bundle}"
    fi
done

if [[ "${SKIP_INSTALL}" -eq 0 ]]; then
    USER_AU_DIR="${HOME}/Library/Audio/Plug-Ins/Components"
    USER_VST3_DIR="${HOME}/Library/Audio/Plug-Ins/VST3"
    mkdir -p "${USER_AU_DIR}" "${USER_VST3_DIR}"
    ditto "${STAGED_AU}" "${USER_AU_DIR}/${PRODUCT_NAME}.component"
    ditto "${STAGED_VST3}" "${USER_VST3_DIR}/${PRODUCT_NAME}.vst3"
    codesign --verify --deep --strict "${USER_AU_DIR}/${PRODUCT_NAME}.component"
    codesign --verify --deep --strict "${USER_VST3_DIR}/${PRODUCT_NAME}.vst3"
    echo "Installed AU:   ${USER_AU_DIR}/${PRODUCT_NAME}.component"
    echo "Installed VST3: ${USER_VST3_DIR}/${PRODUCT_NAME}.vst3"

    if [[ "${RUN_AUVAL}" -eq 1 ]]; then
        killall -9 AudioComponentRegistrar 2>/dev/null || true
        auval -v aufx Eonc Eonn
    fi
fi

DIST_DIR="${PROJECT_DIR}/Packaging/dist"
mkdir -p "${DIST_DIR}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
ARCHIVE_PATH="${DIST_DIR}/${SAFE_PRODUCT_NAME}-macOS-${TIMESTAMP}.zip"

ditto -c -k --keepParent --norsrc --noextattr --noqtn "${PAYLOAD_ROOT}" "${ARCHIVE_PATH}"
shasum -a 256 "${ARCHIVE_PATH}" > "${ARCHIVE_PATH}.sha256"

echo "Package:  ${ARCHIVE_PATH}"
echo "Checksum: ${ARCHIVE_PATH}.sha256"
if [[ "${SIGN_MODE}" == "adhoc" ]]; then
    echo "Signing:  ad-hoc (local testing; not notarized for public distribution)"
elif [[ "${SIGN_MODE}" == "developer-id" ]]; then
    echo "Signing:  Developer ID (${SIGN_IDENTITY}); notarization is still required for public distribution"
else
    echo "Signing:  unchanged from build artifacts"
fi
