#include "usbd_client.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <endian.h>

UsbdClient::UsbdClient()
    : fd(-1), channel_id(0), connected(false)
{}

bool UsbdClient::open(uint8_t requested_channel, const char* sock_path) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd); fd = -1;
        return false;
    }

    if (_write(&requested_channel, 1) < 0) {
        ::close(fd); fd = -1;
        return false;
    }

    uint8_t resp = 0xFF;
    if (_read(&resp, 1) < 0 || resp == 0xFF) {
        ::close(fd); fd = -1;
        return false;
    }

    channel_id = resp;
    connected  = true;
    return true;
}

ssize_t UsbdClient::send(const void* data, uint32_t len) {
    if (!connected || fd < 0) return -1;
    uint32_t net_len = htole32(len);
    if (_write(&net_len, 4) < 0) { _disconnect(); return -1; }
    ssize_t r = _write(data, len);
    if (r < 0) _disconnect();
    return r;
}

ssize_t UsbdClient::recv(void* buf, uint32_t buf_size) {
    if (!connected || fd < 0) return -1;
    uint32_t plen = 0;
    if (_read(&plen, 4) != 4) { _disconnect(); return -1; }
    plen = le32toh(plen);
    if (plen == 0 || plen > buf_size) return -1;
    ssize_t r = _read(buf, plen);
    if (r < 0) _disconnect();
    return r;
}

int UsbdClient::poll(int timeout_ms) {
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    return select(fd + 1, &rfds, nullptr, nullptr, &tv);
}

void UsbdClient::close() {
    if (fd >= 0) { ::close(fd); fd = -1; }
    connected = false;
}

bool UsbdClient::is_connected() const {
    return connected && fd >= 0;
}

uint8_t UsbdClient::get_channel() const {
    return channel_id;
}

void UsbdClient::_disconnect() {
    connected = false;
}

ssize_t UsbdClient::_write(const void* buf, size_t len) {
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

ssize_t UsbdClient::_read(void* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t r = read(fd, (uint8_t*)buf + got, len - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return (got > 0) ? (ssize_t)got : -1;
        got += r;
    }
    return (ssize_t)got;
}
