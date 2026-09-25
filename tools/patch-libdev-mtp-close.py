#!/usr/bin/env python3

import argparse
import hashlib
import pathlib
import re
import shutil
import struct
import subprocess
import sys


def run(*args):
    try:
        return subprocess.check_output(
            args,
            text=True,
            stderr=subprocess.STDOUT,
        )
    except subprocess.CalledProcessError as e:
        print(e.output, file=sys.stderr)
        raise


def sha256(path):
    h = hashlib.sha256()

    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)

    return h.hexdigest()


def check_architecture(lib):
    out = run("readelf", "-h", str(lib))

    if "Advanced Micro Devices X86-64" not in out:
        raise RuntimeError(
            "This patcher currently supports only the "
            "x86-64 OBSBOT libdev.so build."
        )


def get_symbols(lib):
    out = run("nm", "-n", "-C", str(lib))

    symbols = []

    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(.*)$"
    )

    for line in out.splitlines():
        m = pattern.match(line)

        if not m:
            continue

        symbols.append(
            (
                int(m.group(1), 16),
                m.group(2),
                m.group(3),
            )
        )

    return symbols


def find_exact_symbol(symbols, name):
    matches = [
        addr
        for addr, _, symbol in symbols
        if symbol == name
    ]

    if len(matches) != 1:
        raise RuntimeError(
            f"Expected exactly one symbol:\n"
            f"  {name}\n"
            f"Found: {len(matches)}"
        )

    return matches[0]


def find_vector_read_symbols(symbols):
    matches = []

    for addr, _, name in symbols:
        if not name.startswith("mtp::PipePacketer::Read("):
            continue

        if "std::vector<unsigned char" not in name:
            continue

        if "std::shared_ptr<mtp::IObjectOutputStream>" in name:
            continue

        matches.append((addr, name))

    if not matches:
        raise RuntimeError(
            "Could not find the vector-based "
            "mtp::PipePacketer::Read() symbol."
        )

    return matches


def next_symbol_address(symbols, address):
    candidates = sorted(
        addr
        for addr, _, _ in symbols
        if addr > address
    )

    if not candidates:
        raise RuntimeError(
            "Could not determine the end of Session::Close()."
        )

    return candidates[0]


def get_load_segments(lib):
    out = run("readelf", "-lW", str(lib))

    segments = []

    pattern = re.compile(
        r"^\s*LOAD\s+"
        r"(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)"
    )

    for line in out.splitlines():
        m = pattern.match(line)

        if not m:
            continue

        file_offset = int(m.group(1), 16)
        virtual_address = int(m.group(2), 16)
        file_size = int(m.group(4), 16)

        segments.append(
            (
                file_offset,
                virtual_address,
                file_size,
            )
        )

    if not segments:
        raise RuntimeError(
            "Could not parse ELF LOAD segments."
        )

    return segments


def virtual_to_file_offset(address, segments):
    for file_offset, virtual_address, file_size in segments:
        if (
            virtual_address
            <= address
            < virtual_address + file_size
        ):
            return (
                file_offset
                + address
                - virtual_address
            )

    raise RuntimeError(
        f"Virtual address 0x{address:x} is not inside "
        "a file-backed ELF LOAD segment."
    )


def find_read_call(lib, start, stop, target_addresses):
    out = run(
        "objdump",
        "-d",
        "-C",
        f"--start-address=0x{start:x}",
        f"--stop-address=0x{stop:x}",
        str(lib),
    )

    instruction_pattern = re.compile(
        r"^\s*([0-9a-fA-F]+):\s+"
        r"((?:[0-9a-fA-F]{2}\s+)+)"
        r"\s*([A-Za-z0-9_.]+)"
    )

    matches = []

    for line in out.splitlines():
        m = instruction_pattern.match(line)

        if not m:
            continue

        address = int(m.group(1), 16)
        encoded = bytes.fromhex(m.group(2))
        mnemonic = m.group(3)

        if not mnemonic.startswith("call"):
            continue

        # x86-64 direct near CALL rel32
        if len(encoded) != 5 or encoded[0] != 0xE8:
            continue

        displacement = struct.unpack(
            "<i",
            encoded[1:5],
        )[0]

        target = address + 5 + displacement

        if target in target_addresses:
            matches.append(
                (
                    address,
                    encoded,
                    target,
                    line.strip(),
                )
            )

    if len(matches) != 1:
        print(
            "\nSession::Close() disassembly:",
            file=sys.stderr,
        )
        print(out, file=sys.stderr)

        raise RuntimeError(
            "Expected exactly one call from "
            "mtp::Session::Close() to the vector-based "
            "mtp::PipePacketer::Read(); "
            f"found {len(matches)}."
        )

    return matches[0]


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Patch OBSBOT libdev.so so Session::Close() "
            "does not wait for an MTP response after the "
            "USB device has disconnected."
        )
    )

    parser.add_argument(
        "input",
        type=pathlib.Path,
        help="Original libdev.so.1.0.2",
    )

    parser.add_argument(
        "output",
        type=pathlib.Path,
        help="Destination for patched library",
    )

    args = parser.parse_args()

    source = args.input.resolve()
    destination = args.output.resolve()

    if not source.is_file():
        raise RuntimeError(
            f"Input library does not exist: {source}"
        )

    if source == destination:
        raise RuntimeError(
            "Refusing to modify the original library in place."
        )

    check_architecture(source)

    symbols = get_symbols(source)

    close_start = find_exact_symbol(
        symbols,
        "mtp::Session::Close()",
    )

    try:
        close_stop = find_exact_symbol(
            symbols,
            "mtp::Session::~Session()",
        )
    except RuntimeError:
        close_stop = next_symbol_address(
            symbols,
            close_start,
        )

    if close_stop <= close_start:
        raise RuntimeError(
            "Invalid Session::Close() symbol range."
        )

    vector_reads = find_vector_read_symbols(symbols)

    target_addresses = {
        addr
        for addr, _ in vector_reads
    }

    (
        call_address,
        call_bytes,
        call_target,
        disassembly,
    ) = find_read_call(
        source,
        close_start,
        close_stop,
        target_addresses,
    )

    segments = get_load_segments(source)

    file_offset = virtual_to_file_offset(
        call_address,
        segments,
    )

    destination.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    shutil.copy2(
        source,
        destination,
    )

    data = bytearray(destination.read_bytes())

    actual = bytes(
        data[file_offset:file_offset + 5]
    )

    if actual != call_bytes:
        raise RuntimeError(
            "Bytes at calculated file offset do not "
            "match the disassembly.\n"
            f"Expected: {call_bytes.hex(' ')}\n"
            f"Actual:   {actual.hex(' ')}"
        )

    data[file_offset:file_offset + 5] = \
        b"\x90" * 5

    destination.write_bytes(data)

    verify = destination.read_bytes()[
        file_offset:file_offset + 5
    ]

    if verify != b"\x90" * 5:
        raise RuntimeError(
            "Patch verification failed."
        )

    target_name = next(
        name
        for addr, name in vector_reads
        if addr == call_target
    )

    print("OBSBOT libdev MTP reconnect patch")
    print("----------------------------------")
    print(f"Input:        {source}")
    print(f"Output:       {destination}")
    print()
    print(
        f"Session::Close(): "
        f"0x{close_start:x}-0x{close_stop:x}"
    )
    print(f"CALL address: 0x{call_address:x}")
    print(f"CALL target:  0x{call_target:x}")
    print(f"File offset:  0x{file_offset:x}")
    print()
    print(f"Original:     {call_bytes.hex(' ')}")
    print("Patched:      90 90 90 90 90")
    print()
    print(f"Target symbol:\n  {target_name}")
    print()
    print(f"Matched instruction:\n  {disassembly}")
    print()
    print(f"Original SHA256: {sha256(source)}")
    print(f"Patched SHA256:  {sha256(destination)}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(
            f"ERROR: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
