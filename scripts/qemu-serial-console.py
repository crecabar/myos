#!/usr/bin/env python3

"""Run QEMU without forwarding serial terminal-resize commands."""

import re
import subprocess
import sys


RESIZE_SEQUENCE = re.compile(
    rb"\x1b\[8;[0-9]{1,3};[0-9]{1,3}t"
)

RESIZE_PREFIX = b"\x1b[8;"


def could_be_resize_sequence(data):
    if RESIZE_PREFIX.startswith(data):
        return True

    if not data.startswith(RESIZE_PREFIX):
        return False

    parameters = data[len(RESIZE_PREFIX):]

    if b";" not in parameters:
        return (
            len(parameters) <= 3
            and (
                not parameters
                or parameters.isdigit()
            )
        )

    rows, columns = parameters.split(b";", 1)

    return (
        1 <= len(rows) <= 3
        and rows.isdigit()
        and len(columns) <= 3
        and (
            not columns
            or columns.isdigit()
        )
    )


def forward_serial_output(source):
    pending = bytearray()

    while True:
        chunk = source.read(4096)

        if not chunk:
            break

        output = bytearray()

        for byte in chunk:
            if not pending:
                if byte == 0x1B:
                    pending.append(byte)
                else:
                    output.append(byte)

                continue

            pending.append(byte)

            if RESIZE_SEQUENCE.fullmatch(pending):
                pending.clear()
                continue

            if could_be_resize_sequence(pending):
                continue

            if byte == 0x1B:
                output.extend(pending[:-1])
                pending[:] = b"\x1b"
            else:
                output.extend(pending)
                pending.clear()

        if output:
            sys.stdout.buffer.write(output)
            sys.stdout.buffer.flush()

    if pending:
        sys.stdout.buffer.write(pending)
        sys.stdout.buffer.flush()


def main():
    if len(sys.argv) == 1:
        # Filter-only mode, useful for focused regression tests.
        forward_serial_output(sys.stdin.buffer)
        return 0

    try:
        with subprocess.Popen(
            sys.argv[1:],
            stdout=subprocess.PIPE,
            bufsize=0,
        ) as qemu:
            forward_serial_output(qemu.stdout)
            status = qemu.wait()
    except KeyboardInterrupt:
        return 130

    if status < 0:
        return 128 - status

    return status


if __name__ == "__main__":
    sys.exit(main())
