#!/usr/bin/env bash

set -euo pipefail

readonly GDB_PORT=1234

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

cleanup()
{
    make debug-stop >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

make debug-stop

if lsof -nP -iTCP:${GDB_PORT} -sTCP:LISTEN >/dev/null 2>&1; then
    echo "error: TCP port ${GDB_PORT} is already in use"
    exit 1
fi

echo "MYOS_DEBUG_STARTING"

make debug &
make_pid=$!

while ! lsof -nP -iTCP:${GDB_PORT} -sTCP:LISTEN >/dev/null 2>&1; do
    if ! kill -0 "${make_pid}" 2>/dev/null; then
        wait "${make_pid}"
        exit $?
    fi

    sleep 0.1
done

echo "MYOS_DEBUG_READY"

wait "${make_pid}" || true
