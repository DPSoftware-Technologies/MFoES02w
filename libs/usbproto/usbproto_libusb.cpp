#include "usbproto_libusb.h"

#include <libusb.h>

#include <cstdarg>
#include <cstdio>

namespace usbproto {

namespace {

const char *xferTypeName (uint8_t attributes) {
    switch (attributes & 0x03) {
        case LIBUSB_TRANSFER_TYPE_CONTROL:     return "control";
        case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: return "isochronous";
        case LIBUSB_TRANSFER_TYPE_BULK:        return "bulk";
        case LIBUSB_TRANSFER_TYPE_INTERRUPT:   return "interrupt";
    }
    return "?";
}

void appendf (std::string &out, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

} // namespace

bool describeDevice (uint16_t vid, uint16_t pid, std::string &out) {
    out.clear();

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        out = "libusb_init failed";
        return false;
    }

    libusb_device **list = nullptr;
    const ssize_t   n    = libusb_get_device_list(ctx, &list);
    if (n < 0) {
        out = "libusb_get_device_list failed";
        libusb_exit(ctx);
        return false;
    }

    bool found = false;

    for (ssize_t i = 0; i < n && !found; ++i) {
        libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) != 0) continue;
        if (dd.idVendor != vid || dd.idProduct != pid) continue;

        found = true;
        appendf(out, "device %04x:%04x  bus %u  address %u  USB %x.%02x\n",
                dd.idVendor, dd.idProduct,
                libusb_get_bus_number(list[i]),
                libusb_get_device_address(list[i]),
                (dd.bcdUSB >> 8) & 0xFF, dd.bcdUSB & 0xFF);
        appendf(out, "  class %u  configurations %u\n",
                dd.bDeviceClass, dd.bNumConfigurations);

        libusb_config_descriptor *cfg = nullptr;
        if (libusb_get_active_config_descriptor(list[i], &cfg) != 0 || !cfg) {
            out += "  no active configuration — the device is not configured\n";
            break;
        }

        appendf(out, "  configuration %u: %u interface(s)\n",
                cfg->bConfigurationValue, cfg->bNumInterfaces);

        for (uint8_t ifc = 0; ifc < cfg->bNumInterfaces; ++ifc) {
            const libusb_interface &intf = cfg->interface[ifc];
            for (int alt = 0; alt < intf.num_altsetting; ++alt) {
                const libusb_interface_descriptor &id = intf.altsetting[alt];
                appendf(out, "    interface %u alt %u: %u endpoint(s)\n",
                        id.bInterfaceNumber, id.bAlternateSetting, id.bNumEndpoints);

                for (uint8_t e = 0; e < id.bNumEndpoints; ++e) {
                    const libusb_endpoint_descriptor &ep = id.endpoint[e];
                    const bool in = (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
                    appendf(out, "      endpoint 0x%02x  %-3s  %-11s  max packet %u\n",
                            ep.bEndpointAddress,
                            in ? "IN" : "OUT",
                            xferTypeName(ep.bmAttributes),
                            ep.wMaxPacketSize);
                }
            }
        }

        // Report the bulk pair the transport will actually pick. Endpoints are
        // discovered rather than assumed, so what matters is that the device
        // offers a bulk endpoint in each direction at all.
        int  bulkIn = -1, bulkOut = -1;
        for (uint8_t ifc = 0; ifc < cfg->bNumInterfaces; ++ifc) {
            const libusb_interface &intf = cfg->interface[ifc];
            for (int alt = 0; alt < intf.num_altsetting; ++alt) {
                const libusb_interface_descriptor &id = intf.altsetting[alt];
                for (uint8_t e = 0; e < id.bNumEndpoints; ++e) {
                    const libusb_endpoint_descriptor &ep = id.endpoint[e];
                    if ((ep.bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        if (bulkIn  < 0) bulkIn  = ep.bEndpointAddress;
                    } else {
                        if (bulkOut < 0) bulkOut = ep.bEndpointAddress;
                    }
                }
            }
        }

        out += "\n  the transport will use:";
        if (bulkOut >= 0) appendf(out, "  OUT 0x%02x", bulkOut);
        else              out += "  OUT [NONE FOUND]";
        if (bulkIn  >= 0) appendf(out, "  IN 0x%02x\n", bulkIn);
        else              out += "  IN [NONE FOUND]\n";

        libusb_free_config_descriptor(cfg);
    }

    if (!found) {
        appendf(out, "no device %04x:%04x found (%ld USB devices present)\n",
                vid, pid, (long)n);
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return found;
}

LibusbTransport::LibusbTransport () = default;

LibusbTransport::~LibusbTransport () {
    close();
}

bool LibusbTransport::open (uint16_t vid, uint16_t pid, int iface) {
    close();
    m_err.clear();

    int rc = libusb_init(&m_ctx);
    if (rc != 0) {
        m_err = std::string("libusb_init failed: ") + libusb_strerror((libusb_error)rc);
        m_ctx = nullptr;
        return false;
    }

#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x01000106
    if (m_debug) {
        libusb_set_option(m_ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG);
    }
#endif

    m_dev = libusb_open_device_with_vid_pid(m_ctx, vid, pid);
    if (!m_dev) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "no device %04x:%04x — is it plugged in and is usbd running?",
                 vid, pid);
        m_err = buf;
        libusb_exit(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    // On Linux a kernel driver may already hold the interface. Hand it over
    // for the lifetime of this handle; libusb restores it on release.
    libusb_set_auto_detach_kernel_driver(m_dev, 1);

    rc = libusb_claim_interface(m_dev, iface);
    if (rc != 0) {
        m_err = std::string("claim interface failed: ")
              + libusb_strerror((libusb_error)rc)
              + " (another program may be holding the device)";
        libusb_close(m_dev);
        m_dev = nullptr;
        libusb_exit(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    m_iface   = iface;
    m_claimed = true;

    // The endpoint numbers usbd writes into its FunctionFS descriptors are a
    // request, not an assignment: the gadget asks for IN ep2 and dwc2 hands
    // out 0x81. Read the addresses the device actually enumerated with.
    discoverEndpoints();
    return true;
}

void LibusbTransport::discoverEndpoints () {
    libusb_device *dev = m_dev ? libusb_get_device(m_dev) : nullptr;
    if (!dev) return;

    libusb_config_descriptor *cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0 || !cfg) return;

    bool gotIn = false, gotOut = false;

    for (uint8_t i = 0; i < cfg->bNumInterfaces && !(gotIn && gotOut); ++i) {
        const libusb_interface &intf = cfg->interface[i];
        for (int alt = 0; alt < intf.num_altsetting; ++alt) {
            const libusb_interface_descriptor &id = intf.altsetting[alt];
            if (id.bInterfaceNumber != m_iface) continue;

            for (uint8_t e = 0; e < id.bNumEndpoints; ++e) {
                const libusb_endpoint_descriptor &ep = id.endpoint[e];
                if ((ep.bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_BULK) continue;

                if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                    if (!gotIn)  { m_epIn  = ep.bEndpointAddress; gotIn  = true; }
                } else {
                    if (!gotOut) { m_epOut = ep.bEndpointAddress; gotOut = true; }
                }
            }
        }
    }

    libusb_free_config_descriptor(cfg);
}

bool LibusbTransport::isOpen () const {
    return m_dev != nullptr;
}

bool LibusbTransport::send (const uint8_t *data, size_t len) {
    if (!m_dev || !data || len == 0) return false;

    size_t sent = 0;
    while (sent < len) {
        // A bulk write can report a short transfer; keep going until it is all
        // out, otherwise a frame would be truncated and desync the far end.
        const size_t chunk = len - sent;
        int moved = 0;
        const int rc = libusb_bulk_transfer(
            m_dev, m_epOut,
            const_cast<unsigned char *>(data + sent),
            (int)(chunk > (size_t)INT32_MAX ? (size_t)INT32_MAX : chunk),
            &moved, m_writeTimeoutMs);

        if (rc != 0) {
            m_err = std::string("bulk write failed: ") + libusb_strerror((libusb_error)rc);
            return false;
        }
        if (moved <= 0) {
            m_err = "bulk write made no progress";
            return false;
        }
        sent += (size_t)moved;
    }
    return true;
}

int LibusbTransport::recv (uint8_t *buf, size_t bufSize, int timeoutMs) {
    if (!m_dev || !buf || bufSize == 0) return -1;

    int moved = 0;
    const int rc = libusb_bulk_transfer(
        m_dev, m_epIn, buf,
        (int)(bufSize > (size_t)INT32_MAX ? (size_t)INT32_MAX : bufSize),
        &moved, (unsigned)(timeoutMs < 0 ? 0 : timeoutMs));

    if (rc == LIBUSB_ERROR_TIMEOUT) {
        // A timeout can still have delivered a partial transfer.
        return moved > 0 ? moved : 0;
    }
    if (rc != 0) {
        m_err = std::string("bulk read failed: ") + libusb_strerror((libusb_error)rc);
        return -1;
    }
    return moved;
}

void LibusbTransport::close () {
    if (m_dev) {
        if (m_claimed) libusb_release_interface(m_dev, m_iface);
        libusb_close(m_dev);
        m_dev = nullptr;
    }
    m_claimed = false;
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

} // namespace usbproto
