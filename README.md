# DeckOS

> A lightweight shell OS for the Raspberry Pi Pico.

```
  ╔══════════════════════════════════╗
  ║           DeckOS v1.2.0          ║
  ║      Raspberry Pi Pico / RP2040  ║
  ╚══════════════════════════════════╝
```

DeckOS is a bare-metal shell that runs directly on the Raspberry Pi Pico. Plug it in, open a serial terminal, and you get a proper interactive shell — type commands to control GPIO pins, read sensors, play tones on a buzzer, monitor the I²C bus, schedule background tasks, and a lot more. No Linux, no RTOS, no host required.

It's built around a clean kernel/driver/scheduler architecture that uses both RP2040 cores: the shell and all your commands live on Core 0, while background tasks run quietly on Core 1.

> **Platform support:** Raspberry Pi Pico (RP2040) for now. Arduino and ESP32 support is planned down the road.

---

## Table of Contents

- [What it can do](#what-it-can-do)
- [What you need](#what-you-need)
- [How it's structured](#how-its-structured)
- [Getting started](#getting-started)
- [Using the shell](#using-the-shell)
- [Commands](#commands)
  - [Core / Info](#core--info)
  - [Hardware](#hardware)
  - [Audio & Signalling](#audio--signalling)
  - [Scripting & Automation](#scripting--automation)
  - [System](#system)
  - [Subsystems](#subsystems)
- [Buzzer setup](#buzzer-setup)
- [Config system](#config-system)
- [Syslog](#syslog)
- [Scheduler](#scheduler)
- [Boot modes](#boot-modes)
- [Drivers](#drivers)
- [Project layout](#project-layout)

---

## What it can do

- **Interactive shell** over USB serial — command history, arrow-key navigation, Ctrl-C/D/L
- **35+ built-in commands** covering GPIO, ADC, PWM, I²C, audio, scripting, and system info
- **Dual-core scheduler** — background tasks on Core 1, shell on Core 0, completely independent
- **Persistent config** — save your hostname, CPU speed, and boot settings to flash; they survive reboots
- **Ring syslog** — 64-entry in-memory log with DEBUG/INFO/WARN/ERR levels and colour output
- **GPIO IRQ monitor** — watch a pin in real time, get timestamped edge events
- **Tone and melody engine** — drive a passive buzzer with musical note names or raw Hz values
- **Morse code** — blink the onboard LED in morse at any WPM
- **ADC averaging** — clean up noisy readings with configurable sample counts
- **Three boot modes** — normal, recovery, and USB DFU for reflashing
- **I²C tools** — scan the bus, read and write registers

---

## What you need

| Thing | Detail |
|---|---|
| Board | Raspberry Pi Pico (RP2040) |
| Connection | USB to your computer |
| Optional | Passive buzzer on any GPIO pin (for `tone` / `melody`) |
| Optional | I²C device on GP4 (SDA) and GP5 (SCL) |
| Optional | A button wired from GP15 to GND (for recovery mode) |

---

## How it's structured

```
┌─────────────────────────────────────────────┐
│                  Core 0                     │
│   kernel_init() → shell_run() (main loop)   │
│   commands, I/O, user interaction           │
└────────────────────┬────────────────────────┘
                     │  multicore_launch_core1()
┌────────────────────▼────────────────────────┐
│                  Core 1                     │
│   scheduler → background tasks              │
│   (e.g. heartbeat LED every 1000 ms)        │
└─────────────────────────────────────────────┘

Boot order:
  kernel_init()
    ├── syslog_init()
    ├── bootloader_run()   (mode detect, config load, banner)
    ├── drivers_init_all() (adc, gpio, pwm, i2c0)
    ├── sched_init()       (launches Core 1)
    └── shell_init()       (registers commands, prints prompt)

  kernel_run()  [infinite loop]
    └── shell_run()        (non-blocking input → parse → dispatch)
```

---

## Getting started

### Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v1.5 or newer
- CMake 3.13+
- ARM GCC toolchain (`arm-none-eabi-gcc`)

### Build and flash

```bash
git clone https://github.com/enginestein/DeckOS
cd DeckOS
make
make flash
```

### Connect

```bash
# Linux / macOS
minicom -b 115200 -D /dev/ttyACM0

# Windows — PuTTY or Tera Term, COMx, 115200 8N1

# Or simply:
make monitor
```

You'll see the boot banner and a `>` prompt. You're in.

---

## Using the shell

```
> help          ← list everything
> sysinfo       ← board summary
> temp          ← core temperature
> led blink 3   ← blink the LED 3 times
> gpio read 15  ← read GP15
```

### Keyboard shortcuts

| Key | What it does |
|---|---|
| `↑` / `↓` | Browse command history (last 16 commands) |
| `Backspace` | Delete a character |
| `Ctrl-C` | Cancel what you're typing |
| `Ctrl-D` | Quick shortcut for `uptime` |
| `Ctrl-L` | Clear the screen |

---

## Commands

### Core / Info

| Command | Usage | What it does |
|---|---|---|
| `help` | `help` | List all commands |
| `version` | `version` | Show version and build date |
| `clear` | `clear` | Clear the screen |
| `echo` | `echo <text>` | Print text back |
| `uptime` | `uptime` | Time since last boot |
| `sysinfo` | `sysinfo` | Board, CPU, RAM, temp, uptime — everything at once |
| `stats` | `stats` | Command counts, CPU speed, temperature |

### Hardware

| Command | Usage | What it does |
|---|---|---|
| `temp` | `temp` | Internal chip temperature in °C and °F |
| `mem` | `mem` | Available heap and flash sizes |
| `memmap` | `memmap` | Full memory map with SRAM sections and peripheral addresses |
| `led` | `led <on\|off\|toggle\|blink [n]>` | Control the onboard LED (GP25). `blink n` blinks it n times |
| `gpio` | `gpio <read\|write\|mode\|irq> <pin> [val]` | Read, write, set direction, or monitor a GPIO pin |
| `pwm` | `pwm <pin> <0–100>` | Set PWM duty cycle on any pin |
| `adc` | `adc <0\|1\|2>` | Read ADC channel 0–2 (GP26–28), shows raw value and voltage |
| `avg` | `avg <ch> [samples]` | Same as `adc` but averages multiple samples to reduce noise |
| `pull` | `pull <pin> <up\|down\|none>` | Set a pin's pull resistor |
| `clock` | `clock [mhz]` | Get or set CPU speed (48–200 MHz) |
| `i2c` | `i2c scan \| read <addr> <reg> \| write <addr> <reg> <val>` | I²C bus tools (GP4=SDA, GP5=SCL) |
| `pin` | `pin` | Dump the current state of all GPIO pins |

#### Live GPIO monitoring

```bash
> gpio irq 15           # watch GP15 for edges (30 s default)
> gpio irq 15 60        # watch for up to 60 s
> gpio irq 15 dump      # print everything captured so far
> gpio irq 15 stop      # stop watching
```

### Audio & Signalling

| Command | Usage | What it does |
|---|---|---|
| `tone` | `tone <pin> <note\|hz> [ms]` | Play a tone on a buzzer — use a note name like `C4` or `A#3`, or a raw Hz value |
| `melody` | `melody <pin> <C4:200 E4:200 ...>` | Play a sequence of notes. Format is `NOTE:duration_ms`, use `REST` for silence |
| `morse` | `morse <text> [wpm]` | Blink the onboard LED in morse code (default 13 WPM) |

```bash
> tone 16 A4 500
> tone 16 440 500
> melody 16 C4:200 E4:200 G4:400 REST:100 C5:600
> morse SOS
> morse HELLO 20
```

### Scripting & Automation

| Command | Usage | What it does |
|---|---|---|
| `sleep` | `sleep <ms>` | Wait for a number of milliseconds |
| `repeat` | `repeat <n> <command>` | Run a command n times in a row |
| `watch` | `watch <ms> <command>` | Run a command repeatedly at an interval; press any key to stop |
| `trigger` | `trigger <pin> <rise\|fall\|both> <command>` | Watch a pin and run a command the moment an edge fires (one-shot) |
| `cron` | `cron <delay_ms> <command>` | Wait a set time, then run a command once |

```bash
> watch 1000 temp
> watch 500 adc 0
> trigger 15 fall led on
> repeat 5 led toggle
> cron 5000 reboot
```

### System

| Command | Usage | What it does |
|---|---|---|
| `reboot` | `reboot` | Reboot after 1 second |
| `dfu` | `dfu` | Jump into USB bootloader mode for reflashing |
| `uid` | `uid` | Print the board's unique 64-bit ID |
| `wdog` | `wdog` | Check if the last reboot was caused by the watchdog |

### Subsystems

| Command | Usage | What it does |
|---|---|---|
| `drivers` | `drivers` | Show all drivers and whether they initialised cleanly |
| `tasks` | `tasks [enable\|disable <id>]` | View or toggle background scheduler tasks |
| `config` | `config show\|set <key> <val>\|save\|reset` | Manage persistent settings |
| `syslog` | `syslog show [n]\|warn\|err\|write <tag> <msg>\|clear\|stats` | Read and manage the in-memory log |

**Config keys you can set:**

| Key | Values | Effect |
|---|---|---|
| `hostname` | any string | Name shown at boot |
| `cpu_mhz` | 48–200, or 0 for default 125 | CPU speed set at boot |
| `boot_led` | 0 or 1 | Whether the LED turns on at boot |

```bash
> config set hostname my-pico
> config set cpu_mhz 133
> config set boot_led 1
> config save
> config show
> config reset
```

---

## Buzzer setup

The `tone` and `melody` commands use the RP2040's hardware PWM to drive a **passive buzzer**. It works well and sounds perfectly fine for a microcontroller.

**One important thing:** you need a *passive* buzzer, not an active one. An active buzzer has a built-in oscillator — it just beeps at one fixed pitch when you apply power and ignores the PWM frequency entirely. A passive buzzer is just a bare piezo element with no internal circuitry, and that's what responds to DeckOS's PWM signal.

Not sure which you have? Apply 3.3V DC to it. If it beeps on its own, it's active. If it stays silent, it's passive — that's the right one.

**Wiring:**

```
GPIO pin ──── [100Ω resistor] ──── (+) Buzzer (−) ──── GND
```

Any GPIO pin works. GP16 is a good default. The 100 Ω resistor is optional but it protects the pin.

**Supported notes** use standard scientific pitch notation — C3 up through B5, sharps and flats included (`Bb4` is the same as `A#4`). Use `REST` for silence.

```bash
> melody 16 C4:200 D4:200 E4:200 F4:200 G4:200 A4:200 B4:200 C5:400
> tone 16 440 1000
```

---

## Config system

Settings live in the **last 4 KB of flash** (`0x101FF000`), protected by a CRC32 checksum. On a fresh board, the checksum won't match so DeckOS falls back to defaults quietly. Nothing breaks, it just uses sensible starting values.

Changes from `config set` are only in RAM until you run `config save`. CPU speed and LED state changes take effect on the next boot.

---

## Syslog

DeckOS keeps a **64-entry ring log** in memory. When it fills up, the oldest entries get overwritten. It's wiped on reboot.

```bash
> syslog show           # everything
> syslog show 20        # last 20 entries
> syslog warn           # WARN and above only
> syslog err            # errors only
> syslog write myapp "something happened"
> syslog stats
> syslog clear
```

Levels: `DBG` (grey) → `INF` (white) → `WRN` (yellow) → `ERR` (red).

---

## Scheduler

Background tasks run on **Core 1**, completely separate from your shell. Each task has a name, a function, and a repeat interval in milliseconds. The scheduler checks every 100 µs and fires tasks when their time is up.

The built-in `heartbeat` task toggles the onboard LED every second. If you use `led on` or `led off` manually, it just overwrites the same pin — last write wins.

```bash
> tasks                 # see all tasks
> tasks disable 0       # stop the heartbeat
> tasks enable 0        # bring it back
```

---

## Boot modes

| Mode | How to trigger | What happens |
|---|---|---|
| **Normal** | Just power on | Full shell, all drivers loaded |
| **Recovery** | Hold GP15 low at boot | Shell starts with a warning banner — handy for emergency config resets |
| **DFU** | Run `dfu` in the shell | Jumps into USB bootloader so you can reflash |

---

## Drivers

Drivers are registered and initialised in order at boot. Each one reports OK or FAIL.

| Driver | What it covers |
|---|---|
| `adc` | ADC channels + internal temperature sensor |
| `gpio` | GPIO (placeholder, the SDK handles most of this) |
| `pwm` | PWM (placeholder) |
| `i2c0` | I²C bus 0 at 100 kHz on GP4/GP5 |

```bash
> drivers
```

---

## Project layout

```
DeckOS/
├── main.c
├── kernel.c / .h
├── shell.c / .h
├── commands.c / .h
├── bootloader.c / .h
├── config.c / .h
├── scheduler.c / .h
├── drivers.c / .h
├── syslog.c / .h
├── gpio_mon.c / .h
├── morse.c / .h
├── tone.c / .h
└── CMakeLists.txt
```

*Who doesn't love a decent shell?*