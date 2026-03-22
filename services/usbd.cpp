#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <signal.h>
#include <linux/usb/functionfs.h>
#include <sys/ioctl.h> 
#include <poll.h>


// 
// Configuration
// 
#define FFS_PATH        "/dev/ffs-mfoes"
#define SOCK_PATH       "/var/run/usbd.sock"
#define MAX_CHANNELS    32
#define MAX_FRAME_SIZE  (4 * 1024 * 1024)  // 4MB limit — just a check, no allocation
#define USB_BUF_SIZE    (64 * 1024)         // 64KB — stack buffer, MUST stay small
#define RING_SIZE       (16 * 1024 * 1024)  // 16MB — heap allocated, fine

#define FRAME_MAGIC     0x4D554244u    // "MUBD"
#define FLAG_DATA       0x00
#define FLAG_OPEN       0x01
#define FLAG_CLOSE      0x02
#define FLAG_ACK        0x03
#define FLAG_ERROR      0x04

// 
// Frame header (12 bytes, packed)
// 
struct __attribute__((packed)) FrameHeader {
    uint32_t magic;      // FRAME_MAGIC
    uint8_t  channel;    // channel id
    uint8_t  flags;      // FLAG_*
    uint16_t reserved;
    uint32_t length;     // payload length (little-endian)
};

static_assert(sizeof(FrameHeader) == 12, "FrameHeader must be 12 bytes");

// 
// Ring buffer (lock-free SPSC)
// 
struct RingBuffer {
    uint8_t  *buf;
    uint32_t  cap;
    volatile uint32_t head; // writer
    volatile uint32_t tail; // reader
    pthread_mutex_t lock;
    pthread_cond_t  cond;

    void init(uint32_t capacity) {
        cap  = capacity;
        buf  = (uint8_t*)malloc(capacity);
        head = tail = 0;
        pthread_mutex_init(&lock, nullptr);
        pthread_cond_init(&cond, nullptr);
    }
    void destroy() {
        free(buf);
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }
    uint32_t used() { return head - tail; }
    uint32_t free_space() { return cap - used(); }

    // Returns bytes written (may be less than len if full)
    uint32_t write(const uint8_t *data, uint32_t len) {
        pthread_mutex_lock(&lock);
        uint32_t avail = cap - (head - tail);
        if (avail < len) len = avail;
        uint32_t pos = head % cap;
        uint32_t end = pos + len;
        if (end <= cap) {
            memcpy(buf + pos, data, len);
        } else {
            uint32_t first = cap - pos;
            memcpy(buf + pos, data, first);
            memcpy(buf, data + first, len - first);
        }
        head += len;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
        return len;
    }

    // Blocking read of exactly 'len' bytes
    bool read_exact(uint8_t *out, uint32_t len, int timeout_ms = -1) {
        pthread_mutex_lock(&lock);
        while ((head - tail) < len) {
            if (timeout_ms == 0) { pthread_mutex_unlock(&lock); return false; }
            if (timeout_ms > 0) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += timeout_ms * 1000000LL;
                ts.tv_sec  += ts.tv_nsec / 1000000000LL;
                ts.tv_nsec %= 1000000000LL;
                if (pthread_cond_timedwait(&cond, &lock, &ts) == ETIMEDOUT) {
                    pthread_mutex_unlock(&lock);
                    return false;
                }
            } else {
                pthread_cond_wait(&cond, &lock);
            }
        }
        uint32_t pos = tail % cap;
        uint32_t end = pos + len;
        if (end <= cap) {
            memcpy(out, buf + pos, len);
        } else {
            uint32_t first = cap - pos;
            memcpy(out, buf + pos, first);
            memcpy(out + first, buf, len - first);
        }
        tail += len;
        pthread_mutex_unlock(&lock);
        return true;
    }
};

// 
// Channel
// 
struct Channel {
    int      sock_fd;       // unix socket to app
    uint8_t  id;
    bool     active;
    RingBuffer rx_ring;     // USB→App
    RingBuffer tx_ring;     // App→USB
    pthread_mutex_t sock_lock; // protect sock writes

    void init(uint8_t cid, int fd) {
        id      = cid;
        sock_fd = fd;
        active  = true;
        rx_ring.init(RING_SIZE);
        tx_ring.init(RING_SIZE);
        pthread_mutex_init(&sock_lock, nullptr);
    }
    void destroy() {
        active = false;
        rx_ring.destroy();
        tx_ring.destroy();
        pthread_mutex_destroy(&sock_lock);
        if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
    }
};

// 
// Globals
// 
static Channel  channels[MAX_CHANNELS];
static pthread_mutex_t channels_lock = PTHREAD_MUTEX_INITIALIZER;
static int ep0_fd = -1, ep1_fd = -1, ep2_fd = -1;
static volatile bool g_running = true;
static int g_epoll_fd = -1;

// 
// USB Descriptors (FS + HS, bulk OUT ep1 + bulk IN ep2)
// 
static const struct {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio bulk_out;
        struct usb_endpoint_descriptor_no_audio bulk_in;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) descriptors = {
    .header = {
        .magic  = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = htole32(sizeof(descriptors)),
        .flags  = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
    },
    .fs_count = htole32(3),
    .hs_count = htole32(3),
    .fs_descs = {
        .intf = {
            .bLength            = sizeof(descriptors.fs_descs.intf),
            .bDescriptorType    = USB_DT_INTERFACE,
            .bNumEndpoints      = 2,
            .bInterfaceClass    = USB_CLASS_VENDOR_SPEC,
            .iInterface         = 1,
        },
        .bulk_out = {
            .bLength          = sizeof(descriptors.fs_descs.bulk_out),
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes     = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize   = htole16(64),
        },
        .bulk_in = {
            .bLength          = sizeof(descriptors.fs_descs.bulk_in),
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes     = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize   = htole16(64),
        },
    },
    .hs_descs = {
        .intf = {
            .bLength            = sizeof(descriptors.hs_descs.intf),
            .bDescriptorType    = USB_DT_INTERFACE,
            .bNumEndpoints      = 2,
            .bInterfaceClass    = USB_CLASS_VENDOR_SPEC,
            .iInterface         = 1,
        },
        .bulk_out = {
            .bLength          = sizeof(descriptors.hs_descs.bulk_out),
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes     = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize   = htole16(512),
        },
        .bulk_in = {
            .bLength          = sizeof(descriptors.hs_descs.bulk_in),
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes     = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize   = htole16(512),
        },
    },
};

static const struct {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        const char str1[sizeof("MFOES-MUX")];
    } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
    .header = {
        .magic      = htole32(FUNCTIONFS_STRINGS_MAGIC),
        .length     = htole32(sizeof(strings)),
        .str_count  = htole32(1),
        .lang_count = htole32(1),
    },
    .lang0 = { htole16(0x0409), "MFOES-MUX" },
};

// 
// Helpers: write_all / read_all (retry on EINTR)
// 
static ssize_t write_all(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t r = write(fd, (const uint8_t*)buf + sent, len - sent);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += r;
    }
    return (ssize_t)sent;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t r = read(fd, (uint8_t*)buf + got, len - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == 0 || errno == EAGAIN) {
                usleep(1000); // wait for next USB transfer
                continue;
            }
            return -1;
        }
        if (r == 0) {
            usleep(100);
            continue;
        }
        got += r;
    }
    return (ssize_t)got;
}

// 
// Send a frame over USB (ep2 = bulk IN)
// Only one writer thread should call this — guarded by caller
// 
static pthread_mutex_t usb_tx_lock = PTHREAD_MUTEX_INITIALIZER;

static bool usb_send_frame(uint8_t channel, uint8_t flags,
                           const uint8_t *payload, uint32_t len) {
    FrameHeader hdr;
    hdr.magic    = htole32(FRAME_MAGIC);
    hdr.channel  = channel;
    hdr.flags    = flags;
    hdr.reserved = 0;
    hdr.length   = htole32(len);

    pthread_mutex_lock(&usb_tx_lock);
    bool ok = (write_all(ep2_fd, &hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));
    if (ok && len > 0)
        ok = (write_all(ep2_fd, payload, len) == (ssize_t)len);
    pthread_mutex_unlock(&usb_tx_lock);
    return ok;
}

// 
// Per-channel TX thread: drains tx_ring → USB
// 
struct TxThreadArg { uint8_t channel_id; };

static void *channel_tx_thread(void *arg) {
    uint8_t cid = ((TxThreadArg*)arg)->channel_id;
    delete (TxThreadArg*)arg;
    Channel &ch = channels[cid];

    uint8_t txbuf[USB_BUF_SIZE];

    while (g_running && ch.active) {
        // Wait for data in tx_ring
        pthread_mutex_lock(&ch.tx_ring.lock);
        while (ch.active && g_running && ch.tx_ring.used() == 0)
            pthread_cond_wait(&ch.tx_ring.cond, &ch.tx_ring.lock);
        uint32_t avail = ch.tx_ring.used();
        pthread_mutex_unlock(&ch.tx_ring.lock);

        if (avail == 0) continue;

        // Read up to USB_BUF_SIZE from ring
        uint32_t chunk = avail < USB_BUF_SIZE ? avail : USB_BUF_SIZE;
        if (!ch.tx_ring.read_exact(txbuf, chunk, 100)) continue;

		fprintf(stderr, "[ch%d] tx->usb chunk=%u\n", cid, chunk);  // ← ADD HERE
        if (!usb_send_frame(cid, FLAG_DATA, txbuf, chunk)) {
            fprintf(stderr, "[ch%d] USB TX error\n", cid);
            break;
        }
    }

    return nullptr;
}

// 
// Per-channel RX dispatch: rx_ring → unix socket
// 
struct RxThreadArg { uint8_t channel_id; };

static void *channel_rx_thread(void *arg) {
    uint8_t cid = ((RxThreadArg*)arg)->channel_id;
    delete (RxThreadArg*)arg;
    Channel &ch = channels[cid];

    // Heap allocated — large frames won't fit on stack
    uint8_t *rxbuf = (uint8_t*)malloc(MAX_FRAME_SIZE);
    if (!rxbuf) {
        fprintf(stderr, "[ch%d] malloc rxbuf failed\n", cid);
        return nullptr;
    }

    while (g_running && ch.active) {
        pthread_mutex_lock(&ch.rx_ring.lock);
        while (ch.active && g_running && ch.rx_ring.used() < 4)
            pthread_cond_wait(&ch.rx_ring.cond, &ch.rx_ring.lock);
        pthread_mutex_unlock(&ch.rx_ring.lock);

        uint32_t plen = 0;
        if (!ch.rx_ring.read_exact((uint8_t*)&plen, 4, 200)) continue;
        plen = le32toh(plen);

        if (plen == 0 || plen > (uint32_t)MAX_FRAME_SIZE) {
            fprintf(stderr, "[ch%d] bad rx plen=%u, draining\n", cid, plen);
            // Drain so ring doesn't get stuck
            uint8_t trash[256];
            uint32_t skip = plen;
            while (skip > 0 && ch.active) {
                uint32_t n = skip < sizeof(trash) ? skip : sizeof(trash);
                if (!ch.rx_ring.read_exact(trash, n, 100)) break;
                skip -= n;
            }
            continue;
        }

        if (!ch.rx_ring.read_exact(rxbuf, plen, 5000)) {
            fprintf(stderr, "[ch%d] rx_ring read timeout plen=%u\n", cid, plen);
            continue;
        }

        fprintf(stderr, "[ch%d] usb->app plen=%u\n", cid, plen);

        // Forward to app socket: [4B len][data]
        pthread_mutex_lock(&ch.sock_lock);
        uint32_t net_len = htole32(plen);
        write_all(ch.sock_fd, &net_len, 4);
        write_all(ch.sock_fd, rxbuf, plen);
        pthread_mutex_unlock(&ch.sock_lock);
    }

    free(rxbuf);
    return nullptr;
}

// 
// App socket reader: read from unix socket → tx_ring
// 
struct AppReaderArg { uint8_t channel_id; };

static void *app_reader_thread(void *arg) {
    uint8_t cid = ((AppReaderArg*)arg)->channel_id;
    delete (AppReaderArg*)arg;
    Channel &ch = channels[cid];

    uint8_t buf[USB_BUF_SIZE];

    while (g_running && ch.active) {
        // App sends [4B len][data]
        uint32_t plen = 0;
        if (read_all(ch.sock_fd, &plen, 4) != 4) break;
        plen = le32toh(plen);
		fprintf(stderr, "[ch%d] app->tx plen=%u\n", cid, plen);  // ← ADD HERE
        if (plen == 0 || plen > MAX_FRAME_SIZE) break;
		

        uint32_t remaining = plen;
        while (remaining > 0) {
            uint32_t chunk = remaining < USB_BUF_SIZE ? remaining : USB_BUF_SIZE;
            if (read_all(ch.sock_fd, buf, chunk) != (ssize_t)chunk) goto done;
            // Write to tx_ring (may block if full)
            uint32_t written = 0;
            while (written < chunk && ch.active && g_running) {
                written += ch.tx_ring.write(buf + written, chunk - written);
                if (written < chunk) usleep(100);
            }
            remaining -= chunk;
        }
    }
done:
    fprintf(stderr, "[ch%d] app disconnected\n", cid);
    ch.active = false;
    usb_send_frame(cid, FLAG_CLOSE, nullptr, 0);
    return nullptr;
}

// 
// USB RX thread: reads from ep1, dispatches frames to channel rx_rings
// 
static void *usb_rx_thread(void *) {
    fprintf(stderr, "[usb_rx] thread started\n");

    // Reassembly buffer on heap — 4MB for large frames
    const size_t REASSEMBLY_MAX = MAX_FRAME_SIZE + sizeof(FrameHeader);
    uint8_t *reassembly = (uint8_t*)malloc(REASSEMBLY_MAX);
    if (!reassembly) { perror("malloc reassembly"); return nullptr; }

    // Separate small read buffer — kernel allocates DMA for THIS size only
    const size_t READ_CHUNK = 512 * 1024;  // 512KB max per read() call
    uint8_t *readbuf = (uint8_t*)malloc(READ_CHUNK);
    if (!readbuf) { perror("malloc readbuf"); free(reassembly); return nullptr; }

    size_t reassembly_len = 0;

    while (g_running) {
        int efd = ep1_fd;
        if (efd < 0) { usleep(50000); continue; }

        struct pollfd pfd = { efd, POLLIN, 0 };
        int p = poll(&pfd, 1, 500);
        if (p <= 0) continue;

        // Read into small readbuf — limits kernel DMA allocation
        ssize_t r = read(efd, readbuf, READ_CHUNK);
        if (r <= 0) { usleep(1000); continue; }

        // Copy into reassembly buffer
        if (reassembly_len + r > REASSEMBLY_MAX) {
            fprintf(stderr, "[usb_rx] reassembly overflow, resetting\n");
            reassembly_len = 0;
        }
        memcpy(reassembly + reassembly_len, readbuf, r);
        reassembly_len += r;
        fprintf(stderr, "[usb_rx] +%zd bytes total=%zu\n", r, reassembly_len);

        // Process complete frames
        size_t consumed = 0;
        while (consumed + sizeof(FrameHeader) <= reassembly_len) {
            FrameHeader *hdr = (FrameHeader*)(reassembly + consumed);
            if (le32toh(hdr->magic) != FRAME_MAGIC) {
                consumed++;
                continue;
            }
            uint32_t plen = le32toh(hdr->length);
            size_t total_needed = sizeof(FrameHeader) + plen;
            if (consumed + total_needed > reassembly_len) break;

            uint8_t  cid     = hdr->channel;
            uint8_t  flag    = hdr->flags;
            uint8_t *payload = reassembly + consumed + sizeof(FrameHeader);

            fprintf(stderr, "[usb_rx] frame ch=%d flags=%d plen=%u\n",
                    cid, flag, plen);

            pthread_mutex_lock(&channels_lock);
            bool active = channels[cid].active;
            pthread_mutex_unlock(&channels_lock);

            if (flag == FLAG_DATA && active && plen > 0) {
                uint32_t net = htole32(plen);
                channels[cid].rx_ring.write((uint8_t*)&net, 4);
                channels[cid].rx_ring.write(payload, plen);
            } else if (flag == FLAG_OPEN) {
                usb_send_frame(cid, FLAG_ACK, nullptr, 0);
            } else if (flag == FLAG_CLOSE && active) {
                channels[cid].active = false;
            }

            consumed += total_needed;
        }

        if (consumed > 0) {
            reassembly_len -= consumed;
            if (reassembly_len > 0)
                memmove(reassembly, reassembly + consumed, reassembly_len);
        }
    }

    free(reassembly);
    free(readbuf);
    return nullptr;
}

// 
// IPC accept thread: accepts new app connections on unix socket
// 
static void *ipc_accept_thread(void *arg) {
    int srv = *(int*)arg;

    while (g_running) {
        int fd = accept(srv, nullptr, nullptr);
        if (fd < 0) {
            if (!g_running) break;
            perror("accept");
            continue;
        }

        // App sends 1 byte: requested channel id (0xFF = auto-assign)
        uint8_t req_cid = 0xFF;
        read(fd, &req_cid, 1);

        pthread_mutex_lock(&channels_lock);
        int assigned = -1;

        if (req_cid == 0xFF) {
            for (int i = 0; i < MAX_CHANNELS; i++) {
                if (!channels[i].active) { assigned = i; break; }
            }
        } else if (req_cid < MAX_CHANNELS && !channels[req_cid].active) {
            assigned = req_cid;
        }

        if (assigned < 0) {
            pthread_mutex_unlock(&channels_lock);
            uint8_t err = 0xFF;
            write(fd, &err, 1);
            close(fd);
            fprintf(stderr, "[ipc] no channel available\n");
            continue;
        }

        channels[assigned].init((uint8_t)assigned, fd);
        pthread_mutex_unlock(&channels_lock);

        // Respond with assigned channel id
        uint8_t resp = (uint8_t)assigned;
        write(fd, &resp, 1);

        // Notify PC side
        usb_send_frame((uint8_t)assigned, FLAG_OPEN, nullptr, 0);

        fprintf(stderr, "[ipc] app connected on channel %d\n", assigned);

        // Spawn per-channel threads
        pthread_t t;
        auto *ta = new TxThreadArg{(uint8_t)assigned};
        pthread_create(&t, nullptr, channel_tx_thread, ta);
        pthread_detach(t);

        auto *ra = new RxThreadArg{(uint8_t)assigned};
        pthread_create(&t, nullptr, channel_rx_thread, ra);
        pthread_detach(t);

        auto *aa = new AppReaderArg{(uint8_t)assigned};
        pthread_create(&t, nullptr, app_reader_thread, aa);
        pthread_detach(t);
    }
    return nullptr;
}

// 
// ep0 event thread
// 
static void *ep0_thread(void *) {
    struct usb_functionfs_event ev;
    while (g_running) {
        ssize_t r = read(ep0_fd, &ev, sizeof(ev));
        if (r < 0) {
            if (!g_running) break;
            perror("ep0 read");
            usleep(10000);
            continue;
        }
        switch (ev.type) {
            case FUNCTIONFS_DISABLE:
				fprintf(stderr, "[ep0] DISABLE\n");
				// Host disconnected — close bulk endpoints
				if (ep1_fd >= 0) { close(ep1_fd); ep1_fd = -1; }
				if (ep2_fd >= 0) { close(ep2_fd); ep2_fd = -1; }
				break;
			case FUNCTIONFS_ENABLE:
                ep1_fd = open(FFS_PATH "/ep1", O_RDONLY | O_NONBLOCK);
                ep2_fd = open(FFS_PATH "/ep2", O_WRONLY | O_NONBLOCK);
                // Clear halts
                ioctl(ep1_fd, FUNCTIONFS_CLEAR_HALT);
                ioctl(ep2_fd, FUNCTIONFS_CLEAR_HALT);
                // Switch to blocking
                int fl;
                fl = fcntl(ep1_fd, F_GETFL); fcntl(ep1_fd, F_SETFL, fl & ~O_NONBLOCK);
                fl = fcntl(ep2_fd, F_GETFL); fcntl(ep2_fd, F_SETFL, fl & ~O_NONBLOCK);
                fprintf(stderr, "[ep0] endpoints reopened ep1=%d ep2=%d\n", ep1_fd, ep2_fd);
                break;
            case FUNCTIONFS_SETUP:
                // Stall unhandled control requests
                write(ep0_fd, nullptr, 0);
                break;
            default:
                fprintf(stderr, "[ep0] event %d\n", ev.type);
                break;
        }
    }
    return nullptr;
}

static void sig_handler(int) { g_running = false; }

// 
// main
// 
int main(int argc, char *argv[]) {
    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "[usbd] starting\n");

    //  Init channel array 
    memset(channels, 0, sizeof(channels));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].active  = false;
        channels[i].sock_fd = -1;
    }

    //  Open FunctionFS 
    ep0_fd = open(FFS_PATH "/ep0", O_RDWR);
    if (ep0_fd < 0) { perror("open ep0"); return 1; }

    if (write_all(ep0_fd, &descriptors, sizeof(descriptors)) < 0) {
        perror("write descriptors"); return 1;
    }
    if (write_all(ep0_fd, &strings, sizeof(strings)) < 0) {
        perror("write strings"); return 1;
    }
    fprintf(stderr, "[usbd] FFS descriptors written\n");

    // Wait for ENABLE
    fprintf(stderr, "[usbd] waiting for host...\n");
    struct usb_functionfs_event ev;
    while (1) {
        ssize_t r = read(ep0_fd, &ev, sizeof(ev));
        if (r < 0) { perror("read ep0"); return 1; }
        fprintf(stderr, "[usbd] ep0 event %d\n", ev.type);
        if (ev.type == FUNCTIONFS_ENABLE) break;
    }
	
	// After the ENABLE wait loop in main(), before opening ep1/ep2:
	fprintf(stderr, "[usbd] host enabled, waiting for bulk ready...\n");
	usleep(100000); // 100ms — let host finish enumeration

	// After opening ep1 in main():
    ep1_fd = open(FFS_PATH "/ep1", O_RDONLY | O_NONBLOCK);
    if (ep1_fd < 0) { perror("open ep1"); return 1; }

    // Clear halt on ep1
    if (ioctl(ep1_fd, FUNCTIONFS_CLEAR_HALT) < 0) {
        fprintf(stderr, "[usbd] CLEAR_HALT ep1: %s (may be ok)\n", strerror(errno));
    }

    ep2_fd = open(FFS_PATH "/ep2", O_WRONLY | O_NONBLOCK);
    if (ep2_fd < 0) { perror("open ep2"); return 1; }

    // Immediately switch BOTH to blocking
    int fl;
    fl = fcntl(ep1_fd, F_GETFL); fcntl(ep1_fd, F_SETFL, fl & ~O_NONBLOCK);
    fl = fcntl(ep2_fd, F_GETFL); fcntl(ep2_fd, F_SETFL, fl & ~O_NONBLOCK);
    fprintf(stderr, "[usbd] endpoints open (blocking)\n");

    //  Unix socket 
    unlink(SOCK_PATH);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    chmod(SOCK_PATH, 0666);
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }
    fprintf(stderr, "[usbd] IPC socket ready at %s\n", SOCK_PATH);

    //  Spawn threads 
    pthread_t t;

    pthread_create(&t, nullptr, ep0_thread, nullptr); pthread_detach(t);
    pthread_create(&t, nullptr, usb_rx_thread, nullptr); pthread_detach(t);
    pthread_create(&t, nullptr, ipc_accept_thread, &srv); pthread_detach(t);

    fprintf(stderr, "[usbd] running (max %d channels)\n", MAX_CHANNELS);

    // Main thread: just waits
    while (g_running) sleep(1);

    fprintf(stderr, "[usbd] shutting down\n");
    g_running = false;

    close(srv);
    close(ep1_fd);
    close(ep2_fd);
    close(ep0_fd);
    unlink(SOCK_PATH);

    return 0;
}
