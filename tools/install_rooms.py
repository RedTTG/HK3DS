#!/usr/bin/env python3
"""Parallel install of built Hollow Knight rooms."""

import argparse
import os
import shutil
import subprocess
import sys
import traceback
import time
import threading
import tempfile
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from pathlib import Path

from parallel_helpers import interruptible_main, parallel_executor

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lyr_v2
import patch_room_audio
import zwrap


def run(command, cwd=None):
    return subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        check=False,
    )


def atomic_write(path, text):
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as handle:
        handle.write(text)
        handle.flush()
        temporary = Path(handle.name)
    temporary.replace(path)


def install_one(room, room_build, romfs, tools, status_dir, index):
    output = []
    status_dir = Path(status_dir)
    atomic_write(status_dir / f"{room}.status", "RUNNING\n")
    room_log = status_dir / f"{room}.log"
    atomic_write(room_log, "running\n")

    def phase(message):
        output.append(message)
        atomic_write(room_log, "\n".join(output) + "\n")

    room_build = Path(room_build)
    romfs_rooms = Path(romfs) / "rooms"
    romfs_rooms.mkdir(parents=True, exist_ok=True)

    source_room = room_build / f"{room}.room"
    if source_room.is_file():
        phase("patch room audio")
        patch_room_audio.patch_room(room_build, room)
        shutil.copy2(source_room, romfs_rooms / source_room.name)

    for suffix in (".lyr", "_ents.spr", "_enemies.spr"):
        source = room_build / f"{room}{suffix}"
        if source.is_file():
            shutil.copy2(source, romfs_rooms / source.name)

    lyr = romfs_rooms / f"{room}.lyr"
    if lyr.is_file():
        phase("convert layer")
        lyr_v2.convert_layer(str(lyr))

    for path in (
        romfs_rooms / f"{room}.room",
        lyr,
        romfs_rooms / f"{room}_ents.spr",
        romfs_rooms / f"{room}_enemies.spr",
    ):
        if path.is_file():
            phase(f"wrap {path.name}")
            zwrap.wrap_file(str(path))

    phase("camera post-processing")
    camlocks_result = run(
        [sys.executable, str(tools / "camlocks.py"), str(romfs_rooms), room],
        cwd=tools,
    )
    if camlocks_result.stdout.strip():
        output.append(camlocks_result.stdout.strip())
    if camlocks_result.returncode:
        return index, room, False, "\n".join(output)

    phase("mask post-processing")
    masks_result = run(
        [sys.executable, str(tools / "masks.py"), str(romfs_rooms), room],
        cwd=tools,
    )
    if masks_result.stdout.strip():
        output.append(masks_result.stdout.strip())
    if masks_result.returncode:
        return index, room, False, "\n".join(output)

    mask_sheet = romfs_rooms / f"{room}_msk.spr"
    if mask_sheet.is_file():
        zwrap.wrap_file(str(mask_sheet))

    return index, room, True, "\n".join(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--room-build", required=True)
    parser.add_argument("--romfs", required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("rooms", nargs="+")
    args = parser.parse_args()

    tools = Path(__file__).resolve().parent
    room_build = os.path.abspath(args.room_build)
    romfs = os.path.abspath(args.romfs)
    jobs = max(1, args.jobs)
    if jobs > len(args.rooms):
        jobs = max(1, len(args.rooms))

    failures = []
    status_dir = tools.parent / "work" / "logs" / "install"
    status_dir.mkdir(parents=True, exist_ok=True)
    progress_lock = threading.Lock()
    started = time.monotonic()
    completed_count = 0
    terminal_width = shutil.get_terminal_size((80, 20)).columns

    def report_progress(index, room, ok, output):
        nonlocal completed_count
        room_log = status_dir / f"{room}.log"
        atomic_write(room_log, output + ("\n" if output else ""))
        status = "OK" if ok else "FAILED"
        atomic_write(status_dir / f"{room}.status", status + "\n")
        with progress_lock:
            completed_count += 1
            elapsed = time.monotonic() - started
            line = (
                f"[install {completed_count}/{len(args.rooms)}]"
                f" {room}: {status} ({elapsed:.1f}s)"
            )
            print(line[:terminal_width], flush=True)

    with parallel_executor(
        ThreadPoolExecutor,
        max_workers=jobs,
        thread_name_prefix="install-room",
    ) as pool:
        for room in args.rooms:
            atomic_write(status_dir / f"{room}.status", "PENDING\n")
            atomic_write(status_dir / f"{room}.log", "pending\n")
        futures = {}
        room_queue = list(enumerate(args.rooms))
        completed = {}
        next_queue = 0

        def submit_next():
            nonlocal next_queue
            if next_queue >= len(room_queue):
                return False
            index, room = room_queue[next_queue]
            next_queue += 1
            future = pool.submit(
                install_one,
                room,
                room_build,
                romfs,
                tools,
                status_dir,
                index,
            )
            futures[future] = (index, room)
            atomic_write(status_dir / f"{room}.status", "RUNNING\n")
            atomic_write(status_dir / f"{room}.log", "running\n")
            # print(f"== {room}", flush=True)
            return True

        while len(futures) < jobs:
            if not submit_next():
                break

        while futures:
            done, _ = wait(
                futures,
                return_when=FIRST_COMPLETED,
            )
            for future in done:
                index, room = futures.pop(future)
                try:
                    index, room, ok, output = future.result()
                except BaseException as error:
                    output = "".join(
                        traceback.format_exception(type(error), error, error.__traceback__)
                    )
                    ok = False
                report_progress(index, room, ok, output)
                completed[index] = (room, ok, output)
                submit_next()

    for index in sorted(completed):
        room, ok, output = completed[index]
        if output:
            print(output)
        if ok:
            print(f"Done with {room}")
        else:
            failures.append((room, output))

    if failures:
        for room, output in failures:
            print(f"\nFAILED: {room}", file=sys.stderr)
            if output:
                print(output, file=sys.stderr)
        return 1

    result = run(
        [sys.executable, str(tools / "fill_gate_targets.py"),
         str(Path(romfs) / "rooms"), str(room_build)],
        cwd=tools,
    )
    if result.stdout.strip():
        print(result.stdout.strip())
    if result.returncode:
        return result.returncode

    result = run(
        [sys.executable, str(tools / "patch_gate_conditions.py"),
         str(Path(romfs) / "rooms"), *args.rooms],
        cwd=tools,
    )
    if result.stdout.strip():
        print(result.stdout.strip())
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(interruptible_main(main))
