"""
usb_channel_ota.py  —  PlatoonLabs MFoES OTA Uploader
Sends a firmware update package to otad over usb_channel.

Usage:
    python usb_channel_ota.py [--version "1.2.3"] [--chunk-mb 1]

Wire protocol matches otad.cpp exactly.
"""

import usb_channel as mfoes02wusb
import zipfile
import os
import hashlib
import shutil
import struct
import argparse
import time

#  Config 

VID          = 0x750C
PID          = 0x0544
OTA_CHANNEL  = 10

OTA_MAGIC  = 0x0A
OTA_BEGIN  = 0x01
OTA_CHUNK  = 0x02
OTA_END    = 0x03
OTA_ACK    = 0x04
OTA_NAK    = 0x05
OTA_ABORT  = 0x06

FILES_TO_ZIP = [
    ("build-aarch64/bin/mfoes02w", "mfoes02w/mfoes02w"),
    ("build-aarch64/bin/usbd",     "usbd"),
    ("build-aarch64/bin/otad",     "otad"),
]

FOLDERS_TO_ZIP = [
    ("build-aarch64/lib/", "mfoes02w/libs"),
]

#  Protocol helpers 

def xor_checksum(data: bytes) -> int:
    cs = 0
    for b in data:
        cs ^= b
    return cs

def encode_packet(pkt_type: int, payload: bytes) -> bytes:
    """[magic][type][4B len][payload][1B xor_cs]"""
    header = struct.pack("<BBI", OTA_MAGIC, pkt_type, len(payload))
    cs = bytes([xor_checksum(payload)])
    return header + payload + cs

def recv_exact(ch, n: int, timeout_s: float = 30.0) -> bytes:
    buf = b""
    deadline = time.time() + timeout_s
    while len(buf) < n:
        remaining = deadline - time.time()
        if remaining <= 0:
            raise TimeoutError(f"recv_exact: wanted {n}, got {len(buf)}")
        chunk = ch.recv(remaining)
        if chunk is None:
            # Timed out waiting, but maybe we still have time in the overall deadline
            # Check again and continue the loop
            continue
        if not chunk:
            # Empty data means connection closed
            raise ConnectionError("Channel closed during recv")
        buf += chunk
    return buf

def recv_packet(ch, timeout_s: float = 60.0) -> tuple[int, bytes]:
    """Returns (pkt_type, payload)"""
    header = recv_exact(ch, 6, timeout_s)
    magic, pkt_type, payload_len = struct.unpack_from("<BBI", header)
    if magic != OTA_MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:02X}")
    payload = recv_exact(ch, payload_len, timeout_s) if payload_len else b""
    cs_recv = recv_exact(ch, 1, timeout_s)[0]
    cs_calc = xor_checksum(payload)
    if cs_recv != cs_calc:
        raise ValueError(f"Checksum mismatch: got 0x{cs_recv:02X} expected 0x{cs_calc:02X}")
    return pkt_type, payload

def expect_ack(ch, part: int = 0):
    pkt_type, payload = recv_packet(ch)
    if pkt_type == OTA_NAK:
        reason = payload[4:].rstrip(b'\x00').decode('utf-8', errors='replace')
        raise RuntimeError(f"NAK for part {part}: {reason}")
    if pkt_type != OTA_ACK:
        raise RuntimeError(f"Expected ACK, got 0x{pkt_type:02X}")
    ack_part = struct.unpack_from("<I", payload)[0]
    return ack_part

#  Build steps 

def md5_file(path: str) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def md5_bytes(data: bytes) -> bytes:
    return hashlib.md5(data).digest()

def build_zip(tmp_dir: str) -> str:
    zip_path = os.path.join(tmp_dir, "upload.zip")
    print("Zipping files...")
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for src, dst in FILES_TO_ZIP:
            if os.path.exists(src):
                zf.write(src, dst)
                print(f"   + {src} → {dst}")
            else:
                print(f"   Skipping missing: {src}")
        for src_dir, dst_dir in FOLDERS_TO_ZIP:
            if not os.path.isdir(src_dir):
                print(f"   Skipping missing folder: {src_dir}")
                continue
            for root, _, files in os.walk(src_dir):
                for fname in files:
                    full = os.path.join(root, fname)
                    rel  = os.path.relpath(full, src_dir)
                    zf.write(full, os.path.join(dst_dir, rel))
    return zip_path

def split_zip(zip_path: str, chunk_size_bytes: int, tmp_dir: str) -> list[dict]:
    parts = []
    idx = 1
    print(f"Chunking zip ({chunk_size_bytes // 1024 // 1024} MB chunks)...")
    with open(zip_path, "rb") as f:
        while True:
            data = f.read(chunk_size_bytes)
            if not data:
                break
            out = os.path.join(tmp_dir, f"part{idx}.bin")
            with open(out, "wb") as pf:
                pf.write(data)
            parts.append({
                "number": idx,
                "path":   out,
                "data":   data,
                "size":   len(data),
                "md5":    md5_bytes(data),
            })
            print(f"   part {idx}: {len(data)} bytes")
            idx += 1
    return parts

#  OTA session 

def run_ota(ch, parts: list[dict], total_size: int, zip_md5_bytes: bytes, version: str):
    print(f"\n Starting OTA session — {len(parts)} parts, {total_size} bytes, v{version}")

    # OTA_BEGIN payload: [4B total_parts][8B total_size][16B md5][version str]
    version_enc = version.encode('utf-8')[:32].ljust(32, b'\x00')
    begin_payload = struct.pack("<IQ", len(parts), total_size) + zip_md5_bytes + version_enc
    ch.send(encode_packet(OTA_BEGIN, begin_payload))
    ack = expect_ack(ch, 0)
    print(f"  BEGIN ack (part={ack})")

    # OTA_CHUNK for each part
    for part in parts:
        # payload: [4B part_num][4B part_size][16B md5][data]
        chunk_payload = (
            struct.pack("<II", part["number"], part["size"])
            + part["md5"]
            + part["data"]
        )
        ch.send(encode_packet(OTA_CHUNK, chunk_payload))

        ack = expect_ack(ch, part["number"])
        pct = (part["number"] * 100) // len(parts)
        bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
        print(f"   [{bar}] {pct:3d}%  part {part['number']}/{len(parts)}")

    # OTA_END
    ch.send(encode_packet(OTA_END, b""))
    ack = expect_ack(ch, 0xFFFFFFFF)
    print(f"  END ack — device verified zip (ack=0x{ack:08X})")
    print("\n OTA complete! Device is applying update and rebooting.")

#  Main 

def main():
    parser = argparse.ArgumentParser(description="MFoES OTA Uploader")
    parser.add_argument("--version",  default="dev", help="Firmware version string")
    parser.add_argument("--chunk-mb", type=int, default=1, help="Chunk size in MB")
    args = parser.parse_args()

    tmp_dir = "temp_ota"
    if os.path.exists(tmp_dir):
        shutil.rmtree(tmp_dir)
    os.makedirs(tmp_dir)

    try:
        # Build
        zip_path   = build_zip(tmp_dir)
        total_size = os.path.getsize(zip_path)
        zip_md5_hex   = md5_file(zip_path)
        zip_md5_bytes = bytes.fromhex(zip_md5_hex)
        print(f"   zip size : {total_size} bytes")
        print(f"   zip md5  : {zip_md5_hex}")

        parts = split_zip(zip_path, args.chunk_mb * 1024 * 1024, tmp_dir)

        # Connect
        print(f"\nConnecting to MFoES (VID={VID:#06x} PID={PID:#06x})...")
        mgr = mfoes02wusb.UsbChannelManager(vid=VID, pid=PID)
        if not mgr.connect(timeout=30):
            print("Connection timed out.")
            return 1
        ch = mgr.open_channel(OTA_CHANNEL)
        print(f"Connected on channel {OTA_CHANNEL}")

        # OTA
        run_ota(ch, parts, total_size, zip_md5_bytes, args.version)

    except KeyboardInterrupt:
        print("\nInterrupted — sending ABORT...")
        try:
            abort_payload = b"user interrupted\x00"
            ch.send(encode_packet(OTA_ABORT, abort_payload))
        except Exception:
            pass
        return 1
    except Exception as e:
        print(f"\nOTA failed: {e}")
        return 1
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    return 0

if __name__ == "__main__":
    raise SystemExit(main())