# DeckOS

> A lightweight shell OS for the Raspberry Pi Pico.

```
  ╔══════════════════════════════════╗
  ║           DeckOS v1.7            ║
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
  - [Servo](#servo)
  - [Audio & Signalling](#audio--signalling)
  - [Scripting & Automation](#scripting--automation)
  - [System](#system)
  - [Subsystems](#subsystems)
  - [WiFi / ESP8266](#wifi--esp8266)
  - [Bluetooth / HC-05](#bluetooth--hc-05)
  - [Filesystem](#filesystem)
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
- **50+ built-in commands** covering GPIO, ADC, PWM, I²C, SPI, UART, audio, scripting, and system info
- **Dual-core scheduler** — background tasks on Core 1, shell on Core 0, completely independent
- **Persistent config** — save your hostname, CPU speed, and boot settings to flash; they survive reboots
- **Ring syslog** — 64-entry in-memory log with DEBUG/INFO/WARN/ERR levels and colour output
- **GPIO IRQ monitor** — watch a pin in real time, get timestamped edge events
- **Logic analyser** — sample a GPIO pin and render a timing diagram with edge count, duty cycle, and frequency estimate
- **Tone and melody engine** — drive a passive buzzer with musical note names or raw Hz values; built-in Für Elise and Canon in D presets
- **Interactive piano** — play the buzzer live from the keyboard
- **Servo control** — set angle, blocking sweep, or background sweep/goto
- **SPI and UART** — raw SPI transfers and a USB-to-UART passthrough bridge
- **Raw flash access** — read, erase, and program flash sectors directly
- **Device detection** — scan and report connected peripherals
- **Heap tracker** — live allocator stats and outstanding allocation list
- **Benchmark tool** — measure how fast any command runs
- **Live top** — real-time task monitor with CPU usage per Core 1 task
- **Morse code** — blink the onboard LED in morse at any WPM
- **ADC averaging** — clean up noisy readings with configurable sample counts
- **Three boot modes** — normal, recovery, and USB DFU for reflashing
- **I²C tools** — scan the bus, read and write registers
- **WiFi via ESP8266** — scan networks, connect to WiFi, run commands wirelessly through a bridge firmware
- **Bluetooth via HC-05** — wireless shell terminal, remote command execution, file transfer, syslog mirroring *(beta)*

---

## What you need

| Thing | Detail |
|---|---|
| Board | Raspberry Pi Pico (RP2040) |
| Connection | USB to your computer |
| Optional | Passive buzzer on any GPIO pin (for `tone` / `melody` / `piano`) |
| Optional | I²C device on GP4 (SDA) and GP5 (SCL) |
| Optional | SPI device; default pins GP2 (SCK), GP3 (MOSI), GP4 (MISO) |
| Optional | Servo on any GPIO pin |
| Optional | A button wired from GP15 to GND (for recovery mode) |
| Optional | ESP8266 module on UART1 (GP5 TX, GP4 RX) for WiFi |
| Optional | HC-05 Bluetooth module on UART0 for wireless shell |

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

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v1.7 or newer
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
| `top` | `top` | Live task monitor showing Core 1 CPU usage per task; press any key to exit |

### Hardware

| Command | Usage | What it does |
|---|---|---|
| `temp` | `temp` | Internal chip temperature in °C and °F |
| `mem` | `mem` | Available heap and flash sizes |
| `memmap` | `memmap` | Full memory map with SRAM sections and peripheral addresses |
| `free` | `free` | Heap allocator stats and list of live allocations |
| `led` | `led <on\|off\|toggle\|blink [n]>` | Control the onboard LED (GP25). `blink n` blinks it n times |
| `gpio` | `gpio <read\|write\|mode\|irq> <pin> [val]` | Read, write, set direction, or monitor a GPIO pin |
| `pwm` | `pwm <pin> <0–100>` | Set PWM duty cycle on any pin |
| `adc` | `adc <0\|1\|2>` | Read ADC channel 0–2 (GP26–28), shows raw value and voltage |
| `avg` | `avg <ch> [samples]` | Same as `adc` but averages multiple samples to reduce noise |
| `pull` | `pull <pin> <up\|down\|none>` | Set a pin's pull resistor |
| `clock` | `clock [mhz]` | Get or set CPU speed (48–200 MHz) |
| `i2c` | `i2c scan \| read <addr> <reg> \| write <addr> <reg> <val>` | I²C bus tools (GP4=SDA, GP5=SCL) |
| `spi` | `spi init \| write \| read \| xfer` | SPI bus operations (see below) |
| `uart` | `uart <baud> <tx_pin> <rx_pin> [timeout_s]` | Bridge USB-CDC to a hardware UART; press Ctrl-X to exit |
| `pin` | `pin` | Dump the current state of all GPIO pins |
| `pinout` | `pinout` | ASCII Pico pinout diagram with live pin states, directions, and functions |
| `flash` | `flash read \| write \| erase <addr>` | Raw flash read/write/erase (see below) |
| `detect` | `detect` | Scan and report connected devices |
| `la` | `la <pin> [samples] [us_per_sample]` | Logic analyser — sample a pin and render a timing diagram |

#### Live GPIO monitoring

```bash
> gpio irq 15           # watch GP15 for edges (30 s default)
> gpio irq 15 60        # watch for up to 60 s
> gpio irq 15 dump      # print everything captured so far
> gpio irq 15 stop      # stop watching
```

#### SPI bus

SPI must be initialised before use. Default pins are GP2 (SCK), GP3 (MOSI), GP4 (MISO) at 1 MHz.

```bash
> spi init                          # default pins and baud
> spi init 2 3 4 500000             # custom: sck mosi miso baud
> spi write <cs_pin> <hex bytes>    # write bytes to a device
> spi read  <cs_pin> <reg_hex> [n]  # read n bytes from a register
> spi xfer  <cs_pin> <hex bytes>    # full-duplex transfer, prints RX
```

#### UART passthrough

Bridges the USB serial port to a hardware UART. Useful for talking to other devices from the DeckOS shell.

```bash
> uart 9600 0 1          # 9600 baud, TX=GP0, RX=GP1
> uart 115200 4 5 30     # 115200 baud, TX=GP4, RX=GP5, 30s timeout
```

UART0 TX pins: GP0, GP12, GP16. UART1 TX pins: GP4, GP8. Press Ctrl-X to exit.

#### Flash access

Addresses are XIP offsets from `0x00000000` (start of flash). The config sector sits at `0x1FF000` — avoid erasing it manually.

```bash
> flash read  10000 64      # dump 64 bytes starting at offset 0x10000
> flash erase 10000         # erase the 4 KB sector containing 0x10000
> flash write 10000 DE AD   # write bytes (address must be 256-byte page-aligned)
```

#### Logic analyser

Samples a GPIO pin at a configurable rate and renders an ASCII timing diagram.

```bash
> la 15                  # 128 samples at 10 µs each (1.28 ms window)
> la 15 256 5            # 256 samples at 5 µs each (1.28 ms window)
```

After sampling, `la` prints a waveform diagram followed by edge count, duty cycle, window length, and an estimated frequency.

### Servo

| Command | Usage | What it does |
|---|---|---|
| `servo` | `servo <pin> <angle 0–180>` | Move a servo to an absolute angle |
| `servo sweep` | `servo sweep <pin> [from to step_ms]` | Blocking sweep from one angle to another |
| `servo bg` | `servo bg <pin> sweep [min max step step_ms]` | Background sweep (Core 1), non-blocking |
| `servo bg` | `servo bg <pin> goto <angle> [step_ms]` | Move to angle in background |
| `servo bg` | `servo bg <pin> stop` | Stop a background servo |
| `servo bg list` | `servo bg list` | List all active background servos |

```bash
> servo 16 90                       # centre a servo on GP16
> servo sweep 16 0 180 20           # sweep 0→180° with 20 ms steps
> servo bg 16 sweep 0 180 1 15      # background sweep, 1° steps every 15 ms
> servo bg 16 goto 45               # move to 45° in background
> servo bg 16 stop
```

### Audio & Signalling

| Command | Usage | What it does |
|---|---|---|
| `tone` | `tone <pin> <note\|hz> [ms]` | Play a tone on a buzzer — use a note name like `C4` or `A#3`, or a raw Hz value |
| `melody` | `melody <pin> <C4:200 E4:200 ...>` | Play a sequence of notes. Format is `NOTE:duration_ms`, use `REST` for silence |
| `melody` | `melody <pin> elise` | Play the full Für Elise arrangement |
| `melody` | `melody <pin> canon` | Play the full Canon in D arrangement |
| `morse` | `morse <text> [wpm]` | Blink the onboard LED in morse code (default 13 WPM) |
| `piano` | `piano <pin> [duration_ms]` | Interactive keyboard piano — drive a buzzer from keypresses |

```bash
> tone 16 A4 500
> tone 16 440 500
> melody 16 C4:200 E4:200 G4:400 REST:100 C5:600
> melody 16 elise
> melody 16 canon
> morse SOS
> morse HELLO 20
> piano 16
> piano 16 200          # 200 ms per note
```

#### Piano keyboard layout

```
Black keys:  W  E     T  Y  U     O  P   (sharps/flats)
White keys: A  S  D  F  G  H  J  K  L  ;
```

`[` and `]` shift the base octave down and up. Press `q` to quit.

### Scripting & Automation

| Command | Usage | What it does |
|---|---|---|
| `sleep` | `sleep <ms>` | Wait for a number of milliseconds |
| `repeat` | `repeat <n> <command>` | Run a command n times in a row |
| `watch` | `watch <ms> <command>` | Run a command repeatedly at an interval; press any key to stop |
| `trigger` | `trigger <pin> <rise\|fall\|both> <command>` | Watch a pin and run a command the moment an edge fires (one-shot) |
| `cron` | `cron <delay_ms> <command>` | Wait a set time, then run a command once |
| `bench` | `bench <iterations> <command>` | Run a command many times and report throughput and timing |

```bash
> watch 1000 temp
> watch 500 adc 0
> trigger 15 fall led on
> repeat 5 led toggle
> cron 5000 reboot
> bench 1000 echo hi
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

## WiFi / ESP8266

DeckOS supports WiFi through an ESP8266 module running the **DeckOS Bridge firmware** — a small Arduino sketch that lives on the ESP8266 and translates DeckOS commands into WiFi actions. This lets the Pico connect to networks, scan for access points, and in the future send and receive data over the internet, all from the DeckOS shell.

### What WiFi lets you do

- **Scan nearby networks** — see every access point in range with signal strength and security type. Useful for site surveys, finding a specific AP, or just confirming your router is visible
- **Connect to WiFi** — join a network so the ESP8266 has an IP address, opening the door to network-based features
- **Remote monitoring** — combined with future HTTP support, you'll be able to push sensor readings (temperature, ADC values) to a server or dashboard without a USB cable anywhere near the Pico
- **Wireless data logging** — log GPIO events, ADC samples, or syslog entries to a remote server rather than to RAM
- **Home automation** — trigger GPIO outputs, run shell commands, or control servos in response to network events
- **Headless deployment** — once configured, the Pico can operate without a USB connection and still participate in your network

### Wiring

| ESP8266 pin | Pico pin | Notes |
|---|---|---|
| TX | GP5 | |
| RX | GP4 | |
| VCC / 3V3 | External 3.3V | Use a dedicated regulator; the Pico's 3V3 pin may brown out under WiFi load |
| GND | GND | Common ground required |
| EN / CH_PD | 3.3V | Must be HIGH for the module to boot |

### Bridge firmware setup

Flash `ESP8266_DeckOS_Bridge.ino` to the ESP8266 using the Arduino IDE with the ESP8266 board package installed. Before flashing, edit the credentials at the top of the sketch:

```cpp
String wifi_ssid     = "YourNetworkName";
String wifi_password = "YourPassword";
```

The bridge starts in auto-detect mode and responds to `@`-prefixed control commands from the Pico.

### WiFi commands

| Command | What it does |
|---|---|
| `wifi init` | Initialise the ESP8266 on UART1 at 115200 baud |
| `wifi init <baud>` | Initialise at a custom baud rate |
| `wifi status` | Show UART config and ready state |
| `wifi ping` | Query the bridge and show its current status |
| `wifi scan` | Scan for WiFi networks via the bridge |
| `wifi shell` | Drop into a raw interactive shell with the ESP8266 |
| `wifi deinit` | Release UART1 from the ESP8266 |
| `wifi bridge status` | Show bridge mode, WiFi connection state, and IP address |
| `wifi bridge scan` | Scan for nearby WiFi networks |
| `wifi bridge connect` | Connect using the credentials stored in the bridge firmware |
| `wifi bridge reset` | Reboot the ESP8266 |
| `wifi bridge auto` | Switch bridge to auto-detect firmware mode |
| `wifi bridge at` | Switch bridge to raw AT command passthrough |
| `wifi bridge raw` | Switch bridge to raw command mode |

### Typical workflow

```bash
> wifi init                  # start the UART link to the ESP8266
> wifi bridge status         # confirm the bridge is alive and check WiFi state
> wifi bridge scan           # see what networks are in range
> wifi bridge connect        # connect using credentials in the bridge firmware
> wifi bridge status         # confirm connection and get the IP address
```

If the credentials in the sketch are correct and the network is in range, `wifi bridge connect` will print a series of dots followed by the assigned IP address. From there `wifi bridge status` shows the full connection summary.

---

## Bluetooth / HC-05

DeckOS supports wireless shell access through an HC-05 Bluetooth module. Once paired, you can control the Pico from a phone or laptop terminal app without touching a USB cable.

### What Bluetooth lets you do

- **Wireless shell** — run any DeckOS command from a Bluetooth terminal on your phone or laptop, exactly as if you were on the USB serial console
- **Remote command execution** — send a single command from the BT side and get the output back without entering the full shell
- **Live stats streaming** — stream `top`-style CPU and temperature data to a connected Bluetooth client at a configurable interval
- **Syslog mirroring** — automatically forward every log entry to the Bluetooth terminal as it happens
- **File transfer** — send and receive VFS files over the Bluetooth link

### Wiring

HC-05 VCC -> VSYS (worked for me) / 5V / VBUS
HC-05 GND -> GND 
RXD -> GP 01
TXD -> GP 00

Power the HC-05 from 3.3V or 5V depending on your module variant. Most bare HC-05 boards accept 3.6–6V on VCC with a built-in regulator.

### Bluetooth commands

| Command | What it does |
|---|---|
| `bt init [baud]` | Initialise the HC-05 UART (default baud from config) |
| `bt status` | Show init state, connection state, log mirror state |
| `bt shell` | Start a full wireless DeckOS terminal over Bluetooth |
| `bt exec <command>` | Run one command and send the output to the BT client |
| `bt top [ms]` | Stream live CPU/temp stats to the BT client |
| `bt log on` | Mirror all syslog entries to the BT client in real time |
| `bt log off` | Stop mirroring |
| `bt send <file>` | Send a VFS file over Bluetooth |
| `bt recv <file>` | Receive a file from Bluetooth into VFS |
| `bt sniff [s]` | Raw byte sniffer — print everything the module sends |
| `bt at` | Drop into interactive AT command mode |
| `bt name <name>` | Set the HC-05 module name (requires AT mode) |
| `bt pin <code>` | Set the pairing PIN (requires AT mode) |
| `bt baud <rate>` | Change the HC-05 UART baud rate (requires AT mode) |

### Typical workflow

```bash
> bt init                    # start UART link to HC-05
> bt status                  # confirm it's ready
> bt shell                   # open wireless terminal — connect from your phone now
```

On the phone side, pair with the HC-05 (default PIN is usually `1234` or `0000`), then open any Bluetooth serial terminal app and connect. You'll get a full `>` prompt.

To set a custom name and PIN before deploying:

```bash
> bt at                      # enter AT mode (HC-05 must have KEY pin HIGH at power-on)
AT+NAME=DeckOS
AT+PSWD=9876
EXIT
```

---

## Buzzer setup

The `tone`, `melody`, and `piano` commands use the RP2040's hardware PWM to drive a **passive buzzer**. It works well and sounds perfectly fine for a microcontroller.

**One important thing:** you need a *passive* buzzer, not an active one. An active buzzer has a built-in oscillator — it just beeps at one fixed pitch when you apply power and ignores the PWM frequency entirely. A passive buzzer is just a bare piezo element with no internal circuitry, and that's what responds to DeckOS's PWM signal.

Not sure which you have? Apply 3.3V DC to it. If it beeps on its own, it's active. If it stays silent, it's passive — that's the right one.

**Wiring:**

```
GPIO pin ──── [100Ω resistor] ──── (+) Buzzer (−) ──── GND
```

Any GPIO pin works. GP16 is a good default. The 100 Ω resistor is optional but it protects the pin.

**Supported notes** use standard scientific pitch notation — C3 up through B5, sharps and flats included (`Bb4` is the same as `A#4`). The piano command covers C0 through C7. Use `REST` for silence.

```bash
> melody 16 C4:200 D4:200 E4:200 F4:200 G4:200 A4:200 B4:200 C5:400
> tone 16 440 1000
> melody 16 elise
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
> top                   # watch live CPU usage per task
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

## Filesystem

DeckOS includes a small in-memory virtual filesystem (VFS) for storing files and directories across the session. It does not persist across reboots — it lives in SRAM and is wiped on power-off.

| Command | What it does |
|---|---|
| `ls [path]` | List directory contents |
| `cat <file>` | Print file contents |
| `touch <file>` | Create an empty file |
| `mkdir <dir>` | Create a directory |
| `rm [-r] <path>` | Remove a file or directory |
| `write <file> <text>` | Overwrite a file with text |
| `append <file> <text>` | Append text to a file |
| `hexdump <file>` | Hex + ASCII dump of a file |
| `cd [dir]` | Change working directory |
| `pwd` | Print working directory |
| `cp <src> <dst>` | Copy a file |
| `mv <src> <dst>` | Move or rename a file |
| `stat <path>` | Show file or directory metadata |
| `wc <file>` | Count lines, words, and bytes |
| `grep <pattern> <file>` | Search a file for a pattern |
| `find [name]` | Recursive name search |
| `df` | Filesystem usage summary |
| `tree` | Print the full directory tree |

---

# TO-DO

- Custom HTTP server, runs on wifi serve turns pico into a tiny web endpoint
- HTTP GET/POST support with wifi commands, `wifi get <url>` or `wifi post <url>`

*Who doesn't love a decent shell?*