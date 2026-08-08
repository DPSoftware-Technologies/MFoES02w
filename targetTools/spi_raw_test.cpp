/*  spi_raw_test — 4-byte SPI loopback check, no protocol involved.
 *
 *  Jumper MOSI to MISO and run it: MATCH means the bus, the overlay and the
 *  spidev node all work. Use this before nb_link_test when bringing up wiring.
 *
 *  Build: cc -O2 -o spi_raw_test targetTools/spi_raw_test.cpp
 *  Usage: spi_raw_test [spidev]                                             */

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *device = argc > 1 ? argv[1] : "/dev/spidev1.0";
    int fd = open(device, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rx[4] = {0};

    struct spi_ioc_transfer tr = {0};
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = sizeof(tx);
    tr.speed_hz = 500000;
    tr.bits_per_word = 8;

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("sent:     ");
    for (int i = 0; i < 4; i++) printf("0x%02X ", tx[i]);
    printf("\nreceived: ");
    for (int i = 0; i < 4; i++) printf("0x%02X ", rx[i]);
    printf("\n%s\n", memcmp(tx, rx, 4) == 0 ? "MATCH!" : "MISMATCH - check wiring");

    close(fd);
    return 0;
}
