# DeckOS

A bare metal shell OS, currently compatible for RP2040.

> **ESP32 port also available** — see [DeckOS_ESP32](https://github.com/enginestein/DeckOS_ESP32) for the ESP32 (ESP-IDF / FreeRTOS) port with native WiFi, ESP-NOW, camera support, NRF24L01 radio, and SPIFFS file persistence. See the [ESP32 Port](#esp32-port) section below for details.


---

## Table of Contents

- [What you need](#what-you-need)
- [How it's structured](#how-its-structured)
- [Getting started](#getting-started)
- [Using the shell](#using-the-shell)
- [Commands](#commands)
  - [Core / Info](#core--info)
  - [Hardware](#hardware)
  - [Probes & Analysis](#probes--analysis)
  - [OLED / SSD1306](#oled--ssd1306)
  - [Servo](#servo)
  - [Audio & Signalling](#audio--signalling)
  - [Scripting & Automation](#scripting--automation)
  - [System](#system)
  - [Subsystems](#subsystems)
  - [WiFi / ESP8266](#wifi--esp8266)
  - [MQTT](#mqtt)
  - [Swarm / ESP-NOW](#swarm--esp-now)
  - [Bluetooth / HC-05](#bluetooth--hc-05)
  - [Filesystem](#filesystem)
- [Modules (on-demand RAM)](#modules-on-demand-ram)
- [USB portable OS](#usb-portable-os)
  - [USB mass storage (portable disk)](#usb-mass-storage-portable-disk)
  - [USB keyboard (HID)](#usb-keyboard-hid)
  - [Standalone handheld (OLED console)](#standalone-handheld-oled-console)
- [DeckScript](#deckscript)
  - [Running scripts](#running-scripts)
  - [Variables](#variables)
  - [Arithmetic](#arithmetic)
  - [String functions](#string-functions)
  - [Math functions](#math-functions)
  - [Control flow](#control-flow)
  - [Loops](#loops)
  - [Arrays](#arrays)
  - [Functions](#functions)
  - [Hardware access](#hardware-access-from-scripts)
  - [I/O](#io)
  - [Logging and assertions](#logging-and-assertions)
  - [Includes](#includes)
  - [Example scripts](#example-scripts)
- [Buzzer setup](#buzzer-setup)
- [Config system](#config-system)
- [Syslog](#syslog)
- [Scheduler](#scheduler)
- [Boot modes](#boot-modes)
- [Drivers](#drivers)
- [Project layout](#project-layout)

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
│   scheduler → background tasks & jobs       │
│   (servo sweeps, la trigger capture, ...)   │
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

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v3.0 or newer
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

# Windows - PuTTY or Tera Term, COMx, 115200 8N1

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
| `↑` / `↓` | Browse command history (last 16 commands; see also the `history` command) |
| `Backspace` | Delete a character |
| `Ctrl-C` | Cancel what you're typing |
| `Ctrl-D` | Quick shortcut for `uptime` |
| `Ctrl-L` | Clear the screen |

---

## Commands

### Core / Info

| Command | Usage | What it does |
|---|---|---|
| `help` | `help` | List all command groups; `help <group>` to list a specific group |
| `version` | `version` | Show OS version and build date |
| `clear` | `clear` | Clear the screen |
| `echo` | `echo <text>` | Print text back |
| `uptime` | `uptime` | Time since last boot |
| `sysinfo` | `sysinfo` | Board, CPU, RAM, temp, uptime -- everything at once |
| `stats` | `stats` | Command counts, CPU speed, temperature |
| `top` | `top` | Live task monitor showing Core 1 CPU usage per task; press any key to exit |
| `jobs` | `jobs` | List all active background Core 1 jobs |
| `jobs` | `jobs cancel <id>` | Cancel a specific background job |

### Hardware

| Command | Usage | What it does |
|---|---|---|
| `temp` | `temp` | Internal chip temperature in °C and °F |
| `mem` | `mem` | Available heap and flash sizes |
| `memmap` | `memmap` | Full memory map with SRAM sections, stack, heap, and peripheral addresses |
| `free` | `free` | Heap allocator stats and a list of every live allocation |
| `stack` | `stack` | Core 0 stack depth, current usage percentage, free headroom, and canary high-water mark |
| `power` | `power` | VSYS voltage via ADC3 (GP29), source detection, and LiPo / AA battery percentage estimate |
| `led` | `led <on\|off\|toggle\|blink [n]>` | Control the onboard LED (GP25). `blink n` blinks it n times |
| `gpio` | `gpio <read\|write\|mode\|irq> <pin> [val]` | Read, write, set direction, or monitor a GPIO pin |
| `pwm` | `pwm <pin> <duty 0–100> [freq_hz]` | Set PWM duty cycle on any pin; optional frequency argument |
| `adc` | `adc <0\|1\|2>` | Read ADC channel 0–2 (GP26–28), shows raw value and voltage |
| `avg` | `avg <ch> [samples]` | Same as `adc` but averages multiple samples; shows min, max, and peak-to-peak noise |
| `pull` | `pull <pin> <up\|down\|none>` | Set a pin's pull resistor |
| `clock` | `clock [mhz]` | Get or set CPU speed (48–200 MHz) |
| `pin` | `pin` | Snapshot of all GPIO pin states (direction and value) |
| `pinout` | `pinout` | ASCII Pico pinout diagram with live pin states, directions, and functions |
| `uid` | `uid` | Print the board's unique 64-bit ID |
| `wdog` | `wdog` | Check if the last reboot was caused by the watchdog |
| `i2c` | `i2c scan [sda scl] \| read <addr> <reg> \| write <addr> <reg> <val> \| dump <addr>` | I²C bus tools (default GP4=SDA, GP5=SCL) |
| `spi` | `spi init \| write \| read \| xfer` | SPI bus operations (see below) |
| `uart` | `uart <baud> <tx_pin> <rx_pin> [timeout_s]` | Bridge USB-CDC to a hardware UART; press Ctrl-X to exit |
| `flash` | `flash read \| write \| erase <addr>` | Raw flash read/write/erase (see below) |

#### Stack inspection

`stack` reads the current stack pointer directly and compares it to the linker-defined `__StackTop` / `__StackLimit` symbols. It also walks upward from `__StackLimit` looking for a `0xDEADBEEF` canary pattern to report the peak usage high-water mark.

```bash
> stack
stack usage (Core 0):
  top        : 0x20041C00
  limit      : 0x20040000
  size       : 7168 B  (7 KB)
  SP now     : 0x20041800
  used       : 1024 B  (14.3%)
  free       : 6144 B
  peak used  : unknown (stack not pre-filled with canary)
```

#### Power / VSYS

`power` reads ADC channel 3 (GP29, the internal VSYS ÷ 3 tap) and back-calculates the supply voltage. It detects USB power, a 1S LiPo, or 3× AA cells and shows a percentage estimate where applicable.

```bash
> power
```

> GP29 must not be driven externally when using this command.

#### Live GPIO monitoring

```bash
> gpio irq 15           # watch GP15 for edges (30 s default)
> gpio irq 15 60        # watch for up to 60 s
> gpio irq 15 dump      # print everything captured so far
> gpio irq 15 stop      # stop watching
```

#### I²C bus

Default pins are GP4 (SDA) and GP5 (SCL). All subcommands accept optional `[sda] [scl]` pin overrides.

```bash
> i2c scan                      # scan on default pins
> i2c scan 6 7                  # scan on GP6/GP7
> i2c read  0x68 0x75           # read one byte
> i2c write 0x68 0x6B 0x00      # write one byte
> i2c dump  0x68                # dump all 256 registers
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

Addresses are XIP offsets from `0x00000000` (start of flash). The config sector is the **last 4 KB of flash** (e.g. `0x1FF000` on a 2 MB board, higher on larger parts) -- avoid erasing it manually. Run `flash` with no arguments to see the exact address detected for your board.

```bash
> flash read  10000 64      # dump 64 bytes starting at offset 0x10000
> flash erase 10000         # erase the 4 KB sector containing 0x10000
> flash write 10000 DE AD   # write bytes (address must be 256-byte page-aligned)
```

### Probes & Analysis

| Command | Usage | What it does |
|---|---|---|
| `la` | `la <pin> [samples] [us_per_sample] [trigger]` | Logic analyser -- sample a pin and render an ASCII timing diagram |
| `scope` | `scope <pin> <expected_hz> <duration_ms>` | Clean waveform viewer; auto-selects sample rate from expected frequency |
| `detect` | `detect [sda scl] \| uart <pin> [timeout_s] \| analyze <pin> [samples] [us]` | Scan I²C bus, probe for UART baud rate, or guess a protocol from logic samples |
| `imu` | `imu <read\|stream\|attitude\|calibrate\|raw\|whoami>` | MPU6050 accelerometer/gyro on I²C (GP4/GP5) |

#### Logic analyser

```bash
> la 15                    # 128 samples at 10 µs each
> la 15 256 5              # 256 samples at 5 µs each
> la 15 256 2 trigger      # background: wait for falling edge then capture
> jobs                     # check trigger status
> jobs cancel 0            # abort if needed
```

After sampling, `la` prints a waveform diagram followed by edge count, duty cycle, window length, and an estimated frequency. With `trigger`, the capture runs on Core 1 as a background job -- the shell stays responsive while it waits.

#### Scope

`scope` auto-calculates the sample interval from your expected frequency (targeting ~5 samples per cycle) and caps the sample count at 512. It renders a clean two-line waveform with a time-axis ruler below.

```bash
> scope 15 1000 50     # view a ~1 kHz signal for 50 ms
> scope 15 50 500      # view a ~50 Hz signal for 500 ms
```

#### Device detection

```bash
> detect                     # scan I²C and report known devices
> detect 6 7                 # scan on custom SDA/SCL pins
> detect uart 1              # probe GP1 for UART activity
> detect uart 1 5            # same, 5 second timeout
> detect analyze 15          # sample GP15 and guess the protocol
> detect analyze 15 256 5    # 256 samples at 5 µs each
```

### OLED / SSD1306

| Command | Usage | What it does |
|---|---|---|
| `oled init` | `oled init` | Initialise display on GP4 (SDA) / GP5 (SCL) |
| `oled on / off` | `oled on` | Power display on or off |
| `oled clear` | `oled clear` | Blank framebuffer and flush |
| `oled fill` | `oled fill <hex>` | Fill framebuffer with a pattern byte (e.g. `AA`) and flush |
| `oled flush` | `oled flush` | Push framebuffer to screen |
| `oled contrast` | `oled contrast <0–255>` | Set brightness |
| `oled invert` | `oled invert <0\|1>` | Invert display colours |
| `oled flip` | `oled flip <h:0\|1> <v:0\|1>` | Mirror display horizontally and/or vertically |
| `oled text` | `oled text <col> <row> <str>` | Draw text at a character-grid cell |
| `oled textxy` | `oled textxy <x> <y> <str>` | Draw text at pixel coordinates |
| `oled printf` | `oled printf <col> <row> <fmt...>` | Formatted text at a grid cell |
| `oled pixel` | `oled pixel <x> <y> <0\|1>` | Set or clear a single pixel |
| `oled line` | `oled line <x0> <y0> <x1> <y1>` | Draw a line |
| `oled hline` | `oled hline <x0> <x1> <y>` | Horizontal line |
| `oled vline` | `oled vline <x> <y0> <y1>` | Vertical line |
| `oled rect` | `oled rect <x> <y> <w> <h>` | Rectangle outline |
| `oled rectfill` | `oled rectfill <x> <y> <w> <h>` | Filled rectangle |
| `oled circle` | `oled circle <cx> <cy> <r>` | Circle outline |
| `oled circlefill` | `oled circlefill <cx> <cy> <r>` | Filled circle |
| `oled progress` | `oled progress <x> <y> <w> <h> <%>` | Progress bar |
| `oled title` | `oled title <text>` | Title bar at row 0 (inverted) |
| `oled status` | `oled status <left> <right>` | Status bar on bottom row |
| `oled splash` | `oled splash <line1> <line2>` | Splash screen and flush |
| `oled notify` | `oled notify <msg> <ms>` | Timed notification overlay (100–30000 ms) |
| `oled scroll` | `oled scroll <right\|left> <sp> <ep>` | Hardware scroll between pages sp–ep |
| `oled scroll stop` | `oled scroll stop` | Stop hardware scroll |
| `oled spinner` | `oled spinner <x> <y> <frame 0–7>` | Spinner glyph at pixel position |
| `oled boot` | `oled boot` | Animated boot sequence |

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
| `tone` | `tone <pin> <note\|hz> [ms]` | Play a tone on a buzzer -- use a note name like `C4` or `A#3`, or a raw Hz value |
| `melody` | `melody <pin> <C4:200 E4:200 ...>` | Play a sequence of notes. Format is `NOTE:duration_ms`; use `REST` for silence |
| `melody` | `melody <pin> elise` | Play the full Für Elise arrangement |
| `melody` | `melody <pin> canon` | Play the full Canon in D arrangement |
| `morse` | `morse <text> [wpm]` | Blink the onboard LED in morse code (default 13 WPM) |
| `piano` | `piano <pin> [duration_ms]` | Interactive keyboard piano -- drive a buzzer from keypresses |

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
| `sleep` | `sleep <ms>` | Wait for a number of milliseconds (1–30000) |
| `repeat` | `repeat <n> <command>` | Run a command n times in a row (1–100) |
| `watch` | `watch <ms> <command>` | Run a command repeatedly at an interval; press any key to stop |
| `trigger` | `trigger <pin> <rise\|fall\|both> <command>` | Watch a pin and run a command the moment an edge fires (one-shot) |
| `cron` | `cron <delay_ms> <command>` | Schedule a command to run once after a delay; returns immediately |
| `bench` | `bench <iterations> <command>` | Run a command many times and report throughput and timing |
| `time` | `time <command>` | Run a command and report how long it took (ms and µs) |
| `alias` | `alias [name [command...]]` | Define, list, or show command aliases |
| `unalias` | `unalias <name>` | Remove an alias |
| `run` | `run <file>` | Run a DeckScript file from VFS |
| `script` | `script run <file>` | Same as `run` |
| `script` | `script test` | Run the built-in DeckScript self-test |
| `edit` | `edit <file>` | Open a file in the built-in nano-style text editor |

```bash
> watch 1000 temp
> watch 500 adc 0
> trigger 15 fall led on
> repeat 5 led toggle
> cron 5000 reboot
> bench 1000 echo hi
> time sysinfo
> run /home/blink.ds
> edit /home/blink.ds
```

#### Aliases

Aliases let you give a short name to a longer command. The alias name is replaced
by its value, and any extra arguments you type are appended. Expansion happens
exactly **once**, so a self-referential alias such as `alias ls "ls -l"` is safe
and will not loop.

```bash
> alias ll ls               # 'll' now runs 'ls'
> alias t temp              # 't' now runs 'temp'
> ll /home                  # expands to: ls /home
> alias                     # list all defined aliases
> alias ll                  # show one alias
> unalias ll                # remove it
```

Up to 16 aliases can be defined. Aliases live in RAM and are cleared on reboot.

### System

| Command | Usage | What it does |
|---|---|---|
| `reboot` | `reboot` | Save VFS to flash then reboot via watchdog after 1 second |
| `dfu` | `dfu` | Jump into USB bootloader mode for reflashing |
| `uid` | `uid` | Print the board's unique 64-bit ID |
| `wdog` | `wdog` | Check if the last reboot was caused by the watchdog |
| `fault` | `fault info` | Show CPU registers captured from the last HardFault this session |
| `fault` | `fault test` | Trigger a deliberate fault to verify the handler and syslog integration |
| `date` | `date` | Show the current date and time from the RP2040 RTC |
| `date` | `date set <Y> <M> <D> <h> <m> <s>` | Set the real-time clock |
| `history` | `history` | List the command history (last 16 commands) |
| `history` | `history clear` | Clear the command history |
| `uname` | `uname [-a]` | Print the OS name; `-a` shows full system identity |
| `rand` | `rand [min] [max]` | Generate a hardware random number |

The HardFault handler is installed automatically at boot. It captures PC, LR, SP, R0, R12, and xPSR, writes them to syslog, then reboots via watchdog. After an unexpected reboot, run `syslog err` to see the cause.

```bash
> fault info
> fault test     # board will reboot; check 'syslog err' after
```

#### Real-time clock (`date`)

`date` uses the RP2040's built-in hardware RTC. The clock starts at
`2025-01-01 00:00:00` on boot and keeps ticking while the board is powered. Set
it once per session to get accurate timestamps. The day of the week is computed
automatically.

```bash
> date                              # show current time
> date set 2026 5 30 19 45 00       # YYYY MM DD hh mm ss
```

> The RP2040 RTC is **not** battery-backed -- the time resets on every power loss.

#### Random numbers (`rand`)

`rand` draws from the RP2040's hardware ring-oscillator entropy source, so the
values are genuinely random (unlike a seeded software PRNG).

```bash
> rand                  # full 32-bit value
> rand 6                # 0 .. 5  (e.g. a die index)
> rand 1 6              # 1 .. 6  inclusive
```

#### System identity (`uname`)

```bash
> uname                 # -> DeckOS
> uname -a              # DeckOS 5.0 <hostname> RP2040 Cortex-M0+ <mhz> SRAM=.. flash=.. <board>
```

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

DeckOS supports WiFi through an ESP8266 module running the **DeckOS Bridge firmware** -- a small Arduino sketch that lives on the ESP8266 and translates DeckOS commands into WiFi actions. This lets the Pico connect to networks, make HTTP requests, run a telnet server, and more, all from the DeckOS shell.

### What WiFi lets you do

- **Scan nearby networks** -- see every access point in range with signal strength and security type
- **Connect to WiFi** -- join a network so the ESP8266 has an IP address
- **HTTP GET and POST** -- pull data from or push data to any URL
- **Telnet server** -- accept incoming terminal connections on port 23
- **HTTP server** -- turn the Pico into a tiny web endpoint on port 80
- **Remote monitoring** -- push sensor readings to a server without a USB cable near the Pico
- **Wireless data logging** -- log GPIO events, ADC samples, or syslog entries to a remote server
- **Headless deployment** -- once configured, the Pico can operate without USB and still participate in your network

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
| `wifi scan` | Scan for WiFi networks |
| `wifi join <ssid> <pass>` | Join a network |
| `wifi ip` | Show the assigned IP address |
| `wifi shell` | Drop into a raw interactive shell with the ESP8266 |
| `wifi deinit` | Release UART1 from the ESP8266 |
| `wifi get <url>` | HTTP GET request |
| `wifi post <url> <body>` | HTTP POST request |
| `wifi serve` | Start an HTTP server on port 80 |
| `wifi telnet` | Start a telnet server on port 23 |
| `wifi telnet stop` | Stop the telnet server |
| `wifi bridge status` | Show bridge mode, WiFi connection state, and IP address |
| `wifi bridge scan` | Scan for nearby WiFi networks via bridge |
| `wifi bridge connect` | Connect using credentials stored in the bridge firmware |
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
> wifi get http://example.com/data
> wifi post http://example.com/log "temp=27.3"
```

---

## MQTT

| Command | What it does |
|---|---|
| `mqtt server <host>` | Set broker address |
| `mqtt port <n>` | Set broker port (default 1883) |
| `mqtt id <name>` | Set client ID |
| `mqtt connect` | Connect to broker |
| `mqtt disconnect` | Disconnect from broker |
| `mqtt status` | Show connection state |
| `mqtt pub <topic> <msg>` | Publish a message |
| `mqtt sub <topic>` | Subscribe to a topic |
| `mqtt unsub <topic>` | Unsubscribe from a topic |

Requires WiFi to be initialised and connected first.

```bash
> wifi init
> wifi bridge connect
> mqtt server 192.168.1.100
> mqtt connect
> mqtt pub deck/temp 23.4
> mqtt sub deck/cmd/#
```

---

## Swarm / ESP-NOW

Lets multiple Picos communicate peer-to-peer without a router, useful for drone swarms or sensor meshes.

| Command | What it does |
|---|---|
| `swarm init` | Start ESP-NOW mesh |
| `swarm id <name>` | Set this node's name |
| `swarm mac` | Show this node's MAC address |
| `swarm peer <MAC>` | Register a peer node |
| `swarm pub <lat> <lon> <alt> <hdg> <state>` | Broadcast position telemetry |
| `swarm list` | Show all known peers |
| `swarm stop` | Stop the mesh |

```bash
# Node 1
> wifi init
> swarm init
> swarm mac          # note MAC: AA:BB:CC:DD:EE:01
> swarm id drone1

# Node 2
> wifi init
> swarm init
> swarm mac          # note MAC: AA:BB:CC:DD:EE:02
> swarm id drone2
> swarm peer AA:BB:CC:DD:EE:01   # register node 1

# Broadcast from node 1
> swarm pub 28.6139 77.2090 100.0 180.0 1
```

---

## Bluetooth / HC-05

DeckOS supports wireless shell access through an HC-05 Bluetooth module. Once paired, you can control the Pico from a phone or laptop terminal app without touching a USB cable.

### What Bluetooth lets you do

- **Wireless shell** -- run any DeckOS command from a Bluetooth terminal on your phone or laptop
- **Remote command execution** -- send a single command from the BT side and get the output back
- **Live stats streaming** -- stream `top`-style CPU and temperature data to a connected Bluetooth client
- **Syslog mirroring** -- forward every log entry to the Bluetooth terminal as it happens
- **File transfer** -- send and receive VFS files over the Bluetooth link

### Wiring

```
HC-05 VCC  →  VSYS / 5V / VBUS
HC-05 GND  →  GND
HC-05 RXD  →  GP1
HC-05 TXD  →  GP0
```

### Bluetooth commands

| Command | What it does |
|---|---|
| `bt init [baud]` | Initialise the HC-05 UART |
| `bt status` | Show init state, connection state, log mirror state |
| `bt shell` | Start a full wireless DeckOS terminal over Bluetooth |
| `bt exec <command>` | Run one command and send the output to the BT client |
| `bt top [ms]` | Stream live CPU/temp stats to the BT client |
| `bt log on` | Mirror all syslog entries to the BT client in real time |
| `bt log off` | Stop mirroring |
| `bt send <file>` | Send a VFS file over Bluetooth |
| `bt recv <file>` | Receive a file from Bluetooth into VFS |
| `bt sniff [s]` | Raw byte sniffer -- prints hex and ASCII |
| `bt at` | Drop into interactive AT command mode |
| `bt name <name>` | Set the HC-05 module name (requires AT mode) |
| `bt pin <code>` | Set the pairing PIN (requires AT mode) |
| `bt baud <rate>` | Change the HC-05 UART baud rate (requires AT mode) |

### Typical workflow

```bash
> bt init
> bt status
> bt shell       # connect from your phone now
```

---

## Filesystem

DeckOS includes a small in-memory virtual filesystem (VFS). It lives in SRAM and survives reboots only if you explicitly run `save` -- otherwise it is wiped on power-off. It is useful for holding scripts and configuration during a session, or for writing and running quick programs on the device.

| Command | What it does |
|---|---|
| `ls [path]` | List directory contents |
| `cat <file>` | Print file contents |
| `touch <file>` | Create an empty file |
| `mkdir <dir>` | Create a directory |
| `rm [-r] <path>` | Remove a file or directory tree |
| `write <file> <text>` | Overwrite a file with one line of text |
| `write -i <file>` | Interactive multi-line write shell (end with `.`) |
| `iwrite <file>` | Same as `write -i` -- interactive multi-line write |
| `append <file> <text>` | Append a line of text to a file |
| `hexdump <file>` | Hex + ASCII dump of a file |
| `cd [dir]` | Change working directory |
| `pwd` | Print working directory |
| `cp <src> <dst>` | Copy a file |
| `mv <src> <dst>` | Move or rename a file |
| `stat <path>` | Show file or directory metadata |
| `edit <file>` | Open a file in the built-in text editor; creates the file if it doesn't exist (see below) |
| `wc <file>` | Count lines, words, and bytes |
| `grep <pattern> <file>` | Search a file for a pattern |
| `find [name]` | Recursive name search |
| `df` | Filesystem usage summary |
| `tree` | Print the full directory tree |
| `save` | Persist the current VFS to flash so it survives reboots |

### Text editor (`edit`)

> **The editor is a loadable module.** It needs three ~32 KB line buffers
> (~96 KB total), so it does **not** consume any RAM until you load it. Run
> `module load editor` first; `edit` reports an error otherwise. Run
> `module unload editor` to give the ~96 KB back. See
> [Modules](#modules-on-demand-ram).

```bash
> module load editor
> edit /home/blink.ds
> module unload editor      # reclaim ~96 KB when you're done
```

`edit <file>` opens a small nano-style full-screen editor over the serial
terminal. It auto-detects your terminal size at startup (and falls back to 80×24
if the terminal doesn't report it), so the cursor stays aligned on terminals of
any width. Line-wrapping is disabled while editing so long lines truncate at the
screen edge instead of wrapping.

| Key | Action |
|---|---|
| Arrows / `Home` / `End` / `PgUp` / `PgDn` | Move the cursor |
| `Ctrl-S` | Save |
| `Ctrl-Q` / `Ctrl-X` | Quit (prompts if unsaved) |
| `Ctrl-K` | Cut the current line |
| `Ctrl-Y` | Paste the cut line |
| `Ctrl-U` | Insert a blank line |
| `Ctrl-F` | Find (Enter on an empty query repeats the last search) |
| `Ctrl-Z` | Undo the last structural edit |
| `Ctrl-G` | Show the shortcut help line |

Files are limited to 64 lines of up to 511 characters each.

---

## Modules (on-demand RAM)

Some subsystems carry a large RAM footprint that most sessions never touch. The
text editor, for example, needs three line buffers of ~32 KB each (~96 KB) --
more than half of all static RAM -- yet it sits idle unless you actually edit a
file.

DeckOS packages such subsystems as **modules**. A module's buffers are
`malloc()`-ed from the heap only when you explicitly load it, and freed when you
unload it. A module that isn't loaded costs essentially nothing (just a small
registry entry), and its commands refuse to run until it is loaded.

| Command | What it does |
|---|---|
| `module` / `module list` | List modules, their load state, and RAM cost |
| `module load <name>` | Allocate the module's buffers and enable it |
| `module unload <name>` | Free the module's buffers |

```bash
> module list
modules:
  name       state  ram      description
  editor     -        96 KB  nano-style text editor (edit command)
  ----
  loaded RAM: 0 KB
> module load editor       # ~96 KB allocated from heap
> edit /home/blink.ds
> module unload editor      # ~96 KB returned to heap
```

**Why it matters:** with the editor unloaded, idle free heap is ~162 KB; loading
it brings that down to ~66 KB only while you need it. This keeps the DeckScript
interpreter (which `malloc()`s a 16 KB line buffer per nesting level) from
running out of memory during normal use.

Currently the **editor** is the one bundled module; the framework is built to
host more RAM-heavy subsystems the same way.

---

## USB portable OS

DeckOS is a self-contained OS that boots the instant you apply power -- no host
required. It cannot boot a PC the way a Linux Live USB does (the RP2040 is an
ARM Cortex-M0+ and can't be bootable media for an x86 host), but the chip's
USB 1.1 controller lets DeckOS present itself as a **composite USB device** so
it behaves like a portable computer you plug into anything.

When you connect the board it now enumerates as three things at once:

| Interface | What the host sees | DeckOS side |
|---|---|---|
| **CDC** | A serial port (the shell) | unchanged -- `make monitor`, minicom, PuTTY |
| **MSC** | A removable USB drive | a 16 KB FAT12 disk you can drag files to/from |
| **HID** | A USB keyboard | the board can "type" into the host |

All three are always available -- no rebuilding or mode switching. The serial
shell keeps working exactly as before; the mass-storage drive and keyboard are
extra.

> The device shows up with VID `0x2E8A` / PID `0x000B`, product name
> **"DeckOS Portable"**, and a serial number taken from the board's unique ID.

### USB mass storage (portable disk)

Plug the board into any computer and a small removable drive labelled
**DECKOS** appears. It's a genuine FAT12 volume (validated with `fsck.fat`), so
Windows, macOS, and Linux mount it with no drivers. Drop scripts or text files
onto it, then pull them into the DeckOS VFS -- or push VFS files out to the drive
to carry them to another machine.

The disk lives in SRAM (16 KB, 28 usable 512-byte clusters). It is intentionally
small so it doesn't starve the heap the DeckScript interpreter needs -- and the
whole VFS only holds ~16 KB anyway, so a bigger disk could never be filled. It
is wiped on power loss unless you re-create its contents; use the `usb` command
to bridge it to the persistent VFS.

| Command | What it does |
|---|---|
| `usb` / `usb status` | Show mount state, capacity, and file count |
| `usb list` | List files currently on the USB drive |
| `usb export <vfspath> [name]` | Copy a VFS file onto the USB drive |
| `usb import <NAME> [vfsdir]` | Copy a drive file into the VFS (default `/home`) |
| `usb sync` | Export every file in `/home` to the drive |
| `usb rm <NAME>` | Delete a file from the drive |
| `usb format` | Wipe and re-create the drive (adds `README.TXT`) |

```bash
> usb export /home/blink.ds        # now visible as BLINK.DS on the host drive
> usb list
> usb import SETUP.DS               # a file you dragged on from your PC -> /home/SETUP.DS
> usb sync                          # push all of /home onto the drive
```

Names are converted to 8.3 (`blink.ds` → `BLINK.DS`). Files dropped from the
host show up under their short name. To keep imported files across reboots, run
`save` after importing.

> The host owns the filesystem while mounted. DeckOS only touches the disk in
> short interrupt-safe bursts, so reads/writes from both sides stay consistent.

### USB keyboard (HID)

The board can act as a USB keyboard and type into the connected host -- handy for
automation, kiosk setup, or sending a fixed string on demand.

| Command | What it does |
|---|---|
| `hid` / `hid status` | Show connection and ready state |
| `hid type <text...>` | Type the text into the host |
| `hid line <text...>` | Type the text, then press Enter |
| `hid key <COMBO...>` | Send one or more key combinations |
| `hid enter` | Press Enter |

`hid key` understands modifiers (`CTRL`, `ALT`, `SHIFT`, `GUI`/`WIN`/`CMD`) and
named keys (`ENTER`, `TAB`, `ESC`, `SPACE`, `BACKSPACE`, `DEL`, `UP`, `DOWN`,
`LEFT`, `RIGHT`, `HOME`, `END`, `F1`–`F12`, single letters/digits). Join a combo
with `+`.

```bash
> hid type hello from DeckOS
> hid line echo "typed by a microcontroller"
> hid key WIN+r                    # open the Windows Run dialog
> hid key CTRL+ALT+DEL
> hid key F5                       # refresh
```

> A device that can inject keystrokes is powerful -- only plug it into machines
> you trust, and remember it will start typing the moment a command runs.

### Standalone handheld (OLED console)

With an SSD1306 OLED on GP4/GP5 and a battery on VSYS, DeckOS becomes a truly
standalone pocket computer: shell output is mirrored to the 128×64 display as a
scrolling text console, so you don't need a host terminal to see what's
happening. (Input still comes from serial or the Bluetooth shell -- the RP2040's
single USB port can't host a keyboard while also being a device.)

| Command | What it does |
|---|---|
| `console` / `console status` | Show whether the OLED mirror is on |
| `console oled on` | Mirror all shell output to the OLED (auto-inits the panel) |
| `console oled off` | Stop mirroring |

```bash
> console oled on
> sysinfo            # output now also appears on the OLED
> console oled off
```

The console shows the last 8 lines (21 characters wide), swallows ANSI escape
codes so they don't appear as garbage, and redraws per line to keep I²C traffic
low. Pair it with the `power` command to monitor the battery.

---

## DeckScript

DeckScript is DeckOS's built-in scripting language. It runs natively on the Pico -- no interpreter on the host, no serial protocol, just a script file in VFS and a `run` command. It was written specifically for this environment, so it knows about GPIO pins, ADC channels, PWM, and timing right out of the box.

Scripts are plain text files with a `.ds` extension by convention. Comments start with `#`. Everything is line-based -- one statement per line.

### Running scripts

```bash
# Write a script to VFS
> write /home/blink.ds led on
> append /home/blink.ds sleep 500
> append /home/blink.ds led off
> append /home/blink.ds sleep 500

# Or open the built-in editor and type it properly
> edit /home/blink.ds

# Or use the interactive write shell
> iwrite /home/blink.ds

# Run it
> run /home/blink.ds

# Or through the script command
> script run /home/blink.ds

# Quick self-test
> script test
```

### Variables

Variables are set with `let` and referenced with `$`. Variable names are alphanumeric plus underscores, up to 15 characters.

```
let x = 10
let name = hello
print the value is $x
print hello $name
```

There are two special variables available inside scripts:

- **`$_i`** -- inside a `repeat` block, holds the current iteration number (0-based).
- **`$return`** -- after a `call`, holds the value returned by the function (set via `let return = ...` or `return <value>` inside the function body).

### Arithmetic

Basic integer arithmetic in `let` expressions: `+`, `-`, `*`, `/`, `%`. The operator must be surrounded by spaces. Both sides can be a literal number or a `$variable` reference.

```
let x = 10
let y = 3
let z = $x + $y
print $z

let r = $x % $y
print remainder is $r
```

### String functions

These all work as the right-hand side of a `let` statement. Variable references inside the arguments are expanded automatically.

| Function | What it does |
|---|---|
| `upper(text)` | Convert to uppercase |
| `lower(text)` | Convert to lowercase |
| `len(text)` | Length of string |
| `substr(text, start, length)` | Extract a substring |
| `contains(haystack, needle)` | Returns `1` if needle found, `0` if not |
| `trim(text)` | Strip leading and trailing whitespace |
| `replace(text, old, new)` | Replace all occurrences of old with new |
| `format(fmt, arg1, arg2, ...)` | Printf-style string formatting |

```
let msg = hello world
let up = upper($msg)
print $up

let l = len($msg)
print length is $l

let piece = substr($msg, 6, 5)
print $piece

let result = format(%d degrees C, 27)
print $result
```

### Math functions

| Function | What it does |
|---|---|
| `sqrt(x)` | Square root |
| `pow(x, y)` | x to the power of y |
| `abs(x)` | Absolute value |
| `min(a, b)` | Smaller of two values |
| `max(a, b)` | Larger of two values |
| `clamp(x, lo, hi)` | Clamp x to the range [lo, hi] |
| `map(x, in_lo, in_hi, out_lo, out_hi)` | Map a value from one range to another |
| `rand(lo, hi)` | Random integer between lo and hi inclusive |
| `avg(a, b, c, ...)` | Average of up to 5 values |

```
let r = sqrt(144)
print $r

let val = clamp(150, 0, 100)
print $val

let mapped = map(512, 0, 1023, 0, 100)
print $mapped

let n = rand(1, 6)
print dice: $n
```

### Control flow

```
if $x == 10
  print x is ten
elif $x > 10
  print x is more than ten
else
  print x is less than ten
endif
```

Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`. These work for both integers and strings (`<`, `>`, `<=`, `>=` are integer-only). A bare integer expression is truthy if non-zero; a non-empty string with no operator is always truthy.

#### Switch

```
switch $color
case red
  print stop
case green
  print go
default:
  print unknown color
endswitch
```

#### Assert

```
assert $x == 10 or fail: x should be 10
```

If the condition is false, the script prints the message and stops.

### Loops

#### repeat

```
repeat 5
  print iteration $_i
endrepeat
```

`$_i` is 0-based and counts up to `n - 1`. `repeat` is capped at 10,000 iterations.

#### while

```
let i = 0
while $i < 10
  print $i
  let i = $i + 1
endwhile
```

While loops are capped at 100,000 iterations as a safety net.

#### for (range)

```
for i from 1 to 10
  print $i
endfor

for i from 0 to 20 step 2
  print $i
endfor

for i from 10 to 1 step -1
  print $i
endfor
```

#### for (array)

```
arr_new nums 0
arr_push nums 10
arr_push nums 20
arr_push nums 30

for val in nums
  print $val
endfor
```

You can also use a variable that holds the array name:

```
let myarr = nums
for val in $myarr
  print $val
endfor
```

#### break and continue

```
for i from 1 to 10
  if $i == 5
    break
  endif
  print $i
endfor
```

### Arrays

Arrays are dynamically sized. Elements are zero-indexed.

```
arr_new scores 0       # create an empty array called scores
arr_push scores 95
arr_push scores 87
arr_push scores 72

let n = arr_len(scores)
print count: $n

let top = arr_get(scores, 0)
print top score: $top

arr_set scores 1 99    # overwrite element at index 1

arr_dump scores        # print all elements

arr_pop scores popped  # pop last element into variable 'popped'
print popped: $popped
```

You can also pre-allocate with a size:

```
arr_new buffer 10      # 10 elements, all "0"
arr_set buffer 3 hello
```

### Functions

Functions are defined anywhere in the script and called with `call`. Arguments arrive as `$arg0`, `$arg1`, etc. The return value is written to the special variable `$return` -- either by assigning `let return = <value>` inside the function, or by using `return <value>` to set it and exit immediately.

```
def greet
  print hello $arg0
enddef

call greet world
call greet DeckOS
```

```
def add
  let return = $arg0 + $arg1
enddef

call add 3 7
print result: $return
```

Functions can be defined after the code that calls them -- the interpreter scans the whole script for definitions first. Variable arguments are expanded at the call site before being passed in, so `call myfunc $x` passes the current value of `$x`, not the literal string `$x`.

#### Recursion example

```
def fact
  if $arg0 <= 1
    let return = 1
    return
  endif
  let n = $arg0
  let p = $arg0 - 1
  call fact $p
  let return = $n * $return
enddef

call fact 5
print $return
```

### Hardware access from scripts

This is where DeckScript gets useful for actual embedded work. All of these go on the right-hand side of a `let`.

| Expression | What it does |
|---|---|
| `adc(0)` | Read ADC channel 0–2 (GP26–28), returns raw 12-bit value |
| `gpio(15)` | Read the current state of GP15 (0 or 1) |
| `pwm(16, 50)` | Set GP16 to 50% PWM duty cycle, returns 1 on success |
| `millis` | Milliseconds since boot |
| `micros` | Microseconds since boot |

> **Note:** `adc()` accepts channels 0, 1, and 2 only (GP26, GP27, GP28). Other channel numbers return 0.

GPIO write and other hardware commands use shell commands directly inside the script:

```
gpio_write 15 1        # set GP15 high
gpio_write 15 0        # set GP15 low
pulse 15 500           # high for 500 µs then low
wait_pin 15 1 3000     # wait up to 3000 ms for GP15 to go high
sleep 100              # wait 100 ms
```

`wait_pin` sets `$_timeout` to `1` if it times out before the pin changes.

```
# Read ADC and blink LED faster when value is high
let threshold = 2800

repeat 10
  let raw = adc(0)
  if $raw > $threshold
    gpio_write 25 1
    sleep 100
    gpio_write 25 0
    sleep 100
  else
    gpio_write 25 1
    sleep 500
    gpio_write 25 0
    sleep 500
  endif
endrepeat
```

### I/O

```
print hello
print value is $x
println same thing, also prints a newline

let name = input(what is your name? )
print nice to meet you $name

echo           # print a blank line
pause          # wait for any keypress before continuing
vars           # dump all current variable names and values
```

### Logging and assertions

```
log info myapp everything is fine
log warn myapp temperature is high
log err  myapp sensor read failed
log debug myapp raw = $raw

assert $x > 0 or fail: x must be positive
```

Log entries go into the system log and can be read with `syslog show`.

### Includes

Scripts can include other scripts with `include`. The included file runs in the same variable context, so any variables or functions it defines are available after the include.

```
include /home/helpers.ds
```

You can also call `run` from within a script to execute another script file. Unlike `include`, `run` creates a fresh variable context each time.

```
run /home/tests/01_vars.ds
```

### Example scripts

#### Blink the LED

```
# blink.ds - blink onboard LED 10 times
repeat 10
  gpio_write 25 1
  sleep 200
  gpio_write 25 0
  sleep 200
endrepeat
print done
```

#### Read ADC and classify

```
# adc_check.ds
let raw = adc(0)
let mv = map($raw, 0, 4095, 0, 3300)
print raw: $raw
print voltage: $mv mV

if $mv > 2000
  print HIGH
elif $mv > 1000
  print MID
else
  print LOW
endif
```

#### Servo sweep with timing

```
# sweep.ds
let pin = 16
let step = 5

for angle from 0 to 180 step $step
  servo $pin $angle
  sleep 30
endfor

for angle from 180 to 0 step -$step
  servo $pin $angle
  sleep 30
endfor

print sweep complete
```

#### Simple function library

```
# lib.ds - reusable helpers

def wait_high
  # wait for arg0 pin to go high, arg1 timeout ms
  wait_pin $arg0 1 $arg1
  if $_timeout == 1
    print timeout waiting for pin $arg0
  endif
enddef

def blink_n
  # blink pin arg0 n=arg1 times with arg2 ms delay
  repeat $arg1
    gpio_write $arg0 1
    sleep $arg2
    gpio_write $arg0 0
    sleep $arg2
  endrepeat
enddef
```

---

## Buzzer setup

The `tone`, `melody`, and `piano` commands use the RP2040's hardware PWM to drive a **passive buzzer**. It works well and sounds perfectly fine for a microcontroller.

**One important thing:** you need a *passive* buzzer, not an active one. An active buzzer has a built-in oscillator -- it just beeps at one fixed pitch when you apply power and ignores the PWM frequency entirely. A passive buzzer is just a bare piezo element with no internal circuitry, and that's what responds to DeckOS's PWM signal.

Not sure which you have? Apply 3.3V DC to it. If it beeps on its own, it's active. If it stays silent, it's passive -- that's the right one.

**Wiring:**

```
GPIO pin ──── [100Ω resistor] ──── (+) Buzzer (−) ──── GND
```

Any GPIO pin works. GP16 is a good default. The 100 Ω resistor is optional but it protects the pin.

**Supported notes** use standard scientific pitch notation -- C3 up through B5, sharps and flats included (`Bb4` is the same as `A#4`). The piano command covers C0 through C7. Use `REST` for silence.

```bash
> melody 16 C4:200 D4:200 E4:200 F4:200 G4:200 A4:200 B4:200 C5:400
> tone 16 440 1000
> melody 16 elise
```

---

## Config system

Settings live in the **last 4 KB of flash** (`0x101FF000` on a 2 MB board; the offset scales with the detected flash size on larger parts), protected by a CRC32 checksum. On a fresh board, the checksum won't match so DeckOS falls back to defaults quietly. Nothing breaks, it just uses sensible starting values.

Changes from `config set` are only in RAM until you run `config save`. CPU speed and LED state changes take effect on the next boot.

---

## Syslog

DeckOS keeps a **64-entry ring log** in memory. When it fills up, the oldest entries get overwritten. It is wiped on reboot unless a fault handler writes to it first.

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

Background tasks run on **Core 1**, completely separate from your shell. Each task has a name, a function, and a repeat interval in milliseconds. The scheduler checks for due tasks on every Core 1 tick and fires them when their time is up. Core 1 also drives background jobs such as servo sweeps, the `la` trigger capture, and other non-blocking work.

```bash
> tasks                 # see all tasks
> tasks disable 0       # stop a task
> tasks enable 0        # bring it back
> top                   # watch live CPU usage per task
> jobs                  # list active background Core 1 jobs
```

---

## Boot modes

| Mode | How to trigger | What happens |
|---|---|---|
| **Normal** | Just power on | Full shell, all drivers loaded; USB enumerates as a composite CDC + MSC + HID device (see [USB portable OS](#usb-portable-os)) |
| **DFU** | Run `dfu` in the shell | Jumps into USB bootloader so you can reflash |

For hardware recovery (e.g. after flashing a bad build), hold the RP2040's
**BOOTSEL** button while plugging in USB -- the board mounts as the ROM
mass-storage bootloader (a separate volume from the DeckOS `DECKOS` drive) so
you can drag a fresh `.uf2` onto it. The 1200-baud reset touch also still
reboots the board into BOOTSEL. (Earlier versions used a GP15 recovery pin; that
was removed because it tied up a GPIO and could falsely trigger recovery mode on
boards where the pin floats.)

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

---

## ESP32 Port

[DeckOS_ESP32](https://github.com/enginestein/DeckOS_ESP32) is a port of DeckOS from the **RP2040 (bare metal)** to the **ESP32 (ESP-IDF / FreeRTOS)**. It introduces a Hardware Abstraction Layer (HAL) that decouples the kernel, shell, scripting, and command logic from the MCU hardware, allowing the same codebase to target both platforms.

### What's different in the ESP32 port

| Aspect | RP2040 (this repo) | ESP32 Port |
|--------|-------------------|------------|
| **SDK** | Pico SDK (bare metal) | ESP-IDF 5.1 (FreeRTOS) |
| **CPU** | Dual-core Cortex-M0+ @ 125 MHz | Dual-core Xtensa LX6 @ 240 MHz |
| **WiFi** | External ESP8266 bridge | Native ESP-IDF WiFi |
| **ESP-NOW** | Via ESP8266 bridge | Native ESP-NOW |
| **Bluetooth** | External HC-05 (UART) | Stubbed (BT disabled for DRAM) |
| **USB** | TinyUSB (CDC + MSC + HID) | ESP-IDF USB CDC only |
| **Filesystem** | VFS persisted to raw flash | VFS persisted to SPIFFS (2 MB partition) |
| **Config** | Last 4 KB of flash | NVS (Non-Volatile Storage) |
| **Camera** | External ESP32-CAM bridge | Native esp32-camera driver |

### New features in the ESP32 port

- **Native WiFi** — station + AP mode, HTTP server, event-driven
- **Native ESP-NOW** — peer-to-peer mesh without a router
- **ESP32-CAM support** — full camera init/capture
- **NRF24L01 radio driver** — 2.4 GHz SPI radio
- **SPIFFS persistence** — 2 MB partition for VFS files
- **NVS config** — key-value storage via ESP-IDF NVS
- **PSRAM support** — auto-detected and used for heap
- **Board auto-detection** — ESP32-WROOM vs CAM vs S3

### Building the ESP32 port

```bash
git clone https://github.com/enginestein/DeckOS_ESP32
cd DeckOS_ESP32
idf.py build
idf.py flash monitor
```

Requires ESP-IDF 5.1 or newer and the appropriate xtensa toolchain.

---

*Who doesn't love a decent shell?*