#!/usr/bin/env python3
import os
import sys
import time
import struct
import argparse
import textwrap
from typing import Optional

#  Blinka / CircuitPython imports 
os.environ.setdefault("BLINKA_FT232H", "1")

try:
    import board
    import busio
    import digitalio
    from adafruit_bus_device.spi_device import SPIDevice
except ImportError as e:
    print(f"\n[ERROR] Missing library: {e}")
    print("Install with:  pip install pyftdi adafruit-blinka adafruit-circuitpython-busdevice")
    print("Then set:      export BLINKA_FT232H=1  (Linux/Mac)")
    print("           or  set BLINKA_FT232H=1     (Windows)")
    sys.exit(1)

#  ANSI colours 
class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"
    RED    = "\033[91m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    WHITE  = "\033[97m"
    GREY   = "\033[90m"
    BG_RED = "\033[41m"

def _c(text: str, *codes: str) -> str:
    """Wrap text in ANSI codes (skipped if stdout is not a tty)."""
    if not sys.stdout.isatty():
        return text
    return "".join(codes) + text + C.RESET

#  helpers 

def hexdump(data: bytes, indent: int = 4) -> str:
    """Return a classic hexdump string for the given bytes."""
    pad = " " * indent
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_part  = " ".join(f"{b:02X}" for b in chunk)
        hex_part += "   " * (16 - len(chunk))          # pad short last row
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        lines.append(f"{pad}{i:04X}  {hex_part}  |{ascii_part}|")
    return "\n".join(lines)


def parse_bytes(s: str) -> bytes:
    """
    Accept several input formats:
        0x01 0x02 0xFF          hex with 0x prefix
        01 02 FF                plain hex pairs (space or comma separated)
        "hello"                 ASCII string literal (with quotes)
        [1, 2, 255]             Python list literal
    """
    s = s.strip()

    # ASCII string in quotes
    if (s.startswith('"') and s.endswith('"')) or \
       (s.startswith("'") and s.endswith("'")):
        return s[1:-1].encode()

    # Python list / tuple
    if s.startswith("[") or s.startswith("("):
        vals = [int(x.strip(), 0) for x in s.strip("[]()").split(",") if x.strip()]
        return bytes(vals)

    # Hex tokens (with or without 0x prefix, comma or space separated)
    tokens = s.replace(",", " ").split()
    result = []
    for t in tokens:
        result.append(int(t, 16) if not t.startswith("0x") else int(t, 0))
    return bytes(result)


def freq_str(hz: int) -> str:
    if hz >= 1_000_000:
        return f"{hz / 1_000_000:.3g} MHz"
    if hz >= 1_000:
        return f"{hz / 1_000:.3g} kHz"
    return f"{hz} Hz"


#  GPIO pin lookup for FT232H via Blinka 

PIN_MAP = {
    # MPSSE pins (D0–D7)
    "D0": "D0", "D1": "D1", "D2": "D2", "D3": "D3",
    "D4": "D4", "D5": "D5", "D6": "D6", "D7": "D7",
    # GPIO pins (C0–C7)
    "C0": "C0", "C1": "C1", "C2": "C2", "C3": "C3",
    "C4": "C4", "C5": "C5", "C6": "C6", "C7": "C7",
}

def get_pin(name: str) -> digitalio.DigitalInOut:
    """Return a DigitalInOut for a named FT232H pin (D0-D7, C0-C7)."""
    name = name.upper()
    if name not in PIN_MAP:
        raise ValueError(f"Unknown pin '{name}'. Valid: D0-D7, C0-C7")
    pin_obj = getattr(board, PIN_MAP[name])
    dio = digitalio.DigitalInOut(pin_obj)
    dio.direction = digitalio.Direction.OUTPUT
    dio.value = True   # deassert CS (active-low)
    return dio


#  SPI session 

class SPISession:
    def __init__(self, freq: int, mode: int, cs_pin: str, bits: int = 8):
        self.freq    = freq
        self.mode    = mode
        self.cs_name = cs_pin.upper()
        self.bits    = bits
        self._spi    = None
        self._cs     = None
        self._dev    = None

    def connect(self):
        """Open the SPI bus and CS pin on the FT232H."""
        cpol = bool(self.mode & 0b10)
        cpha = bool(self.mode & 0b01)
        self._spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)
        self._cs  = get_pin(self.cs_name)
        self._dev = SPIDevice(self._spi, self._cs,
                              baudrate=self.freq,
                              polarity=int(cpol),
                              phase=int(cpha))

    def transfer(self, tx: bytes) -> bytes:
        """Full-duplex transfer: send tx, return rx of same length."""
        buf = bytearray(tx)
        with self._dev as spi:
            spi.write_readinto(buf, buf)
        return bytes(buf)

    def write(self, tx: bytes):
        """Write only (MISO ignored)."""
        with self._dev as spi:
            spi.write(tx)

    def read(self, n: int, write_byte: int = 0xFF) -> bytes:
        """Read n bytes, clocking out write_byte repeatedly."""
        buf = bytearray(n)
        with self._dev as spi:
            spi.readinto(buf, write_value=write_byte)
        return bytes(buf)

    def close(self):
        if self._spi:
            self._spi.deinit()
            self._spi = None

    @property
    def info(self) -> str:
        return (f"SPI mode={self.mode}  freq={freq_str(self.freq)}"
                f"  CS={self.cs_name}  bits={self.bits}")


#  southbridge helpers 

try:
    import sb_protocol as sbp
    _SB_AVAILABLE = True
except ImportError:
    _SB_AVAILABLE = False

def _sb_require():
    if not _SB_AVAILABLE:
        raise RuntimeError("sb_protocol.py not found — place it next to spi_tester.py")

def _print_audio_response(resp, dt_ms: float):
    """Pretty-print a parsed AudioResponse."""
    magic  = _c("OK", C.GREEN) if resp.magic_ok  else _c("BAD", C.RED)
    crc    = _c("OK", C.GREEN) if resp.crc_ok    else _c("FAIL", C.RED)
    status_col = C.GREEN if resp.status == sbp.STATUS_OK else C.RED
    print(f"  magic={magic}  type={_c(resp.ftype_str, C.CYAN)}"
          f"  seq={resp.seq}"
          f"  status={_c(resp.status_str, status_col)}"
          f"  crc={crc}"
          f"  {_c(f'{dt_ms:.2f} ms', C.DIM)}")


def sb_nop(session: SPISession):
    """Send a NOP frame and decode the Pico's status response."""
    _sb_require()
    frame = sbp.build_nop_frame()
    t0 = time.monotonic()
    rx = session.transfer(frame)
    dt = (time.monotonic() - t0) * 1000
    resp = sbp.parse_audio_response(rx)
    print(f"  NOP ({sbp.SB_FRAME_BYTES}B)")
    _print_audio_response(resp, dt)


def sb_reset(session: SPISession):
    """Send a RESET frame — Pico flushes its audio queue."""
    _sb_require()
    frame = sbp.build_reset_frame()
    t0 = time.monotonic()
    rx = session.transfer(frame)
    dt = (time.monotonic() - t0) * 1000
    resp = sbp.parse_audio_response(rx)
    print(f"  RESET ({sbp.SB_FRAME_BYTES}B)")
    _print_audio_response(resp, dt)


def sb_silence(session: SPISession, n: int = 1):
    """Send n SILENCE frames — Pico outputs silence."""
    _sb_require()
    for i in range(n):
        frame = sbp.build_silence_frame(seq=i)
        rx = session.transfer(frame)
        resp = sbp.parse_audio_response(rx)
        status_col = C.GREEN if resp.status == sbp.STATUS_OK else C.RED
        print(f"  [{i+1}/{n}] SILENCE  status={_c(resp.status_str, status_col)}")


def sb_tone(session: SPISession, freq_hz: float, n_frames: int, amplitude: float = 0.5):
    """Stream a sine wave tone to the Pico over CS0."""
    _sb_require()
    print(f"  Streaming {freq_hz:.0f} Hz sine, {n_frames} frames "
          f"({n_frames * sbp.SB_AUDIO_SAMPLES_PER_FRAME / sbp.SB_SAMPLE_RATE * 1000:.0f} ms) …")
    ok = err = 0
    for frame, seq in sbp.sine_frames(freq_hz, n_frames, amplitude):
        t0 = time.monotonic()
        rx = session.transfer(frame)
        dt = (time.monotonic() - t0) * 1000
        resp = sbp.parse_audio_response(rx)
        if resp.status == sbp.STATUS_OK and resp.crc_ok:
            ok += 1
        else:
            err += 1
            print(f"  seq={seq}  {_c(resp.status_str, C.RED)}"
                  f"  crc={'ok' if resp.crc_ok else _c('FAIL',C.RED)}")
        # pace to ~realtime: each frame = SB_AUDIO_SAMPLES_PER_FRAME / SB_SAMPLE_RATE seconds
        frame_dur = sbp.SB_AUDIO_SAMPLES_PER_FRAME / sbp.SB_SAMPLE_RATE
        sleep_s = frame_dur - dt / 1000
        if sleep_s > 0:
            time.sleep(sleep_s)
    col = C.GREEN if err == 0 else C.YELLOW
    print(f"  Done  {_c(f'{ok} OK  {err} errors', col)}")


def sb_wav(session: SPISession, path: str):
    """Stream a WAV file to the Pico over CS0."""
    _sb_require()
    if not os.path.isfile(path):
        print(f"  {_c(f'File not found: {path}', C.RED)}")
        return
    print(f"  Streaming {path} …  (Ctrl-C to stop)")
    ok = err = 0
    try:
        for frame, seq in sbp.wav_to_frames(path):
            t0 = time.monotonic()
            rx = session.transfer(frame)
            dt = (time.monotonic() - t0) * 1000
            resp = sbp.parse_audio_response(rx)
            if resp.status == sbp.STATUS_OK and resp.crc_ok:
                ok += 1
            else:
                err += 1
                print(f"  seq={seq}  {_c(resp.status_str, C.RED)}"
                      f"  crc={'ok' if resp.crc_ok else _c('FAIL',C.RED)}")
            frame_dur = sbp.SB_AUDIO_SAMPLES_PER_FRAME / sbp.SB_SAMPLE_RATE
            sleep_s = frame_dur - dt / 1000
            if sleep_s > 0:
                time.sleep(sleep_s)
    except KeyboardInterrupt:
        print(f"\n  {_c('Stopped by user', C.YELLOW)}")
    col = C.GREEN if err == 0 else C.YELLOW
    print(f"  Done  {_c(f'{ok} frames OK  {err} errors', col)}")


def sb_cmd(session: SPISession, json_str: str, cmd_cs: str = "D4"):
    """
    Send a JSON command on the CS1 command channel.
    Temporarily switches CS to cmd_cs for this transfer, then restores it.
    """
    _sb_require()
    frame = sbp.build_cmd_frame(json_str)
    orig_cs = session.cs_name
    session.cs_name = cmd_cs
    session.close()
    session.connect()
    t0 = time.monotonic()
    rx = session.transfer(frame)
    dt = (time.monotonic() - t0) * 1000
    session.cs_name = orig_cs
    session.close()
    session.connect()

    resp = sbp.parse_cmd_response(rx)
    tx_col = C.CYAN
    print(f"  CMD TX: {_c(json_str, tx_col)}  ({sbp.SB_CMD_FRAME_BYTES}B)")
    if resp.magic_ok and resp.json_str:
        print(f"  CMD RX: {_c(resp.json_str, C.GREEN)}")
    else:
        print(f"  CMD RX: {_c('(no response / empty)', C.DIM)}")
    print(_c(f"  {dt:.2f} ms", C.DIM))


def sb_info():
    """Print southbridge protocol constants."""
    _sb_require()
    print(f"""
  {_c('Southbridge protocol constants', C.BOLD)}
  Frame size     : {sbp.SB_FRAME_BYTES} bytes  (audio CS0)
  Cmd frame size : {sbp.SB_CMD_FRAME_BYTES} bytes  (command CS1)
  Samples/frame  : {sbp.SB_AUDIO_SAMPLES_PER_FRAME}
  Sample rate    : {sbp.SB_SAMPLE_RATE} Hz
  Channels       : {sbp.SB_CHANNELS}
  Audio payload  : {sbp.SB_AUDIO_PAYLOAD_BYTES} bytes
  Magic (audio)  : 0x{sbp.AUDIO_MAGIC_HI:02X} 0x{sbp.AUDIO_MAGIC_LO:02X}
  Magic (cmd)    : 0x{sbp.CMD_MAGIC_HI:02X} 0x{sbp.CMD_MAGIC_LO:02X}
""")


#  loopback test 

def run_loopback(session: SPISession, length: int = 32) -> bool:
    """
    Send a known pattern and verify it comes back unchanged.
    Requires MOSI and MISO to be physically shorted together.
    """
    import random
    tx = bytes(random.randint(0, 255) for _ in range(length))
    rx = session.transfer(tx)
    ok = tx == rx
    print(f"\n  {'TX':5} {_c(tx.hex(' ').upper(), C.CYAN)}")
    print(f"  {'RX':5} {_c(rx.hex(' ').upper(), C.GREEN if ok else C.RED)}")
    if ok:
        print(f"\n  {_c('PASS', C.BOLD, C.GREEN)} – all {length} bytes match")
    else:
        mismatches = sum(a != b for a, b in zip(tx, rx))
        print(f"\n  {_c('FAIL', C.BOLD, C.RED)} – {mismatches}/{length} bytes differ")
        for i, (a, b) in enumerate(zip(tx, rx)):
            if a != b:
                print(f"    byte[{i:3d}]  TX={a:02X}  RX={b:02X}")
    return ok


#  clock speed sweep 

def run_speed_sweep(session: SPISession):
    """Try progressively higher clock speeds and report first failure."""
    speeds = [100_000, 500_000, 1_000_000, 3_000_000, 6_000_000,
              10_000_000, 15_000_000, 20_000_000, 30_000_000]
    print()
    orig = session.freq
    last_ok = None
    for spd in speeds:
        session.freq = spd
        session.close()
        session.connect()
        try:
            import random
            tx = bytes(random.randint(0, 255) for _ in range(16))
            rx = session.transfer(tx)
            ok = tx == rx
        except Exception as e:
            ok = False
        marker = _c("OK", C.GREEN) if ok else _c("FAIL", C.RED)
        print(f"  {freq_str(spd):>10}  {marker}")
        if ok:
            last_ok = spd
        else:
            break

    if last_ok:
        print(f"\n  Max reliable speed: {_c(freq_str(last_ok), C.BOLD, C.CYAN)}")
    else:
        print(f"\n  {_c('No speed worked – check loopback wiring', C.YELLOW)}")

    session.freq = orig
    session.close()
    session.connect()


#  register read helper 

def run_reg_read(session: SPISession, reg: int, n_bytes: int):
    """Send a register address byte then read n_bytes response."""
    tx = bytes([reg]) + bytes(n_bytes)
    rx = session.transfer(tx)
    reg_data = rx[1:]   # first byte is dummy during address phase
    print(f"\n  Reg 0x{reg:02X} → {n_bytes} byte(s):")
    if len(reg_data) <= 16:
        print(f"    hex  : {reg_data.hex(' ').upper()}")
        print(f"    dec  : {list(reg_data)}")
        if len(reg_data) == 1:
            print(f"    bin  : {reg_data[0]:08b}")
    else:
        print(hexdump(reg_data))


#  REPL 

HELP_TEXT = """
{bold}General commands{reset}
  {cmd}tx  <bytes>{reset}          full-duplex transfer, show RX
  {cmd}write  <bytes>{reset}       write only (no RX capture)
  {cmd}read  <n> [fill]{reset}     read n bytes, clock fill byte (default FF)
  {cmd}reg  <addr> <n>{reset}      send addr byte + read n bytes
  {cmd}loop [n]{reset}             loopback test (short MOSI↔MISO), n bytes (default 32)
  {cmd}sweep{reset}                clock speed sweep (loopback required)
  {cmd}freq  <hz>{reset}           change clock frequency (e.g. 1000000 or 1MHz)
  {cmd}mode  <0-3>{reset}          change SPI mode
  {cmd}cs  <pin>{reset}            change CS pin (D0-D7, C0-C7)
  {cmd}info{reset}                 show current SPI settings
  {cmd}pins{reset}                 show FT232H pin reference
  {cmd}help{reset}                 this message
  {cmd}quit{reset} / {cmd}exit{reset}         close and exit

{bold}Southbridge commands  (Pico2W firmware){reset}
  {cmd}sb.nop{reset}               send NOP frame, decode Pico status
  {cmd}sb.reset{reset}             send RESET — Pico flushes audio queue
  {cmd}sb.silence [n]{reset}       send n SILENCE frames (default 1)
  {cmd}sb.tone <hz> [frames] [amp]{reset}
                       stream a sine wave  e.g. sb.tone 440 200 0.5
  {cmd}sb.wav <file>{reset}        stream a WAV file to the Pico
  {cmd}sb.cmd <json> [cs_pin]{reset}
                       send JSON on CS1 cmd channel  e.g. sb.cmd {{"cmd":"vol","val":80}} D4
  {cmd}sb.info{reset}              show protocol frame layout constants

{bold}Byte formats accepted{reset}
  01 02 FF          plain hex (space or comma separated)
  0x01 0xFF         with 0x prefix
  "hello"           ASCII string
  [1, 2, 255]       Python list
""".format(
    bold=C.BOLD if sys.stdout.isatty() else "",
    reset=C.RESET if sys.stdout.isatty() else "",
    cmd=C.CYAN if sys.stdout.isatty() else "",
)

PIN_REF = """
{bold}FT232H SPI pin reference{reset}
  D0  →  SCK   (clock)
  D1  →  MOSI  (master out)
  D2  →  MISO  (master in)
  D3  →  CS    (default chip-select, active-low)
  D4-D7, C0-C7  →  available as extra CS or GPIO

  GND to GND of target device — always required
  3V3 or 5V from FT232H 5V pin (check target voltage!)
""".format(
    bold=C.BOLD if sys.stdout.isatty() else "",
    reset=C.RESET if sys.stdout.isatty() else "",
)


def parse_freq(s: str) -> int:
    s = s.strip().upper()
    if s.endswith("MHZ"):
        return int(float(s[:-3]) * 1_000_000)
    if s.endswith("KHZ"):
        return int(float(s[:-3]) * 1_000)
    return int(s)


def repl(session: SPISession):
    print(_c("\n SPI Tester ready ", C.BOLD, C.CYAN))
    print(_c(f"  {session.info}", C.DIM))
    print(_c("  Type 'help' for commands.\n", C.DIM))

    while True:
        try:
            line = input(_c("spi> ", C.BOLD, C.YELLOW)).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        parts = line.split(None, 1)
        cmd   = parts[0].lower()
        rest  = parts[1] if len(parts) > 1 else ""

        try:
            #  tx 
            if cmd == "tx":
                tx = parse_bytes(rest)
                t0 = time.monotonic()
                rx = session.transfer(tx)
                dt = (time.monotonic() - t0) * 1000
                print(f"  TX ({len(tx):3d}B)  {_c(tx.hex(' ').upper(), C.CYAN)}")
                print(f"  RX ({len(rx):3d}B)  {_c(rx.hex(' ').upper(), C.GREEN)}")
                if len(rx) > 16:
                    print(hexdump(rx))
                print(_c(f"  {dt:.2f} ms", C.DIM))

            #  write 
            elif cmd == "write":
                tx = parse_bytes(rest)
                t0 = time.monotonic()
                session.write(tx)
                dt = (time.monotonic() - t0) * 1000
                print(f"  wrote {len(tx)} byte(s)  "
                      f"{_c(tx.hex(' ').upper(), C.CYAN)}"
                      f"  {_c(f'{dt:.2f} ms', C.DIM)}")

            #  read 
            elif cmd == "read":
                toks = rest.split()
                n     = int(toks[0]) if toks else 1
                fill  = int(toks[1], 0) if len(toks) > 1 else 0xFF
                t0 = time.monotonic()
                rx = session.read(n, fill)
                dt = (time.monotonic() - t0) * 1000
                print(f"  RX ({n}B, fill=0x{fill:02X})  "
                      f"{_c(rx.hex(' ').upper(), C.GREEN)}")
                if n > 16:
                    print(hexdump(rx))
                print(_c(f"  {dt:.2f} ms", C.DIM))

            #  reg 
            elif cmd == "reg":
                toks = rest.split()
                if len(toks) < 2:
                    print("  Usage: reg <addr_hex> <n_bytes>")
                    continue
                reg_addr = int(toks[0], 0)
                n_bytes  = int(toks[1])
                run_reg_read(session, reg_addr, n_bytes)

            #  loop 
            elif cmd == "loop":
                n = int(rest) if rest.strip() else 32
                print(f"  Loopback test ({n} bytes) — MOSI↔MISO must be shorted")
                run_loopback(session, n)

            #  sweep 
            elif cmd == "sweep":
                print("  Clock speed sweep — MOSI↔MISO must be shorted")
                run_speed_sweep(session)

            #  freq 
            elif cmd == "freq":
                hz = parse_freq(rest)
                session.freq = hz
                session.close()
                session.connect()
                print(f"  Frequency set to {_c(freq_str(hz), C.CYAN)}")

            #  mode 
            elif cmd == "mode":
                m = int(rest.strip())
                if m not in (0, 1, 2, 3):
                    print("  Mode must be 0, 1, 2, or 3")
                    continue
                session.mode = m
                session.close()
                session.connect()
                cpol = m >> 1
                cpha = m & 1
                print(f"  SPI mode {m} "
                      f"{_c(f'(CPOL={cpol}, CPHA={cpha})', C.DIM)}")

            #  cs 
            elif cmd == "cs":
                pin = rest.strip().upper()
                session.cs_name = pin
                session.close()
                session.connect()
                print(f"  CS pin set to {_c(pin, C.CYAN)}")

            #  info 
            elif cmd == "info":
                print(f"  {_c(session.info, C.CYAN)}")

            #  pins 
            elif cmd == "pins":
                print(PIN_REF)

            #  help 
            elif cmd in ("help", "?", "h"):
                print(HELP_TEXT)

            #  quit 
            elif cmd in ("quit", "exit", "q"):
                break

            #  sb.* 
            elif cmd == "sb.nop":
                sb_nop(session)

            elif cmd == "sb.reset":
                sb_reset(session)

            elif cmd == "sb.silence":
                n = int(rest.strip()) if rest.strip() else 1
                sb_silence(session, n)

            elif cmd == "sb.tone":
                toks = rest.split()
                if not toks:
                    print("  Usage: sb.tone <hz> [frames=100] [amp=0.5]")
                else:
                    freq_hz  = float(toks[0])
                    n_frames = int(toks[1])   if len(toks) > 1 else 100
                    amp      = float(toks[2]) if len(toks) > 2 else 0.5
                    sb_tone(session, freq_hz, n_frames, amp)

            elif cmd == "sb.wav":
                if not rest.strip():
                    print("  Usage: sb.wav <path/to/file.wav>")
                else:
                    sb_wav(session, rest.strip().strip('"').strip("'"))

            elif cmd == "sb.cmd":
                toks = rest.rsplit(None, 1)
                import re
                if len(toks) == 2 and re.fullmatch(r"[DdCc]\d", toks[1]):
                    json_str = toks[0].strip()
                    cmd_cs   = toks[1].upper()
                else:
                    json_str = rest.strip()
                    cmd_cs   = "D4"
                if not json_str:
                    print('  Usage: sb.cmd {"cmd":"vol","val":80} [cs_pin]')
                else:
                    sb_cmd(session, json_str, cmd_cs)

            elif cmd == "sb.info":
                sb_info()

            #  unknown 
            else:
                print(f"  {_c(f'Unknown command: {cmd}', C.YELLOW)}"
                      f"  (type 'help')")

        except (ValueError, IndexError) as e:
            print(f"  {_c(f'Parse error: {e}', C.RED)}")
        except Exception as e:
            print(f"  {_c(f'SPI error: {e}', C.RED, C.BOLD)}")

    session.close()
    print(_c("Bye.", C.DIM))


#  main 

def main():
    parser = argparse.ArgumentParser(
        description="Interactive SPI tester for FT232H via Adafruit Blinka",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Examples:
              python spi_tester.py
              python spi_tester.py --cs C0 --freq 4000000 --mode 0
              python spi_tester.py --freq 1MHz
        """),
    )
    parser.add_argument("--cs",   default="D4",      help="CS pin (default: D4)")
    parser.add_argument("--freq", default="1000000", help="SPI clock Hz (default: 1000000)")
    parser.add_argument("--mode", default=0, type=int, choices=[0,1,2,3],
                        help="SPI mode 0-3 (default: 0)")
    parser.add_argument("--bits", default=8, type=int,
                        help="bits per word (default: 8)")
    args = parser.parse_args()

    freq = parse_freq(args.freq)

    print(_c(r"""
  ╔═════════════════════════════════════════════════╗
  ║   SPI Tester  ·  FT232H + Blinka + Southbridge ║
  ╚═════════════════════════════════════════════════╝""", C.CYAN, C.BOLD))

    session = SPISession(freq=freq, mode=args.mode,
                         cs_pin=args.cs, bits=args.bits)
    print(f"  Connecting …  {_c(session.info, C.DIM)}")

    try:
        session.connect()
    except Exception as e:
        print(f"\n  {_c('Connection failed:', C.RED, C.BOLD)} {e}")
        print(_c("\n  Checklist:", C.YELLOW))
        print("    1. FT232H plugged in via USB")
        print("    2. BLINKA_FT232H=1 environment variable set")
        print("    3. pyftdi / adafruit-blinka installed")
        print("    4. On Linux: udev rule for FT232H, or run as root")
        print("    5. On Windows: Zadig – install WinUSB driver for FT232H\n")
        sys.exit(1)

    print(_c("  Connected.\n", C.GREEN))
    repl(session)


if __name__ == "__main__":
    main()
