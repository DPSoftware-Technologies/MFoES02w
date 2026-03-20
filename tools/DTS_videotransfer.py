import usb_channel as mfoes02wusb
import cv2
import numpy as np
import time
import struct

MSG_TILE_UPDATE = 0xF5  # single tile update with position

SCREEN_W, SCREEN_H = 1280, 720
TILES_X, TILES_Y   = 16, 9   # 144 tiles of 80×80px each
TILE_W = SCREEN_W // TILES_X  # 80
TILE_H = SCREEN_H // TILES_Y  # 80
DIFF_THRESHOLD = 1.5  # % changed pixels to trigger update
TARGET_FPS = 30

# Tile packet: [1B type][1B tx][1B ty][2B tw][2B th][pixels]
TILE_HDR_FMT  = '<BBBHH'
TILE_HDR_SIZE = struct.calcsize(TILE_HDR_FMT)  # 7 bytes

mgr = mfoes02wusb.UsbChannelManager(vid=0x750C, pid=0x0544)
print("connecting to MFoES02w...")
if not mgr.connect(timeout=30):
    exit(1)
ch = mgr.open_channel(0)

def tile_diff(t1, t2):
    if t1 is None: return 100.0
    diff = cv2.absdiff(t1, t2)
    return (np.count_nonzero(diff > 20) / t1.size) * 100

def tile_to_rgb565(tile):
    b, g, r = cv2.split(tile)
    return (
        (r.astype(np.uint16) & 0xF8) << 8 |
        (g.astype(np.uint16) & 0xFC) << 3 |
        (b.astype(np.uint16) >> 3)
    ).astype(np.uint16)

def send_tile(ch, tx, ty, tile_rgb565):
    header = struct.pack(TILE_HDR_FMT,
        MSG_TILE_UPDATE, tx, ty, TILE_W, TILE_H)
    ch.send(header + tile_rgb565.tobytes())

# Per-tile cache
tile_cache = [None] * (TILES_X * TILES_Y)

cap = cv2.VideoCapture(r"video.mp4")
src_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
frame_skip = max(1, round(src_fps / TARGET_FPS))
frame_count = 0

# Reset cache every 2 seconds to prevent drift
CACHE_RESET_FRAMES = int(src_fps * 2)

while True:
    ret, frame = cap.read()
    if not ret:
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        frame_count = 0
        tile_cache = [None] * (TILES_X * TILES_Y)
        continue

    frame_count += 1
    if frame_count % frame_skip != 0:
        continue

    # Periodic full refresh
    if frame_count % CACHE_RESET_FRAMES == 0:
        tile_cache = [None] * (TILES_X * TILES_Y)

    frame = cv2.resize(frame, (SCREEN_W, SCREEN_H))
    t0 = time.monotonic()

    sent = 0
    skipped = 0

    for ty in range(TILES_Y):
        for tx in range(TILES_X):
            idx = ty * TILES_X + tx
            x0, y0 = tx * TILE_W, ty * TILE_H
            tile = frame[y0:y0+TILE_H, x0:x0+TILE_W]

            if tile_diff(tile_cache[idx], tile) < DIFF_THRESHOLD:
                skipped += 1
                continue  # no change — skip this tile

            tile_cache[idx] = tile.copy()
            send_tile(ch, tx, ty, tile_to_rgb565(tile))
            sent += 1

    elapsed = time.monotonic() - t0
    sleep_time = (1.0 / TARGET_FPS) - elapsed
    if sleep_time > 0:
        time.sleep(sleep_time)
    else:
        print(f"[warn] {elapsed*1000:.0f}ms behind by {-sleep_time*1000:.0f}ms")

    print(f"sent={sent}/{TILES_X*TILES_Y} skipped={skipped} "
          f"bw={sent*(TILE_W*TILE_H*2)/1e6:.2f}MB t={elapsed*1000:.0f}ms")