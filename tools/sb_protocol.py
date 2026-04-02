"""
sb_protocol.py – Python mirror of protocol.h

Builds and parses southbridge wire frames so the FT232H SPI tester
can talk directly to a Pico2W running SouthbridgePico firmware.
"""

import struct
import math
import wave
import os
from dataclasses import dataclass, field
from typing import Optional

#  tunables (must match protocol.h) 
SB_AUDIO_SAMPLES_PER_FRAME = 128
SB_SAMPLE_BYTES            = 2
SB_CHANNELS                = 1
SB_SAMPLE_RATE             = 16000

SB_AUDIO_PAYLOAD_BYTES = SB_AUDIO_SAMPLES_PER_FRAME * SB_SAMPLE_BYTES * SB_CHANNELS

SB_FRAME_HEADER_BYTES  = 8
SB_FRAME_CRC_BYTES     = 2
SB_FRAME_RAW           = SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES + SB_FRAME_CRC_BYTES
SB_FRAME_BYTES         = (SB_FRAME_RAW + 3) & ~3   # align to 4 bytes

SB_CMD_FRAME_BYTES     = 64

#  frame type constants 
FTYPE_AUDIO   = 0x01
FTYPE_SILENCE = 0x02
FTYPE_RESET   = 0xFE
FTYPE_NOP     = 0xFF

FTYPE_NAMES = {
    FTYPE_AUDIO:   "AUDIO",
    FTYPE_SILENCE: "SILENCE",
    FTYPE_RESET:   "RESET",
    FTYPE_NOP:     "NOP",
}

#  status bit constants 
STATUS_OK       = 0x00
STATUS_OVERFLOW = 0x01
STATUS_UNDERRUN = 0x02
STATUS_CRC_ERR  = 0x04
STATUS_BUSY     = 0x08

#  magic bytes 
AUDIO_MAGIC_HI = 0x5B
AUDIO_MAGIC_LO = 0xA4
CMD_MAGIC_HI   = 0x5C
CMD_MAGIC_LO   = 0xB5


#  CRC-16/CCITT (matches C implementation in protocol.h) 

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


#  status decoding 

def decode_status(status: int) -> str:
    if status == STATUS_OK:
        return "OK"
    parts = []
    if status & STATUS_OVERFLOW: parts.append("OVERFLOW")
    if status & STATUS_UNDERRUN: parts.append("UNDERRUN")
    if status & STATUS_CRC_ERR:  parts.append("CRC_ERR")
    if status & STATUS_BUSY:     parts.append("BUSY")
    return " | ".join(parts) if parts else f"UNKNOWN(0x{status:02X})"


#  audio frame builder 

def build_audio_frame(pcm: bytes, seq: int) -> bytes:
    """
    Build a complete SB_FRAME_BYTES audio frame ready to clock out over SPI.

    pcm   : exactly SB_AUDIO_PAYLOAD_BYTES of raw 16-bit signed LE PCM
    seq   : 32-bit sequence counter (wraps naturally)
    """
    if len(pcm) != SB_AUDIO_PAYLOAD_BYTES:
        raise ValueError(
            f"PCM must be exactly {SB_AUDIO_PAYLOAD_BYTES} bytes, got {len(pcm)}")

    header = bytes([
        AUDIO_MAGIC_HI,
        AUDIO_MAGIC_LO,
        FTYPE_AUDIO,
        0x00,                          # status (master → slave = 0)
    ]) + struct.pack("<I", seq & 0xFFFFFFFF)

    payload_region = header + pcm
    crc = crc16(payload_region)

    frame = payload_region + struct.pack("<H", crc)
    # pad to SB_FRAME_BYTES
    frame += b"\x00" * (SB_FRAME_BYTES - len(frame))
    return frame


def build_nop_frame(seq: int = 0) -> bytes:
    """Build a NOP frame (no payload, tells Pico to ignore)."""
    header = bytes([AUDIO_MAGIC_HI, AUDIO_MAGIC_LO, FTYPE_NOP, 0x00]) \
           + struct.pack("<I", seq & 0xFFFFFFFF)
    crc = crc16(header)
    frame = header + struct.pack("<H", crc)
    frame += b"\x00" * (SB_FRAME_BYTES - len(frame))
    return frame


def build_silence_frame(seq: int = 0) -> bytes:
    """Build a SILENCE frame (tells Pico to output silence)."""
    header = bytes([AUDIO_MAGIC_HI, AUDIO_MAGIC_LO, FTYPE_SILENCE, 0x00]) \
           + struct.pack("<I", seq & 0xFFFFFFFF)
    crc = crc16(header)
    frame = header + struct.pack("<H", crc)
    frame += b"\x00" * (SB_FRAME_BYTES - len(frame))
    return frame


def build_reset_frame() -> bytes:
    """Build a RESET frame (tells Pico to flush its audio queue)."""
    header = bytes([AUDIO_MAGIC_HI, AUDIO_MAGIC_LO, FTYPE_RESET, 0x00]) \
           + struct.pack("<I", 0)
    crc = crc16(header)
    frame = header + struct.pack("<H", crc)
    frame += b"\x00" * (SB_FRAME_BYTES - len(frame))
    return frame


#  command frame builder/parser 

def build_cmd_frame(json_str: str) -> bytes:
    """Build a 64-byte command frame for the CS1 channel."""
    payload = json_str.encode()
    if len(payload) > SB_CMD_FRAME_BYTES - 4:
        raise ValueError(
            f"JSON too long: {len(payload)} bytes, max {SB_CMD_FRAME_BYTES - 4}")
    frame = bytes([CMD_MAGIC_HI, CMD_MAGIC_LO, len(payload), 0x00])
    frame += payload
    frame += b"\x00" * (SB_CMD_FRAME_BYTES - len(frame))
    return frame


#  response parser 

@dataclass
class AudioResponse:
    """Parsed MISO data from a CS0 audio transfer."""
    raw:        bytes
    magic_ok:   bool
    frame_type: int
    status:     int
    seq:        int
    crc_ok:     bool
    status_str: str = field(init=False)
    ftype_str:  str = field(init=False)

    def __post_init__(self):
        self.status_str = decode_status(self.status)
        self.ftype_str  = FTYPE_NAMES.get(self.frame_type,
                                          f"UNKNOWN(0x{self.frame_type:02X})")


def parse_audio_response(rx: bytes) -> AudioResponse:
    """Parse the MISO bytes returned during a CS0 audio frame transfer."""
    if len(rx) < SB_FRAME_BYTES:
        rx = rx + b"\x00" * (SB_FRAME_BYTES - len(rx))

    magic_ok = (rx[0] == AUDIO_MAGIC_HI and rx[1] == AUDIO_MAGIC_LO)
    ftype    = rx[2]
    status   = rx[3]
    seq      = struct.unpack_from("<I", rx, 4)[0]

    # CRC check on first SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES bytes
    crc_data  = rx[:SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES]
    crc_rcv   = struct.unpack_from("<H", rx,
                    SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES)[0]
    crc_calc  = crc16(crc_data)
    crc_ok    = (crc_rcv == crc_calc)

    return AudioResponse(
        raw=rx, magic_ok=magic_ok, frame_type=ftype,
        status=status, seq=seq, crc_ok=crc_ok)


@dataclass
class CmdResponse:
    """Parsed MISO data from a CS1 command frame transfer."""
    raw:      bytes
    magic_ok: bool
    length:   int
    json_str: str


def parse_cmd_response(rx: bytes) -> CmdResponse:
    if len(rx) < SB_CMD_FRAME_BYTES:
        rx = rx + b"\x00" * (SB_CMD_FRAME_BYTES - len(rx))
    magic_ok = (rx[0] == CMD_MAGIC_HI and rx[1] == CMD_MAGIC_LO)
    length   = rx[2] if magic_ok else 0
    json_str = ""
    if magic_ok and length > 0 and length <= SB_CMD_FRAME_BYTES - 4:
        try:
            json_str = rx[4:4 + length].decode("utf-8", errors="replace")
        except Exception:
            json_str = ""
    return CmdResponse(raw=rx, magic_ok=magic_ok, length=length, json_str=json_str)


#  WAV / PCM helpers 

def wav_to_frames(path: str):
    """
    Generator: yield successive (frame_bytes, seq) tuples from a WAV file.
    The WAV is resampled (simple decimation/repeat) to SB_SAMPLE_RATE if needed,
    and converted to mono 16-bit signed LE.
    Yields SB_FRAME_BYTES bytes per iteration until the file is exhausted.
    """
    with wave.open(path, "rb") as wf:
        n_ch    = wf.getnchannels()
        samp_w  = wf.getsampwidth()
        rate    = wf.getframerate()
        n_total = wf.getnframes()

        if samp_w not in (1, 2):
            raise ValueError(f"Unsupported sample width: {samp_w} bytes")

        raw = wf.readframes(n_total)

    # Parse to list of int16 samples (mono)
    if samp_w == 2:
        fmt = f"<{n_total * n_ch}h"
        all_samples = list(struct.unpack(fmt, raw))
    else:
        # 8-bit unsigned → 16-bit signed
        all_samples = [(b - 128) << 8 for b in raw]

    # Mix to mono
    if n_ch > 1:
        mono = [sum(all_samples[i:i + n_ch]) // n_ch
                for i in range(0, len(all_samples), n_ch)]
    else:
        mono = all_samples

    # Resample to SB_SAMPLE_RATE (nearest-neighbour)
    if rate != SB_SAMPLE_RATE:
        ratio   = SB_SAMPLE_RATE / rate
        n_out   = int(len(mono) * ratio)
        resampled = [mono[min(int(i / ratio), len(mono) - 1)]
                     for i in range(n_out)]
        mono = resampled

    # Yield frames
    seq = 0
    for i in range(0, len(mono), SB_AUDIO_SAMPLES_PER_FRAME):
        chunk = mono[i:i + SB_AUDIO_SAMPLES_PER_FRAME]
        # pad last frame with silence
        if len(chunk) < SB_AUDIO_SAMPLES_PER_FRAME:
            chunk += [0] * (SB_AUDIO_SAMPLES_PER_FRAME - len(chunk))
        pcm = struct.pack(f"<{SB_AUDIO_SAMPLES_PER_FRAME}h", *chunk)
        yield build_audio_frame(pcm, seq), seq
        seq = (seq + 1) & 0xFFFFFFFF


def sine_frames(freq_hz: float = 440.0, n_frames: int = 100,
                amplitude: float = 0.5):
    """
    Generator: yield (frame_bytes, seq) of a sine wave tone.
    Useful for quick audio output testing without a WAV file.
    """
    import math
    phase = 0.0
    phase_inc = 2.0 * math.pi * freq_hz / SB_SAMPLE_RATE
    peak = int(32767 * amplitude)
    seq  = 0
    for _ in range(n_frames):
        samples = []
        for _ in range(SB_AUDIO_SAMPLES_PER_FRAME):
            samples.append(int(peak * math.sin(phase)))
            phase = (phase + phase_inc) % (2.0 * math.pi)
        pcm = struct.pack(f"<{SB_AUDIO_SAMPLES_PER_FRAME}h", *samples)
        yield build_audio_frame(pcm, seq), seq
        seq = (seq + 1) & 0xFFFFFFFF
