#include "hwinterface/usbd_client.h"
#include "libgfx/GFX.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string>
#include <vector>

// ============================================================
//  OTA Wire Protocol
//  All multi-byte fields are little-endian.
//
//  Packet layout:
//    [1B magic 0xOA] [1B type] [4B payload_len] [payload] [1B checksum xor]
//
//  OTA_BEGIN payload:
//    [4B total_parts] [8B total_size] [16B md5_full_zip] [null-term version string ≤32B]
//
//  OTA_CHUNK payload:
//    [4B part_number] [4B part_size] [16B md5_chunk] [part_size bytes data]
//
//  OTA_END payload: (empty)
//  OTA_ACK payload: [4B part_number_or_0_for_session]
//  OTA_NAK payload: [4B part_number_or_0] [null-term reason ≤64B]
//  OTA_ABORT payload: [null-term reason ≤64B]
// ============================================================

static const uint8_t OTA_MAGIC   = 0x0A;
static const uint8_t OTA_BEGIN   = 0x01;
static const uint8_t OTA_CHUNK   = 0x02;
static const uint8_t OTA_END     = 0x03;
static const uint8_t OTA_ACK     = 0x04;
static const uint8_t OTA_NAK     = 0x05;
static const uint8_t OTA_ABORT   = 0x06;

static const char*   OTA_STAGING = "/tmp/ota_staging";
static const char*   OTA_ZIP     = "/tmp/ota_staging/upload.zip";
static const int     OTA_CHANNEL = 10;
static const size_t  PKT_BUF     = 1024 * 1024 + 128; // 1MB chunk + header overhead

static bool running = true;

void signalHandler(int signum) {
    printf("[otad] Received signal %d, shutting down...\n", signum);
    running = false;
}

// ============================================================
//  Minimal MD5 implementation (RFC 1321)
//  No external dependencies.
// ============================================================

struct MD5_CTX_impl {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
};

static const uint32_t MD5_T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const uint8_t MD5_S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,
};

static inline uint32_t rotl32(uint32_t x, uint8_t n) {
    return (x << n) | (x >> (32 - n));
}

static void md5_init(MD5_CTX_impl* ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = ctx->count[1] = 0;
}

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i*4]
             | ((uint32_t)block[i*4+1] << 8)
             | ((uint32_t)block[i*4+2] << 16)
             | ((uint32_t)block[i*4+3] << 24);
    }
    for (int i = 0; i < 64; i++) {
        uint32_t F, g;
        if (i < 16) {
            F = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            F = (d & b) | (~d & c);
            g = (5*i + 1) % 16;
        } else if (i < 48) {
            F = b ^ c ^ d;
            g = (3*i + 5) % 16;
        } else {
            F = c ^ (b | ~d);
            g = (7*i) % 16;
        }
        F = F + a + MD5_T[i] + M[g];
        a = d;
        d = c;
        c = b;
        b = b + rotl32(F, MD5_S[i]);
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_update(MD5_CTX_impl* ctx, const uint8_t* data, size_t len) {
    uint32_t index = (ctx->count[0] >> 3) & 0x3F;

    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    uint32_t partLen = 64 - index;
    size_t i = 0;

    if (len >= partLen) {
        memcpy(ctx->buffer + index, data, partLen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            md5_transform(ctx->state, data + i);
        index = 0;
    }
    memcpy(ctx->buffer + index, data + i, len - i);
}

static void md5_final(MD5_CTX_impl* ctx, uint8_t digest[16]) {
    static const uint8_t PADDING[64] = { 0x80 };
    uint8_t bits[8];
    for (int i = 0; i < 4; i++) {
        bits[i]   = (ctx->count[0] >> (i * 8)) & 0xFF;
        bits[i+4] = (ctx->count[1] >> (i * 8)) & 0xFF;
    }
    uint32_t index  = (ctx->count[0] >> 3) & 0x3F;
    uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, PADDING, padLen);
    md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (ctx->state[i])       & 0xFF;
        digest[i*4+1] = (ctx->state[i] >>  8) & 0xFF;
        digest[i*4+2] = (ctx->state[i] >> 16) & 0xFF;
        digest[i*4+3] = (ctx->state[i] >> 24) & 0xFF;
    }
    memset(ctx, 0, sizeof(*ctx));
}

// ---- Packet helpers ----------------------------------------

struct OtaPacket {
    uint8_t  type;
    uint32_t payload_len;
    uint8_t* payload; // not owned
};

static uint8_t xor_checksum(const uint8_t* data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= data[i];
    return cs;
}

// Serialise a packet into buf. Returns total bytes written, or -1 if buf too small.
static ssize_t encode_packet(uint8_t* buf, size_t buf_size,
                              uint8_t type,
                              const uint8_t* payload, uint32_t payload_len) {
    size_t total = 1 + 1 + 4 + payload_len + 1;
    if (buf_size < total) return -1;

    size_t i = 0;
    buf[i++] = OTA_MAGIC;
    buf[i++] = type;
    memcpy(buf + i, &payload_len, 4); i += 4;
    if (payload && payload_len) { memcpy(buf + i, payload, payload_len); i += payload_len; }
    buf[i++] = xor_checksum(payload, payload_len);
    return (ssize_t)total;
}

// Wait for the socket to be readable, with retries. Returns false on error/disconnect/shutdown.
static bool wait_readable(UsbdClient& usbd) {
    while (running && usbd.is_connected()) {
        int pr = usbd.poll(2000);
        if (pr < 0) {
            printf("[otad] wait_readable: poll error %d\n", pr);
            return false;
        }
        if (pr > 0) return true;
        // pr == 0: timeout, loop and re-check is_connected()
    }
    printf("[otad] wait_readable: not connected or shutting down\n");
    return false;
}

// Receive one full OTA packet via UsbdClient's message-framed recv().
//
// IMPORTANT: UsbdClient::recv() is NOT a raw byte stream — it is a
// length-prefixed message protocol. Each call to recv() reads one
// complete message: it first reads a 4-byte little-endian length from
// the socket, then reads exactly that many bytes. You must never call
// recv() asking for partial data (e.g. "just the 6-byte header"), and
// you must never call recv() multiple times to reassemble a single OTA
// packet. The host Python tool sends each OTA packet as one usbd message,
// so one recv() call delivers the entire packet — magic, type, length,
// payload, and checksum — in one contiguous buffer.
//
// payload_buf must be at least PKT_BUF bytes (1 MB + 128).
static bool recv_packet(UsbdClient& usbd, OtaPacket& pkt, uint8_t* payload_buf, size_t payload_buf_size) {
    printf("[otad] recv_packet: waiting for message...\n");

    if (!wait_readable(usbd)) return false;

    // One recv() call delivers the entire OTA packet as sent by the host.
    ssize_t r = usbd.recv(payload_buf, (uint32_t)payload_buf_size);
    if (r <= 0) {
        printf("[otad] recv_packet: recv returned %zd\n", r);
        return false;
    }
    printf("[otad] recv_packet: got %zd bytes\n", r);

    // Minimum packet: magic(1) + type(1) + payload_len(4) + checksum(1) = 7 bytes
    if (r < 7) {
        printf("[otad] recv_packet: message too short (%zd bytes)\n", r);
        return false;
    }

    const uint8_t* msg = payload_buf;

    if (msg[0] != OTA_MAGIC) {
        printf("[otad] recv_packet: bad magic 0x%02X (expected 0x%02X)\n", msg[0], OTA_MAGIC);
        return false;
    }

    pkt.type = msg[1];
    uint32_t declared_payload_len = 0;
    memcpy(&declared_payload_len, msg + 2, 4);

    // Verify the message length matches what the header declares:
    // expected = magic(1) + type(1) + payload_len_field(4) + payload + checksum(1)
    size_t expected_total = 1 + 1 + 4 + declared_payload_len + 1;
    if ((size_t)r != expected_total) {
        printf("[otad] recv_packet: length mismatch — got %zd bytes, header says %zu\n",
               r, expected_total);
        return false;
    }

    // Verify checksum over the payload bytes
    const uint8_t* payload_start = msg + 6;
    uint8_t cs_recv = msg[r - 1];
    uint8_t cs_calc = xor_checksum(payload_start, declared_payload_len);
    if (cs_recv != cs_calc) {
        printf("[otad] recv_packet: checksum mismatch got=0x%02X expected=0x%02X\n",
               cs_recv, cs_calc);
        return false;
    }

    pkt.payload_len = declared_payload_len;
    // Point payload directly into the receive buffer (past the 6-byte header)
    pkt.payload = payload_buf + 6;

    printf("[otad] recv_packet: OK type=0x%02X payload_len=%u\n", pkt.type, pkt.payload_len);
    return true;
}

static bool send_ack(UsbdClient& usbd, uint8_t* tx_buf, uint32_t part) {
    uint8_t payload[4];
    memcpy(payload, &part, 4);
    ssize_t len = encode_packet(tx_buf, PKT_BUF, OTA_ACK, payload, 4);
    return len > 0 && usbd.send(tx_buf, len) >= 0;
}

static bool send_nak(UsbdClient& usbd, uint8_t* tx_buf, uint32_t part, const char* reason) {
    uint8_t payload[68] = {};
    memcpy(payload, &part, 4);
    strncpy((char*)payload + 4, reason, 63);
    ssize_t len = encode_packet(tx_buf, PKT_BUF, OTA_NAK, payload, 4 + (uint32_t)strlen(reason) + 1);
    return len > 0 && usbd.send(tx_buf, len) >= 0;
}

// ---- MD5 helpers -------------------------------------------

static void md5_of_buf(const uint8_t* data, size_t len, uint8_t out[16]) {
    MD5_CTX_impl ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, out);
}

static bool md5_of_file(const char* path, uint8_t out[16]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    MD5_CTX_impl ctx;
    md5_init(&ctx);
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        md5_update(&ctx, buf, n);
    fclose(f);
    md5_final(&ctx, out);
    return true;
}

static bool md5_eq(const uint8_t a[16], const uint8_t b[16]) {
    return memcmp(a, b, 16) == 0;
}

// ---- Staging directory -------------------------------------

static void cleanup_staging() {
    // simple recursive delete via shell — staging is always /tmp
    system("rm -rf /tmp/ota_staging");
}

static bool mkdir_p(const char* path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

// ---- Apply update ------------------------------------------
// Extracts zip and atomically replaces binaries.
// Returns true on success.

static bool apply_update(LinuxGFX& gfx) {
    printf("[otad] Extracting update...\n");

    // Use unzip; minizip would be cleaner but keeps deps minimal
    std::string cmd = std::string("unzip -o ") + OTA_ZIP + " -d /tmp/ota_extract/";
    int rc = system(cmd.c_str());
    if (rc != 0) {
        printf("[otad] unzip failed: %d\n", rc);
        return false;
    }

    printf("[otad] Installing binaries...\n");
    // Install each binary with atomic rename
    struct { const char* src; const char* dst; } bins[] = {
        { "/tmp/ota_extract/mfoes02w/mfoes02w", "/mfoes/mfoes02w" },
        { "/tmp/ota_extract/usbd",               "/mfoes/usbd"      },
        { "/tmp/ota_extract/otad",               "/mfoes/otad"      },
    };

    for (auto& b : bins) {
        if (access(b.src, F_OK) != 0) continue; // part not in this update
        char tmp_dst[256];
        snprintf(tmp_dst, sizeof(tmp_dst), "%s.new", b.dst);
        rename(b.src, tmp_dst);
        rename(tmp_dst, b.dst);
        chmod(b.dst, 0755);
        printf("[otad] Installed %s\n", b.dst);
    }

    // Install libs
    system("cp -r /tmp/ota_extract/mfoes02w/libs/. /mfoes/mfoes02w/libs/");

    system("rm -rf /tmp/ota_extract");
    return true;
}

// ---- OTA session handler -----------------------------------

static void run_ota_session(UsbdClient& usbd, LinuxGFX& gfx) {
    std::vector<uint8_t> rx_buf(PKT_BUF);
    std::vector<uint8_t> tx_buf(PKT_BUF);
    OtaPacket pkt;

    // ---- Wait for OTA_BEGIN ----
    printf("[otad] Waiting for OTA_BEGIN...\n");
    if (!recv_packet(usbd, pkt, rx_buf.data(), rx_buf.size())) {
        printf("[otad] recv_packet failed for OTA_BEGIN\n");
        return;
    }
    if (pkt.type != OTA_BEGIN) {
        printf("[otad] Expected OTA_BEGIN (0x%02X), got 0x%02X\n", OTA_BEGIN, pkt.type);
        return;
    }

    // Sanity check: BEGIN payload must be at least 4+8+16+1 = 29 bytes
    if (pkt.payload_len < 29) {
        printf("[otad] OTA_BEGIN payload too short: %u bytes\n", pkt.payload_len);
        send_nak(usbd, tx_buf.data(), 0, "begin payload too short");
        return;
    }

    uint32_t total_parts;
    uint64_t total_size;
    uint8_t  md5_full[16];
    char     version[33] = {};

    const uint8_t* p = pkt.payload;
    memcpy(&total_parts, p, 4);   p += 4;
    memcpy(&total_size,  p, 8);   p += 8;
    memcpy(md5_full,     p, 16);  p += 16;
    // Version string: however many bytes remain (up to 32 + null)
    uint32_t ver_len = pkt.payload_len - 28;
    if (ver_len > 32) ver_len = 32;
    memcpy(version, p, ver_len);
    version[32] = '\0';

    printf("[otad] OTA BEGIN: %u parts, %llu bytes, version='%s' (payload_len=%u)\n",
           total_parts, (unsigned long long)total_size, version, pkt.payload_len);

    if (total_parts == 0 || total_size == 0) {
        printf("[otad] OTA_BEGIN: invalid parts=%u size=%llu\n",
               total_parts, (unsigned long long)total_size);
        send_nak(usbd, tx_buf.data(), 0, "invalid begin params");
        return;
    }

    // Prepare staging
    cleanup_staging();
    if (!mkdir_p(OTA_STAGING)) {
        printf("[otad] Cannot create staging dir\n");
        send_nak(usbd, tx_buf.data(), 0, "staging mkdir failed");
        return;
    }

    // ACK begin
    printf("[otad] Sending ACK for BEGIN...\n");
    if (!send_ack(usbd, tx_buf.data(), 0)) {
        printf("[otad] Failed to send BEGIN ACK\n");
        cleanup_staging();
        return;
    }
    printf("[otad] BEGIN ACK sent OK\n");

    // ---- Receive chunks ----
    FILE* zip_out = fopen(OTA_ZIP, "wb");
    if (!zip_out) {
        send_nak(usbd, tx_buf.data(), 0, "cannot open staging zip");
        return;
    }

    uint32_t received_parts = 0;

    while (running && usbd.is_connected()) {
        if (!recv_packet(usbd, pkt, rx_buf.data(), rx_buf.size())) break;

        if (pkt.type == OTA_ABORT) {
            printf("[otad] Host aborted: %s\n", (char*)pkt.payload);
            fclose(zip_out);
            cleanup_staging();
            return;
        }

        if (pkt.type == OTA_END) {
            break;
        }

        if (pkt.type != OTA_CHUNK) {
            printf("[otad] Unexpected packet 0x%02X\n", pkt.type);
            continue;
        }

        uint32_t part_num, part_size;
        uint8_t  md5_chunk[16];
        const uint8_t* cp = pkt.payload;
        memcpy(&part_num,  cp, 4); cp += 4;
        memcpy(&part_size, cp, 4); cp += 4;
        memcpy(md5_chunk,  cp, 16); cp += 16;
        const uint8_t* chunk_data = cp;

        // Verify chunk MD5
        uint8_t computed[16];
        md5_of_buf(chunk_data, part_size, computed);
        if (!md5_eq(computed, md5_chunk)) {
            printf("[otad] Chunk %u MD5 mismatch!\n", part_num);
            send_nak(usbd, tx_buf.data(), part_num, "md5 mismatch");
            fclose(zip_out);
            cleanup_staging();
            return;
        }

        fwrite(chunk_data, 1, part_size, zip_out);
        received_parts++;

        // Draw progress bar
        int pct = (int)((received_parts * 100) / total_parts);
        // Simple progress bar: 200px wide
        int bar_w = (pct * 200) / 100;
        gfx.drawRect(10, 70, 200, 12, 0x4208);      // bg
        gfx.fillRect(10, 70, bar_w, 12, 0x07E0);    // fill

        printf("[otad] Chunk %u/%u OK\n", received_parts, total_parts);
        send_ack(usbd, tx_buf.data(), part_num);
    }

    fclose(zip_out);

    if (received_parts != total_parts) {
        printf("[otad] Part count mismatch: got %u expected %u\n", received_parts, total_parts);
        cleanup_staging();
        return;
    }

    // ---- Verify full zip MD5 ----
    printf("[otad] Verifying full zip...\n");
    uint8_t zip_md5[16];
    if (!md5_of_file(OTA_ZIP, zip_md5) || !md5_eq(zip_md5, md5_full)) {
        printf("[otad] Full zip MD5 mismatch!\n");
        send_nak(usbd, tx_buf.data(), 0, "full zip md5 mismatch");
        cleanup_staging();
        return;
    }

    send_ack(usbd, tx_buf.data(), 0xFFFFFFFF); // signal verify OK

    // ---- Apply ----
    if (!apply_update(gfx)) {
        printf("[otad] Apply failed\n");
        cleanup_staging();
        return;
    }

    cleanup_staging();

    printf("[otad] Update applied. Rebooting...\n");
    sleep(2);
    system("reboot");
}

// ============================================================
//  main
// ============================================================

int main(int argc, char* argv[]) {
    printf("[otad] Starting OTA Daemon v1.0\n");

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    LinuxGFX gfx("/dev/fb0");

    while (running) {
        UsbdClient usbd;

        if (!usbd.open(OTA_CHANNEL)) {
            printf("[otad] Waiting for USB connection on channel %d...\n", OTA_CHANNEL);
            sleep(2);
            continue;
        }

        printf("[otad] Connected on channel %d\n", OTA_CHANNEL);

        // Poll for an incoming OTA session (OTA_BEGIN)
        while (running && usbd.is_connected()) {
            int pr = usbd.poll(2000);
            if (pr < 0) { printf("[otad] Poll error\n"); break; }
            if (pr == 0) continue;

            // Peek at packet type without consuming — just run the session handler
            run_ota_session(usbd, gfx);

            // After session ends (success or fail) close channel; host reconnects for next one
            break;
        }

        printf("[otad] Session ended, closing channel\n");
        usbd.close();
        sleep(1);
    }

    printf("[otad] Shutdown complete\n");
    return 0;
}