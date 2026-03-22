#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

// Usage:
//   UsbdClient client;
//   if (!client.open(0)) { /* error */ }
//   client.send(data, len);
//   int r = client.recv(buf, sizeof(buf));  // blocking
//   client.close();

#define USBD_SOCK_PATH "/var/run/usbd.sock"
#define USBD_CHAN_AUTO 0xFF

class UsbdClient {
public:
    int     fd;
    uint8_t channel_id;
    bool    connected;

    UsbdClient();

    bool    open(uint8_t requested_channel = USBD_CHAN_AUTO,
                 const char* sock_path = USBD_SOCK_PATH);
    ssize_t send(const void* data, uint32_t len);
    ssize_t recv(void* buf, uint32_t buf_size);
    int     poll(int timeout_ms = 0);
    void    close();

    bool    is_connected() const;
    uint8_t get_channel()  const;

private:
    void    _disconnect();
    ssize_t _write(const void* buf, size_t len);
    ssize_t _read(void* buf, size_t len);
};
