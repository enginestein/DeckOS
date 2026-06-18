#!/usr/bin/env python3
"""
deckos-dashboard.py — real-time serial dashboard for DeckOS (RP2040).

Connects to a DeckOS board over USB CDC and provides:
  - Live system info (uptime, CPU, memory, temperature)
  - File browser (upload/download from host)
  - One-click command execution
  - Reboot trigger

Usage:
  ./deckos-dashboard.py [port]
  (leave port blank for auto-detect on Linux)
"""

import sys
import os
import time
import json
import threading
import socket
import argparse
from dataclasses import dataclass
from typing import Optional, Callable

HAS_SERIAL = False
try:
    import serial
    HAS_SERIAL = True
except ImportError:
    pass


@dataclass
class DeckOSSerial:
    port: str
    conn: Optional["serial.Serial"] = None
    buf: str = ""
    lock: threading.Lock = threading.Lock()

    def connect(self) -> bool:
        if not HAS_SERIAL:
            print("deckos-dashboard: pyserial not installed (pip install pyserial)")
            return False
        try:
            self.conn = serial.Serial(self.port, 115200, timeout=0.5)
            time.sleep(2)
            # Drain startup garbage
            self.conn.read(1024)
            return True
        except Exception as e:
            print(f"deckos-dashboard: {e}")
            return False

    def cmd(self, c: str, timeout: float = 2.0) -> str:
        with self.lock:
            self.conn.write((c + "\n").encode())
            self.conn.flush()
            out = ""
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    data = self.conn.read(256).decode(errors="replace")
                    out += data
                    if "> " in out:
                        break
                except:
                    break
            return out.strip("> ").strip()

    def close(self):
        if self.conn:
            self.conn.close()


HAS_CURSES = False
try:
    import curses
    import curses.textpad
    HAS_CURSES = True
except ImportError:
    pass


def run_curses(deck: DeckOSSerial):
    if not HAS_CURSES:
        print("deckos-dashboard: curses not available (run in terminal)")
        return

    stdscr = curses.initscr()
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_GREEN, -1)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_RED, -1)
    curses.init_pair(4, curses.COLOR_CYAN, -1)
    curses.curs_set(0)
    stdscr.nodelay(1)
    stdscr.timeout(500)

    refresh_count = 0

    def safe_cmd(c):
        try:
            return deck.cmd(c, timeout=1.5)
        except:
            return ""

    while True:
        refresh_count += 1
        stdscr.erase()

        h, w = stdscr.getmaxyx()

        # Title
        stdscr.addstr(0, 0, "DeckOS Dashboard", curses.A_BOLD | curses.color_pair(4))
        stdscr.addstr(0, w - len(deck.port) - 2, f"[{deck.port}]", curses.color_pair(1))

        # System info
        try:
            uptime = safe_cmd("uptime")
            stdscr.addstr(2, 2, f"Uptime: {uptime}", curses.color_pair(1))

            vers = safe_cmd("version")
            stdscr.addstr(3, 2, f"Version: {vers[:w-20]}")

            temp = safe_cmd("temperature")
            stdscr.addstr(4, 2, f"Temp: {temp}")

            free = safe_cmd("free")
            stdscr.addstr(5, 2, f"Memory: {free}")
        except:
            pass

        # File listing
        stdscr.addstr(7, 2, "── Files ──", curses.A_BOLD | curses.color_pair(4))
        try:
            ls = safe_cmd("ls")
            y = 8
            for line in ls.split("\n"):
                if y >= h - 2:
                    break
                stdscr.addstr(y, 4, line[:w-8])
                y += 1
        except:
            pass

        # Command bar
        stdscr.addstr(h - 2, 0, "─" * w, curses.color_pair(4))
        stdscr.addstr(h - 1, 2, "[Q]uit  [R]eboot  [r]efresh  type to send command: ")

        # Input handling
        key = stdscr.getch()
        if key == ord('q') or key == ord('Q'):
            break
        elif key == ord('r'):
            pass  # refresh
        elif key == ord('R'):
            safe_cmd("reboot")
            time.sleep(1)
        elif key >= 32 and key < 127:
            # Start typing a command
            curses.curs_set(1)
            stdscr.nodelay(0)
            stdscr.addstr(h - 1, 2, "DeckOS> ")
            curses.echo()
            cmd_str = stdscr.getstr(h - 1, 12, 120)
            curses.noecho()
            if cmd_str:
                result = safe_cmd(cmd_str.decode())
                curses.curs_set(0)
                stdscr.nodelay(1)
                stdscr.timeout(500)
            else:
                curses.curs_set(0)
                stdscr.nodelay(1)
                stdscr.timeout(500)

    curses.endwin()
    print("deckos-dashboard: disconnected")


def main():
    parser = argparse.ArgumentParser(description="DeckOS Dashboard")
    parser.add_argument("port", nargs="?", default=None, help="Serial port (auto-detect if omitted)")
    args = parser.parse_args()

    port = args.port

    # Auto-detect on Linux
    if port is None and sys.platform.startswith("linux"):
        import glob
        candidates = sorted(glob.glob("/dev/serial/by-id/*DeckOS*") or
                           glob.glob("/dev/ttyACM*") or
                           glob.glob("/dev/ttyUSB*"))
        if candidates:
            port = candidates[0]
            print(f"deckos-dashboard: detected {port}")

    if port is None and sys.platform == "darwin":
        import glob
        candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
        if candidates:
            port = candidates[0]
            print(f"deckos-dashboard: detected {port}")

    if port is None:
        print("deckos-dashboard: no serial port specified and auto-detect failed")
        print("usage: deckos-dashboard.py [port]")
        print("  e.g. deckos-dashboard.py /dev/ttyACM0")
        sys.exit(1)

    deck = DeckOSSerial(port)
    if not deck.connect():
        sys.exit(1)

    print("deckos-dashboard: connected, launching monitor (Ctrl+C to quit)...")

    try:
        run_curses(deck)
    except KeyboardInterrupt:
        pass
    finally:
        deck.close()


if __name__ == "__main__":
    main()
