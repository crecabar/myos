#!/usr/bin/env bash

set -euo pipefail

LIMINE_VERSION="12.6.0"
LIMINE_ARCHIVE="limine-binary.tar.gz"
LIMINE_SHA256="8edf447b9c3c9bbd55b1e1e43528289ccb9fc8cb6f6f9edb4de3b7a2380671fe"
LIMINE_URL="https://github.com/Limine-Bootloader/Limine/releases/download/v${LIMINE_VERSION}/${LIMINE_ARCHIVE}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="${ROOT_DIR}/vendor/limine"
TMP_DIR="$(mktemp -d)"
ARCHIVE_PATH="${TMP_DIR}/${LIMINE_ARCHIVE}"

cleanup() {
    rm -rf "${TMP_DIR}"
}

trap cleanup EXIT

echo "=== Fetching Limine v${LIMINE_VERSION} ==="

command -v curl >/dev/null 2>&1 || {
    echo "ERROR: curl not found" >&2
    exit 1
}

command -v shasum >/dev/null 2>&1 || {
    echo "ERROR: shasum not found" >&2
    exit 1
}

command -v tar >/dev/null 2>&1 || {
    echo "ERROR: tar not found" >&2
    exit 1
}

echo
echo "-- Download --"
curl --fail --location --show-error \
    --output "${ARCHIVE_PATH}" \
    "${LIMINE_URL}"

echo
echo "-- Verify SHA-256 --"
ACTUAL_SHA256="$(shasum -a 256 "${ARCHIVE_PATH}" | awk '{print $1}')"

if [[ "${ACTUAL_SHA256}" != "${LIMINE_SHA256}" ]]; then
    echo "ERROR: Limine archive checksum mismatch" >&2
    echo "Expected: ${LIMINE_SHA256}" >&2
    echo "Actual:   ${ACTUAL_SHA256}" >&2
    exit 1
fi

echo "Checksum OK."

echo
echo "-- Install required files --"
rm -rf "${VENDOR_DIR}"
mkdir -p "${VENDOR_DIR}"

tar -xzf "${ARCHIVE_PATH}" \
    -C "${TMP_DIR}" \
    "limine-binary/BOOTX64.EFI" \
    "limine-binary/limine-uefi-cd.bin" \
    "limine-binary/LICENSE"

install -m 0644 \
    "${TMP_DIR}/limine-binary/BOOTX64.EFI" \
    "${VENDOR_DIR}/BOOTX64.EFI"

install -m 0644 \
    "${TMP_DIR}/limine-binary/limine-uefi-cd.bin" \
    "${VENDOR_DIR}/limine-uefi-cd.bin"

install -m 0644 \
    "${TMP_DIR}/limine-binary/LICENSE" \
    "${VENDOR_DIR}/LICENSE"

printf '%s\n' "${LIMINE_VERSION}" > "${VENDOR_DIR}/VERSION"

echo
echo "Installed:"
echo "  ${VENDOR_DIR}/BOOTX64.EFI"
echo "  ${VENDOR_DIR}/LICENSE"
echo "  ${VENDOR_DIR}/VERSION"
echo
echo "Limine v${LIMINE_VERSION} ready."
