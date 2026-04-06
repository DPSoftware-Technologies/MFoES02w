#!/usr/bin/env python3
import struct
import threading
import time
import queue
import argparse
import sys
from typing import Optional, Dict, Callable

try:
    import usb.core
    import usb.util
except ImportError:
    print("ERROR: pyusb not installed. Run: pip install pyusb")
    sys.exit(1)

FRAME_MAGIC   = 0x4D554244   # "MUBD"
HEADER_FMT    = "<IBBHI"     # magic(4), channel(1), flags(1), reserved(2), length(4)
HEADER_SIZE   = struct.calcsize(HEADER_FMT)  # 12 bytes

FLAG_DATA  = 0x00
FLAG_OPEN  = 0x01
FLAG_CLOSE = 0x02
FLAG_ACK   = 0x03
FLAG_ERROR = 0x04

DEFAULT_VID = 0x750C   # Linux Foundation
DEFAULT_PID = 0x0544   # Multifunction Composite Gadget

USB_BUF_SIZE   = 65536              # 64 KB per USB bulk transfer (libusb limit)
MAX_FRAME_SIZE = 512 * 1024         # 512 KB — max payload in a single MUBD frame

def encode_frame(channel: int, flags: int, payload: bytes = b"") -> bytes:
    hdr = struct.pack(HEADER_FMT,
                      FRAME_MAGIC, channel, flags, 0, len(payload))
    return hdr + payload


def decode_header(data: bytes) -> Optional[tuple]:
    """Returns (magic, channel, flags, reserved, length) or None."""
    if len(data) < HEADER_SIZE:
        return None
    return struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
class UsbChannel:
    """Represents one logical channel over USB."""

    def __init__(self, channel_id: int, manager: "UsbChannelManager"):
        self.channel_id  = channel_id
        self._mgr        = manager
        self._rx_queue   = queue.Queue()
        self._open       = True
        self._on_data_cb: Optional[Callable[[bytes], None]] = None


    def send(self, data: bytes) -> bool:
        """
        Send data over this channel.

        If data fits in one USB transfer (≤ USB_BUF_SIZE - HEADER_SIZE bytes),
        it is sent as a single MUBD frame.  Larger payloads are automatically
        split by send_large() — each piece becomes its own complete frame and
        the receiver must reassemble them.  For the OTA use-case the receiver
        (usbd / UsbdClient) reassembles based on the 4-byte length prefix, so
        large sends are transparent to the application layer.
        """
        if not self._open:
            return False
        max_payload = USB_BUF_SIZE - HEADER_SIZE  # 65524 bytes
        if len(data) <= max_payload:
            return self._mgr._send_frame(self.channel_id, FLAG_DATA, data)
        return self.send_large(data, chunk_size=max_payload)

    def send_large(self, data: bytes, chunk_size: int = USB_BUF_SIZE - HEADER_SIZE) -> bool:
        """Send large data split into chunks, each as its own complete MUBD frame."""
        max_payload = USB_BUF_SIZE - HEADER_SIZE  # never exceed one USB transfer
        if chunk_size > max_payload:
            chunk_size = max_payload
        for i in range(0, len(data), chunk_size):
            chunk = data[i:i + chunk_size]
            if not self._mgr._send_frame(self.channel_id, FLAG_DATA, chunk):
                return False
        return True

    def recv(self, timeout: float = 5.0) -> Optional[bytes]:
        """
        Blocking receive. Returns payload bytes or None on timeout/close.
        timeout: seconds (float). Use None for blocking indefinitely.
        """
        try:
            return self._rx_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def recv_nowait(self) -> Optional[bytes]:
        """Non-blocking receive. Returns None if no data ready."""
        try:
            return self._rx_queue.get_nowait()
        except queue.Empty:
            return None

    def recv_all(self, total_bytes: int, timeout: float = 30.0) -> Optional[bytes]:
        """Receive until exactly total_bytes accumulated. Good for benchmarks."""
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while len(buf) < total_bytes:
            remaining_t = deadline - time.monotonic()
            if remaining_t <= 0:
                return None
            chunk = self.recv(timeout=remaining_t)
            if chunk is None:
                return None
            buf.extend(chunk)
        return bytes(buf[:total_bytes])

    def set_data_callback(self, cb: Callable[[bytes], None]):
        self._on_data_cb = cb

    def close(self):
        if self._open:
            self._open = False
            self._mgr._send_frame(self.channel_id, FLAG_CLOSE)
            self._mgr._remove_channel(self.channel_id)


    def _dispatch(self, payload: bytes):
        if self._on_data_cb:
            try:
                self._on_data_cb(payload)
            except Exception as e:
                print(f"[ch{self.channel_id}] callback error: {e}")
        else:
            self._rx_queue.put(payload)

    def _mark_closed(self):
        self._open = False
        self._rx_queue.put(None)  # Unblock any waiting recv()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __repr__(self):
        return f"<UsbChannel id={self.channel_id} open={self._open}>"

class UsbChannelManager:
    """
    Manages the USB connection and demultiplexes channels.

    Example:
        mgr = UsbChannelManager()
        mgr.connect()

        ch0 = mgr.open_channel(0)
        ch0.send(b"ping")
        reply = ch0.recv(timeout=2.0)

        mgr.close()
    """

    def __init__(self,
                 vid: int = DEFAULT_VID,
                 pid: int = DEFAULT_PID,
                 interface: int = 0):
        self.vid        = vid
        self.pid        = pid
        self.iface_num  = interface
        self._dev       = None
        self._ep_out    = None  # bulk OUT (PC→device, ep1)
        self._ep_in     = None  # bulk IN  (device→PC, ep2)
        self._channels: Dict[int, UsbChannel] = {}
        self._ch_lock   = threading.Lock()
        self._tx_lock   = threading.Lock()
        self._rx_thread = None
        self._running   = False
        self._rx_buf    = bytearray()  # reassembly buffer


    def connect(self, timeout: float = 10.0) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            dev = usb.core.find(idVendor=self.vid, idProduct=self.pid)
            if dev is not None:
                self._dev = dev
                break
            time.sleep(0.5)

        if self._dev is None:
            print(f"[usb] device {self.vid:04x}:{self.pid:04x} not found "
                  f"(check lsusb, VID/PID, or permissions)")
            return False

        try:
            if sys.platform != "win32":
                try:
                    if self._dev.is_kernel_driver_active(self.iface_num):
                        self._dev.detach_kernel_driver(self.iface_num)
                except (NotImplementedError, usb.core.USBError):
                    pass  # Not supported on this platform, ignore
            self._dev.set_configuration()
        except usb.core.USBError as e:
            print(f"[usb] config error: {e}")
            return False

        # Find endpoints
        cfg   = self._dev.get_active_configuration()
        iface = cfg[(self.iface_num, 0)]

        self._ep_out = usb.util.find_descriptor(
            iface,
            custom_match=lambda e:
                usb.util.endpoint_direction(e.bEndpointAddress)
                == usb.util.ENDPOINT_OUT
        )
        self._ep_in = usb.util.find_descriptor(
            iface,
            custom_match=lambda e:
                usb.util.endpoint_direction(e.bEndpointAddress)
                == usb.util.ENDPOINT_IN
        )

        if self._ep_out is None or self._ep_in is None:
            print("[usb] could not find bulk endpoints")
            return False

        self._running = True
        self._rx_thread = threading.Thread(target=self._rx_loop,
                                           name="usbd-rx", daemon=True)
        self._rx_thread.start()
        print(f"[usb] connected to {self.vid:04x}:{self.pid:04x} "
              f"ep_out=0x{self._ep_out.bEndpointAddress:02x} "
              f"ep_in=0x{self._ep_in.bEndpointAddress:02x}")
        return True

    def close(self):
        self._running = False
        with self._ch_lock:
            for ch in list(self._channels.values()):
                ch._mark_closed()
            self._channels.clear()
        if self._dev is not None:
            usb.util.dispose_resources(self._dev)
            self._dev = None

    def open_channel(self, channel_id: int = 0) -> UsbChannel:
        """
        Open a logical channel. The remote daemon will receive FLAG_OPEN.
        channel_id: 0-31 (must match what the device app opened)
        """
        with self._ch_lock:
            if channel_id in self._channels:
                return self._channels[channel_id]
            ch = UsbChannel(channel_id, self)
            self._channels[channel_id] = ch

        self._send_frame(channel_id, FLAG_OPEN)
        return ch

    def _remove_channel(self, channel_id: int):
        with self._ch_lock:
            self._channels.pop(channel_id, None)


    def _send_frame(self, channel: int, flags: int, payload: bytes = b"") -> bool:
        """
        Send one complete MUBD frame as a single USB bulk transfer.

        A MUBD frame MUST be delivered as one USB write — usbd on the device
        processes each bulk transfer independently and expects a complete
        [MUBD header + payload] in each one. Splitting a frame across multiple
        writes corrupts the framing.

        Maximum payload per frame is USB_BUF_SIZE - HEADER_SIZE (= 65524 bytes).
        Callers sending larger data must use send_large() which splits at the
        logical message level (each split piece becomes its own complete frame).
        """
        if self._ep_out is None:
            return False
        frame = encode_frame(channel, flags, payload)
        if len(frame) > USB_BUF_SIZE:
            print(f"[usb] _send_frame: frame too large ({len(frame)} bytes) — "
                  f"use send_large() for payloads > {USB_BUF_SIZE - HEADER_SIZE} bytes")
            return False
        print(f"[debug] sending frame ch={channel} flags={flags} "
              f"payload={len(payload)} frame={len(frame)} to ep={self._ep_out.bEndpointAddress:#04x}")
        with self._tx_lock:
            try:
                self._ep_out.write(frame, timeout=5000)
                return True
            except usb.core.USBError as e:
                print(f"[usb] send error: {e}")
                return False


    def _rx_loop(self):
        """Background thread: reads USB bulk IN, demuxes frames."""
        while self._running:
            try:
                raw = self._ep_in.read(USB_BUF_SIZE, timeout=1000)
                if raw:
                    self._rx_buf.extend(bytes(raw))
                    self._process_rx_buf()
            except usb.core.USBTimeoutError:
                continue
            except usb.core.USBError as e:
                if self._running:
                    print(f"[usb] rx error: {e}")
                    time.sleep(0.1)

    def _process_rx_buf(self):
        """Parse as many complete frames from _rx_buf as possible."""
        while len(self._rx_buf) >= HEADER_SIZE:
            hdr = decode_header(self._rx_buf)
            if hdr is None:
                break
            magic, channel, flags, _, plen = hdr

            if magic != FRAME_MAGIC:
                # Resync: find next magic
                idx = self._rx_buf.find(
                    struct.pack("<I", FRAME_MAGIC), 1)
                if idx < 0:
                    self._rx_buf.clear()
                else:
                    del self._rx_buf[:idx]
                print("[usb] rx resync")
                continue

            if plen > MAX_FRAME_SIZE:
                print(f"[usb] oversized frame plen={plen}, dropping")
                self._rx_buf.clear()
                break

            total = HEADER_SIZE + plen
            if len(self._rx_buf) < total:
                break   # Wait for more data

            payload = bytes(self._rx_buf[HEADER_SIZE:total])
            del self._rx_buf[:total]

            self._dispatch(channel, flags, payload)

    def _dispatch(self, channel: int, flags: int, payload: bytes):
        with self._ch_lock:
            ch = self._channels.get(channel)

        if flags == FLAG_OPEN:
            if ch is None:
                # Auto-create channel when device opens one
                with self._ch_lock:
                    ch = UsbChannel(channel, self)
                    self._channels[channel] = ch
                print(f"[usb] channel {channel} opened by device")
        elif flags == FLAG_CLOSE:
            if ch:
                ch._mark_closed()
                with self._ch_lock:
                    self._channels.pop(channel, None)
            print(f"[usb] channel {channel} closed by device")
        elif flags == FLAG_DATA:
            if ch and payload:
                ch._dispatch(payload)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

def run_benchmark(ch: UsbChannel, mb: float = 10.0,
                  chunk_kb: int = 64):
    """
    Send `mb` MB of data to device, measure throughput.
    Assumes device echoes data back (example_app does this).
    """
    total_bytes  = int(mb * 1024 * 1024)
    chunk_size   = chunk_kb * 1024
    payload      = bytes(range(256)) * (chunk_size // 256 + 1)
    payload      = payload[:chunk_size]

    print(f"[bench] sending {mb:.1f} MB in {chunk_size // 1024} KB chunks...")

    t0 = time.monotonic()
    sent = 0
    while sent < total_bytes:
        chunk = payload[:min(chunk_size, total_bytes - sent)]
        if not ch.send(chunk):
            print("[bench] send failed")
            return
        sent += len(chunk)

    tx_time = time.monotonic() - t0
    tx_mbps = (total_bytes / tx_time) / (1024 * 1024)
    print(f"[bench] TX done: {total_bytes / 1024:.0f} KB "
          f"in {tx_time:.3f}s = {tx_mbps:.2f} MB/s")

    print("[bench] waiting for echo...")
    t1 = time.monotonic()
    received = ch.recv_all(total_bytes, timeout=60.0)
    if received is None:
        print("[bench] recv timeout/error")
        return
    rx_time = time.monotonic() - t1
    rx_mbps = (total_bytes / rx_time) / (1024 * 1024)
    rtt = time.monotonic() - t0
    print(f"[bench] RX done: {len(received) / 1024:.0f} KB "
          f"in {rx_time:.3f}s = {rx_mbps:.2f} MB/s")
    print(f"[bench] round-trip: {rtt:.3f}s")

def main():
    parser = argparse.ArgumentParser(
        description="PC-side USB channel client for usbd on Pi Zero 2W")
    parser.add_argument("--vid",     type=lambda x: int(x, 16),
                        default=DEFAULT_VID,
                        help=f"USB Vendor ID (default: {DEFAULT_VID:#06x})")
    parser.add_argument("--pid",     type=lambda x: int(x, 16),
                        default=DEFAULT_PID,
                        help=f"USB Product ID (default: {DEFAULT_PID:#06x})")
    parser.add_argument("--channel", type=int, default=0,
                        help="Channel ID to use (default: 0)")
    parser.add_argument("--send",    type=str, default=None,
                        help="Send a string message")
    parser.add_argument("--bench",   type=float, default=None,
                        metavar="MB",
                        help="Run throughput benchmark sending N MB")
    parser.add_argument("--listen",  action="store_true",
                        help="Just listen and print received data")
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="Connect timeout seconds (default: 10)")
    args = parser.parse_args()

    mgr = UsbChannelManager(vid=args.vid, pid=args.pid)
    print(f"[cli] connecting to {args.vid:04x}:{args.pid:04x} ...")
    if not mgr.connect(timeout=args.timeout):
        sys.exit(1)

    ch = mgr.open_channel(args.channel)
    print(f"[cli] channel {ch.channel_id} open")

    try:
        if args.bench is not None:
            run_benchmark(ch, mb=args.bench)

        elif args.send is not None:
            payload = args.send.encode()
            ch.send(payload)
            print(f"[cli] sent {len(payload)} bytes: {args.send!r}")
            print("[cli] waiting for echo...")
            reply = ch.recv(timeout=5.0)
            if reply:
                print(f"[cli] echo ({len(reply)} bytes): {reply!r}")
            else:
                print("[cli] no reply (timeout)")

        elif args.listen:
            print("[cli] listening (Ctrl+C to stop)...")
            while True:
                data = ch.recv(timeout=1.0)
                if data:
                    print(f"[cli] ch{ch.channel_id} rx {len(data)} bytes: "
                          f"{data[:64]!r}{'...' if len(data) > 64 else ''}")

        else:
            # Interactive REPL
            print("[cli] interactive mode. Type messages, empty line to quit.")
            while True:
                try:
                    line = input("send> ")
                except EOFError:
                    break
                if not line:
                    break
                ch.send(line.encode())
                reply = ch.recv(timeout=3.0)
                if reply:
                    print(f"recv: {reply.decode(errors='replace')!r}")
                else:
                    print("(no reply)")

    except KeyboardInterrupt:
        print("\n[cli] interrupted")
    finally:
        ch.close()
        mgr.close()


if __name__ == "__main__":
    main()