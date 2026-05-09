#!/usr/bin/env python3
import struct
import threading
import time
import queue
import argparse
import sys
from typing import Optional, Dict, Callable

# ── backend imports ────────────────────────────────────────────────────────────
# usb1 (pip install libusb1) is preferred: it exposes libusb1's async transfer
# API so NUM_ASYNC_RX reads can be permanently in-flight simultaneously,
# keeping the USB pipe busy while Python processes previous completions.
# PyUSB is the fallback (one synchronous read at a time).
try:
    import usb1 as _usb1
    _HAVE_USB1 = True
except ImportError:
    _HAVE_USB1 = False

try:
    import usb.core
    import usb.util
    _HAVE_PYUSB = True
except ImportError:
    _HAVE_PYUSB = False

if not _HAVE_USB1 and not _HAVE_PYUSB:
    print("ERROR: no USB backend found.\n"
          "  Fast path : pip install libusb1\n"
          "  Fallback  : pip install pyusb")
    sys.exit(1)

# ── constants ──────────────────────────────────────────────────────────────────
FRAME_MAGIC  = 0x4D554244        # "MUBD"
HEADER_FMT   = "<IBBHI"
HEADER_SIZE  = struct.calcsize(HEADER_FMT)  # 12 bytes

FLAG_DATA  = 0x00
FLAG_OPEN  = 0x01
FLAG_CLOSE = 0x02
FLAG_ACK   = 0x03
FLAG_ERROR = 0x04

DEFAULT_VID = 0x750C
DEFAULT_PID = 0x0544

USB_BUF_SIZE   = 512 * 1024       # 512 KB — matches dwc2 device-side limit
MAX_FRAME_SIZE = 4 * 1024 * 1024  # 4 MB  — matches usbd.cpp MAX_FRAME_SIZE
NUM_ASYNC_RX   = 4                # concurrent bulk-IN transfers (usb1 only)

# ── framing helpers ────────────────────────────────────────────────────────────
def encode_frame(channel: int, flags: int, payload: bytes = b"") -> bytes:
    return struct.pack(HEADER_FMT,
                       FRAME_MAGIC, channel, flags, 0, len(payload)) + payload


# ── UsbChannel ─────────────────────────────────────────────────────────────────
class UsbChannel:
    """One logical channel multiplexed over the USB bulk pipe."""

    def __init__(self, channel_id: int, manager: "UsbChannelManager"):
        self.channel_id  = channel_id
        self._mgr        = manager
        self._rx_queue   = queue.Queue()
        self._open       = True
        self._on_data_cb: Optional[Callable[[bytes], None]] = None

    def send(self, data: bytes) -> bool:
        """Send data, splitting into 512 KB frames if needed."""
        if not self._open:
            return False
        max_payload = USB_BUF_SIZE - HEADER_SIZE
        if len(data) <= max_payload:
            return self._mgr._send_frame(self.channel_id, FLAG_DATA, data)
        return self.send_large(data, chunk_size=max_payload)

    def send_large(self, data: bytes,
                   chunk_size: int = USB_BUF_SIZE - HEADER_SIZE) -> bool:
        """Split large data into complete MUBD frames."""
        max_payload = USB_BUF_SIZE - HEADER_SIZE
        if chunk_size > max_payload:
            chunk_size = max_payload
        for i in range(0, len(data), chunk_size):
            if not self._mgr._send_frame(self.channel_id, FLAG_DATA,
                                         data[i:i + chunk_size]):
                return False
        return True

    def recv(self, timeout: float = 5.0) -> Optional[bytes]:
        """Blocking receive. Returns payload or None on timeout/close."""
        try:
            return self._rx_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def recv_nowait(self) -> Optional[bytes]:
        try:
            return self._rx_queue.get_nowait()
        except queue.Empty:
            return None

    def recv_all(self, total_bytes: int, timeout: float = 30.0) -> Optional[bytes]:
        """Accumulate exactly total_bytes (useful for benchmarks)."""
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while len(buf) < total_bytes:
            t = deadline - time.monotonic()
            if t <= 0:
                return None
            chunk = self.recv(timeout=t)
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
        self._rx_queue.put(None)

    def __enter__(self): return self
    def __exit__(self, *_): self.close()
    def __repr__(self):
        return f"<UsbChannel id={self.channel_id} open={self._open}>"


# ── UsbChannelManager ──────────────────────────────────────────────────────────
class UsbChannelManager:
    """
    Manages the USB connection and demultiplexes logical channels.

    Backend priority: usb1 > pyusb (auto-detected).

    usb1 path: submits NUM_ASYNC_RX bulk-IN transfers simultaneously so the
    USB pipe is never stalled waiting for Python to resubmit.  Each completion
    callback takes the fast path (single complete frame per read, common after
    the usb_send_frame combined-write fix) or falls through to a reassembly
    buffer for the fragmented case.

    pyusb path: one synchronous read at a time — works everywhere, slower.

    Example:
        mgr = UsbChannelManager()
        mgr.connect()
        ch = mgr.open_channel(0)
        ch.send(b"ping")
        print(ch.recv(timeout=2.0))
        mgr.close()
    """

    def __init__(self, vid: int = DEFAULT_VID, pid: int = DEFAULT_PID,
                 interface: int = 0, num_async_rx: int = NUM_ASYNC_RX):
        self.vid          = vid
        self.pid          = pid
        self.iface_num    = interface
        self._num_async   = num_async_rx
        self._backend     = None  # 'usb1' | 'pyusb'

        # usb1 state
        self._usb1_ctx    = None
        self._usb1_handle = None
        self._ep_in_addr  = 0
        self._ep_out_addr = 0

        # pyusb state
        self._dev    = None
        self._ep_out = None
        self._ep_in  = None

        self._channels: Dict[int, UsbChannel] = {}
        self._ch_lock = threading.Lock()
        self._tx_lock = threading.Lock()

        # Reassembly buffer (fallback path).  _rx_pos is a read offset so we
        # never do an O(n) del-from-front on every frame — we advance the
        # pointer and compact once we've consumed ≥ USB_BUF_SIZE bytes.
        self._rx_buf  = bytearray()
        self._rx_pos  = 0

        self._rx_thread = None
        self._running   = False

    # ── connect ────────────────────────────────────────────────────────────────
    def connect(self, timeout: float = 10.0,
                backend: Optional[str] = None) -> bool:
        """
        Connect to the device.
        backend: 'usb1' (async multi-transfer), 'pyusb', or None (auto).
        """
        if backend is None:
            backend = 'usb1' if _HAVE_USB1 else 'pyusb'
        if backend == 'usb1' and not _HAVE_USB1:
            print("[usb] usb1 not installed (pip install libusb1) — "
                  "falling back to pyusb")
            backend = 'pyusb'
        if backend == 'pyusb' and not _HAVE_PYUSB:
            print("[usb] pyusb not available")
            return False
        self._backend = backend
        return (self._connect_usb1(timeout) if backend == 'usb1'
                else self._connect_pyusb(timeout))

    # ── usb1 connect ───────────────────────────────────────────────────────────
    def _connect_usb1(self, timeout: float) -> bool:
        ctx    = _usb1.USBContext()
        handle = None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                handle = ctx.openByVendorIDAndProductID(
                    self.vid, self.pid, skip_on_error=True)
            except Exception:
                handle = None
            if handle is not None:
                break
            time.sleep(0.5)

        if handle is None:
            print(f"[usb] device {self.vid:04x}:{self.pid:04x} not found")
            ctx.close()
            return False

        try:
            if sys.platform != 'win32':
                try:
                    if handle.kernelDriverActive(self.iface_num):
                        handle.detachKernelDriver(self.iface_num)
                except Exception:
                    pass
            handle.claimInterface(self.iface_num)
        except _usb1.USBError as e:
            print(f"[usb] usb1 init error: {e}")
            ctx.close()
            return False

        ep_in_addr = ep_out_addr = None
        for config in handle.getDevice():
            for iface in config:
                for setting in iface:
                    if setting.getNumber() != self.iface_num:
                        continue
                    for ep in setting:
                        addr = ep.getAddress()
                        if (ep.getAttributes() & 0x03) == 0x02:  # BULK
                            if addr & 0x80:
                                ep_in_addr = addr
                            else:
                                ep_out_addr = addr

        if ep_in_addr is None or ep_out_addr is None:
            print("[usb] could not find bulk endpoints")
            ctx.close()
            return False

        self._usb1_ctx    = ctx
        self._usb1_handle = handle
        self._ep_in_addr  = ep_in_addr
        self._ep_out_addr = ep_out_addr
        self._running     = True

        self._rx_thread = threading.Thread(
            target=self._rx_loop_usb1, name="usbd-rx", daemon=True)
        self._rx_thread.start()

        print(f"[usb] connected via usb1  "
              f"ep_out=0x{ep_out_addr:02x}  ep_in=0x{ep_in_addr:02x}  "
              f"({self._num_async} async RX × {USB_BUF_SIZE // 1024} KB)")
        return True

    # ── pyusb connect ──────────────────────────────────────────────────────────
    def _connect_pyusb(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            dev = usb.core.find(idVendor=self.vid, idProduct=self.pid)
            if dev is not None:
                self._dev = dev
                break
            time.sleep(0.5)
        if self._dev is None:
            print(f"[usb] device {self.vid:04x}:{self.pid:04x} not found")
            return False

        try:
            if sys.platform != 'win32':
                try:
                    if self._dev.is_kernel_driver_active(self.iface_num):
                        self._dev.detach_kernel_driver(self.iface_num)
                except (NotImplementedError, usb.core.USBError):
                    pass
            self._dev.set_configuration()
        except usb.core.USBError as e:
            print(f"[usb] config error: {e}")
            return False

        cfg   = self._dev.get_active_configuration()
        iface = cfg[(self.iface_num, 0)]
        self._ep_out = usb.util.find_descriptor(
            iface, custom_match=lambda e:
                usb.util.endpoint_direction(e.bEndpointAddress)
                == usb.util.ENDPOINT_OUT)
        self._ep_in  = usb.util.find_descriptor(
            iface, custom_match=lambda e:
                usb.util.endpoint_direction(e.bEndpointAddress)
                == usb.util.ENDPOINT_IN)
        if self._ep_out is None or self._ep_in is None:
            print("[usb] could not find bulk endpoints")
            return False

        self._running   = True
        self._rx_thread = threading.Thread(
            target=self._rx_loop_pyusb, name="usbd-rx", daemon=True)
        self._rx_thread.start()
        print(f"[usb] connected via pyusb  "
              f"ep_out=0x{self._ep_out.bEndpointAddress:02x}  "
              f"ep_in=0x{self._ep_in.bEndpointAddress:02x}")
        return True

    # ── close ──────────────────────────────────────────────────────────────────
    def close(self):
        self._running = False
        with self._ch_lock:
            for ch in list(self._channels.values()):
                ch._mark_closed()
            self._channels.clear()
        if self._usb1_handle:
            try:
                self._usb1_handle.releaseInterface(self.iface_num)
            except Exception:
                pass
            try:
                self._usb1_handle.close()
            except Exception:
                pass
            self._usb1_handle = None
        if self._usb1_ctx:
            try:
                self._usb1_ctx.close()
            except Exception:
                pass
            self._usb1_ctx = None
        if self._dev:
            usb.util.dispose_resources(self._dev)
            self._dev = None

    # ── channel management ─────────────────────────────────────────────────────
    def open_channel(self, channel_id: int = 0) -> UsbChannel:
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

    # ── send ───────────────────────────────────────────────────────────────────
    def _send_frame(self, channel: int, flags: int,
                    payload: bytes = b"") -> bool:
        frame = encode_frame(channel, flags, payload)
        if len(frame) > USB_BUF_SIZE:
            print(f"[usb] frame too large ({len(frame)} B) — "
                  f"use send_large() for payloads > {USB_BUF_SIZE - HEADER_SIZE} B")
            return False
        with self._tx_lock:
            return (self._write_usb1(frame) if self._backend == 'usb1'
                    else self._write_pyusb(frame))

    def _write_usb1(self, frame: bytes) -> bool:
        try:
            self._usb1_handle.bulkWrite(self._ep_out_addr, frame, timeout=5000)
            return True
        except _usb1.USBError as e:
            print(f"[usb] send error: {e}")
            return False

    def _write_pyusb(self, frame: bytes) -> bool:
        try:
            self._ep_out.write(frame, timeout=5000)
            return True
        except usb.core.USBError as e:
            print(f"[usb] send error: {e}")
            return False

    # ── usb1 async RX (primary path) ───────────────────────────────────────────
    def _rx_loop_usb1(self):
        """
        Keep self._num_async bulk-IN transfers permanently in-flight.

        Fast path: because usb_send_frame on the device now emits header +
        payload as a single write(), each bulk read normally returns one
        complete MUBD frame.  We parse the header directly from the transfer
        buffer and dispatch without touching the reassembly bytearray.

        Fallback path: if a read returns a partial frame (reconnect, first
        read after ZLP, etc.) we hand the bytes to _feed_rx() which uses the
        offset-based reassembly buffer.
        """
        transfers = []

        def _on_rx(transfer):
            status = transfer.getStatus()
            if status == _usb1.TRANSFER_COMPLETED:
                n = transfer.getActualLength()
                if n >= HEADER_SIZE:
                    raw = transfer.getBuffer()
                    magic, ch, flags, _, plen = struct.unpack_from(
                        HEADER_FMT, raw, 0)
                    if (magic == FRAME_MAGIC
                            and n == HEADER_SIZE + plen
                            and plen <= MAX_FRAME_SIZE):
                        # Fast path — complete frame, no reassembly needed
                        self._dispatch(ch, flags, bytes(raw[HEADER_SIZE:n]))
                        if self._running:
                            try:
                                transfer.submit()
                            except _usb1.USBError:
                                pass
                        return
                    # Partial or malformed — hand off to reassembly
                    if n > 0:
                        self._feed_rx(bytes(raw[:n]))
            elif status not in (_usb1.TRANSFER_TIMED_OUT,
                                 _usb1.TRANSFER_CANCELLED):
                if self._running:
                    print(f"[usb] rx transfer status {status}")
            if self._running:
                try:
                    transfer.submit()
                except _usb1.USBError:
                    pass

        for _ in range(self._num_async):
            t = self._usb1_handle.getTransfer()
            t.setBulk(self._ep_in_addr, USB_BUF_SIZE,
                      callback=_on_rx, timeout=0)
            t.submit()
            transfers.append(t)

        while self._running and self._usb1_ctx:
            try:
                self._usb1_ctx.handleEventsTimeout(0.05)
            except Exception as e:
                if self._running:
                    print(f"[usb] handleEvents error: {e}")
                break

        for t in transfers:
            try:
                t.cancel()
            except Exception:
                pass
        # Drain so cancellations complete before we return
        for _ in range(20):
            try:
                self._usb1_ctx.handleEventsTimeout(0.02)
            except Exception:
                break

    # ── pyusb synchronous RX (fallback) ───────────────────────────────────────
    def _rx_loop_pyusb(self):
        while self._running:
            try:
                raw = self._ep_in.read(USB_BUF_SIZE, timeout=1000)
                if raw:
                    self._feed_rx(bytes(raw))
            except usb.core.USBTimeoutError:
                continue
            except usb.core.USBError as e:
                if self._running:
                    print(f"[usb] rx error: {e}")
                    time.sleep(0.1)

    # ── reassembly (fallback for both backends) ────────────────────────────────
    def _feed_rx(self, data: bytes):
        self._rx_buf.extend(data)
        self._parse_rx()

    def _parse_rx(self):
        """
        Parse complete frames from _rx_buf starting at _rx_pos.
        Advances _rx_pos instead of del-from-front to avoid O(n) shifts;
        compacts the buffer once _rx_pos crosses USB_BUF_SIZE.
        """
        buf = self._rx_buf
        pos = self._rx_pos

        while True:
            avail = len(buf) - pos
            if avail < HEADER_SIZE:
                break

            magic, channel, flags, _, plen = struct.unpack_from(
                HEADER_FMT, buf, pos)

            if magic != FRAME_MAGIC:
                needle = struct.pack("<I", FRAME_MAGIC)
                idx    = buf.find(needle, pos + 1)
                if idx < 0:
                    pos = len(buf)
                    break
                print("[usb] rx resync")
                pos = idx
                continue

            if plen > MAX_FRAME_SIZE:
                print(f"[usb] oversized frame plen={plen}, dropping")
                pos = len(buf)
                break

            total = HEADER_SIZE + plen
            if avail < total:
                break

            self._dispatch(channel, flags,
                           bytes(buf[pos + HEADER_SIZE: pos + total]))
            pos += total

        self._rx_pos = pos
        # Compact once we've consumed at least one full buffer's worth
        if self._rx_pos >= USB_BUF_SIZE:
            del self._rx_buf[:self._rx_pos]
            self._rx_pos = 0

    # ── dispatch ───────────────────────────────────────────────────────────────
    def _dispatch(self, channel: int, flags: int, payload: bytes):
        with self._ch_lock:
            ch = self._channels.get(channel)

        if flags == FLAG_OPEN:
            if ch is None:
                with self._ch_lock:
                    ch = UsbChannel(channel, self)
                    self._channels[channel] = ch
                print(f"[usb] channel {channel} opened by device")
        elif flags == FLAG_CLOSE:
            if ch:
                ch._mark_closed()
                with self._ch_lock:
                    self._channels.pop(channel, None)
            print(f"[usb] channel {channel} closed")
        elif flags == FLAG_DATA and ch and payload:
            ch._dispatch(payload)

    def __enter__(self): return self
    def __exit__(self, *_): self.close()


# ── benchmark ──────────────────────────────────────────────────────────────────
def run_benchmark(ch: UsbChannel, mb: float = 10.0,
                  chunk_kb: int = 512):
    """Send mb MB and measure throughput (device must echo data back)."""
    total_bytes = int(mb * 1024 * 1024)
    chunk_size  = chunk_kb * 1024
    payload     = (bytes(range(256)) * (chunk_size // 256 + 1))[:chunk_size]

    print(f"[bench] sending {mb:.1f} MB in {chunk_size // 1024} KB chunks ...")
    t0 = time.monotonic()
    sent = 0
    while sent < total_bytes:
        n = min(chunk_size, total_bytes - sent)
        if not ch.send(payload[:n]):
            print("[bench] send failed")
            return
        sent += n

    tx_time = time.monotonic() - t0
    print(f"[bench] TX {total_bytes/1024:.0f} KB  "
          f"{tx_time:.3f} s  {total_bytes/tx_time/1e6:.2f} MB/s")

    print("[bench] waiting for echo ...")
    t1       = time.monotonic()
    received = ch.recv_all(total_bytes, timeout=60.0)
    if received is None:
        print("[bench] recv timeout/error")
        return
    rx_time = time.monotonic() - t1
    print(f"[bench] RX {len(received)/1024:.0f} KB  "
          f"{rx_time:.3f} s  {len(received)/rx_time/1e6:.2f} MB/s")
    print(f"[bench] round-trip {time.monotonic()-t0:.3f} s")


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="PC-side USB channel client for usbd on Pi Zero 2W")
    parser.add_argument("--vid",     type=lambda x: int(x, 16),
                        default=DEFAULT_VID,
                        help=f"USB Vendor ID (default: {DEFAULT_VID:#06x})")
    parser.add_argument("--pid",     type=lambda x: int(x, 16),
                        default=DEFAULT_PID,
                        help=f"USB Product ID (default: {DEFAULT_PID:#06x})")
    parser.add_argument("--channel", type=int,   default=0)
    parser.add_argument("--backend", choices=["usb1", "pyusb"], default=None,
                        help="USB backend (default: usb1 if available)")
    parser.add_argument("--async-rx", type=int,  default=NUM_ASYNC_RX,
                        dest="async_rx",
                        help=f"concurrent RX transfers for usb1 (default: {NUM_ASYNC_RX})")
    parser.add_argument("--send",    type=str,   default=None)
    parser.add_argument("--bench",   type=float, default=None, metavar="MB",
                        help="throughput benchmark sending N MB")
    parser.add_argument("--listen",  action="store_true")
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    mgr = UsbChannelManager(vid=args.vid, pid=args.pid,
                            num_async_rx=args.async_rx)
    if not mgr.connect(timeout=args.timeout, backend=args.backend):
        sys.exit(1)

    ch = mgr.open_channel(args.channel)
    print(f"[cli] channel {ch.channel_id} open")

    try:
        if args.bench is not None:
            run_benchmark(ch, mb=args.bench)
        elif args.send is not None:
            ch.send(args.send.encode())
            print(f"[cli] sent {len(args.send)} bytes")
            reply = ch.recv(timeout=5.0)
            print(f"[cli] echo: {reply!r}" if reply else "[cli] no reply")
        elif args.listen:
            print("[cli] listening (Ctrl+C to stop) ...")
            while True:
                data = ch.recv(timeout=1.0)
                if data:
                    print(f"[cli] {len(data)} B: "
                          f"{data[:64]!r}{'...' if len(data) > 64 else ''}")
        else:
            print("[cli] interactive mode. Empty line to quit.")
            while True:
                try:
                    line = input("send> ")
                except EOFError:
                    break
                if not line:
                    break
                ch.send(line.encode())
                reply = ch.recv(timeout=3.0)
                print(f"recv: {reply.decode(errors='replace')!r}" if reply
                      else "(no reply)")
    except KeyboardInterrupt:
        print("\n[cli] interrupted")
    finally:
        ch.close()
        mgr.close()


if __name__ == "__main__":
    main()
