#!/usr/bin/env bash

set -euo pipefail

LIMINE_PROTOCOL_COMMIT="617369cb577108e483c096e373c4f2ecc9d2081d"
LIMINE_HEADER_SHA256="0aadf2633c85d8cb145344c968add158aeb1deb68af567d55ba8431c492b6d4c"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="${ROOT_DIR}/vendor/limine-protocol"
HEADER_PATH="${VENDOR_DIR}/limine.h"

LIMINE_HEADER_URL="https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/${LIMINE_PROTOCOL_COMMIT}/include/limine.h"

TMP_DIR="$(mktemp -d)"
TMP_HEADER="${TMP_DIR}/limine.h"

cleanup()
{
    rm -rf "${TMP_DIR}"
}

trap cleanup EXIT

echo "=== Fetching Limine protocol ==="

command -v curl >/dev/null 2>&1 || {
    echo "ERROR: curl not found" >&2
    exit 1
}

command -v shasum >/dev/null 2>&1 || {
    echo "ERROR: shasum not found" >&2
    exit 1
}

echo
echo "-- Download --"

curl --fail --location --show-error \
    --output "${TMP_HEADER}" \
    "${LIMINE_HEADER_URL}"

echo
echo "-- Verify SHA-256 --"

ACTUAL_SHA256="$(shasum -a 256 "${TMP_HEADER}" | awk '{print $1}')"

if [[ "${ACTUAL_SHA256}" != "${LIMINE_HEADER_SHA256}" ]]; then
    echo "ERROR: limine.h checksum mismatch" >&2
    echo "Expected: ${LIMINE_HEADER_SHA256}" >&2
    echo "Actual:   ${ACTUAL_SHA256}" >&2
    exit 1
fi

echo "Checksum OK."

echo
echo "-- Install --"

mkdir -p "${VENDOR_DIR}"

install -m 0644 \
    "${TMP_HEADER}" \
    "${HEADER_PATH}"

printf '%s\n' "${LIMINE_PROTOCOL_COMMIT}" \
    > "${VENDOR_DIR}/COMMIT"

echo
echo "Installed:"
echo "  ${HEADER_PATH}"
echo "  ${VENDOR_DIR}/COMMIT"
echo
echo "Limine protocol ready."
