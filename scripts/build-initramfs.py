#!/usr/bin/env python3

import pathlib
import sys


NEWC_MAGIC = b"070701"
TRAILER_NAME = "TRAILER!!!"


def padding_size(size):
    return (-size) & 3


def write_padding(output, size):
    output.write(b"\0" * padding_size(size))


def write_entry(output, inode, name, mode, data):
    name_bytes = name.encode("utf-8") + b"\0"

    fields = (
        inode,
        mode,
        0,
        0,
        1,
        0,
        len(data),
        0,
        0,
        0,
        0,
        len(name_bytes),
        0,
    )

    output.write(NEWC_MAGIC)

    for value in fields:
        output.write(f"{value:08x}".encode("ascii"))

    output.write(name_bytes)
    write_padding(output, 110 + len(name_bytes))

    output.write(data)
    write_padding(output, len(data))


def collect_entries(root):
    entries = []

    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()

        if path.is_dir():
            entries.append(
                (
                    relative,
                    0o040755,
                    b"",
                )
            )

            continue

        if path.is_file():
            entries.append(
                (
                    relative,
                    0o100644,
                    path.read_bytes(),
                )
            )

    return entries


def build_archive(root, output_path):
    entries = collect_entries(root)

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with output_path.open("wb") as output:
        inode = 1

        for name, mode, data in entries:
            write_entry(
                output,
                inode,
                name,
                mode,
                data,
            )

            inode += 1

        write_entry(
            output,
            inode,
            TRAILER_NAME,
            0,
            b"",
        )


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: build-initramfs.py ROOT OUTPUT"
        )

    root = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])

    if not root.is_dir():
        raise SystemExit(
            f"initramfs root does not exist: {root}"
        )

    build_archive(
        root,
        output,
    )


if __name__ == "__main__":
    main()
