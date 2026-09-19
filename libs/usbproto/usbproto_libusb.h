#ifndef USBPROTO_LIBUSB_H
#define USBPROTO_LIBUSB_H

#include "usbproto.h"

#include <string>

struct libusb_context;
struct libusb_device_handle;

// =============================================================================
// LibusbTransport — host side of the MFoES USB link
// =============================================================================
//
// Talks to the Pi's USB gadget over the bulk pipe that usbd exposes:
// EP_BULK_OUT carries host -> device, EP_BULK_IN carries device -> host.
// This is the C++ counterpart of tools/usb_channel.py, and the two must stay
// interchangeable — both speak the framing defined in usbproto.h.
//
//   usbproto::LibusbTransport t;
//   if (!t.open()) { fprintf(stderr, "%s\n", t.lastError().c_str()); return 1; }
//   usbproto::Session s(&t);
//   s.openChannel(usbproto::CHAN_DRAWREPLAY);
//   s.pump(100, [](uint8_t ch, uint8_t flags, const uint8_t* p, size_t n) { ... });
//

namespace usbproto {

/**
 * Describe what the gadget actually exposes: configuration, interfaces and
 * every endpoint with its address, direction and type.
 *
 * Purely diagnostic — nothing is claimed and no transfer is attempted. This
 * exists because the transport assumes interface 0 with EP_BULK_OUT/EP_BULK_IN,
 * and when that assumption is wrong the only symptom is a bulk transfer failing
 * for no visible reason.
 *
 * @return false when no matching device is present; @p out says why.
 */
bool describeDevice (uint16_t vid, uint16_t pid, std::string &out);

class LibusbTransport : public Transport {
public:
    LibusbTransport ();
    ~LibusbTransport () override;

    /**
     * Find and claim the gadget.
     * @param vid,pid  USB identity; defaults match the usbd descriptors.
     * @param iface    Interface number to claim.
     * @return false on failure — lastError() says why.
     */
    bool open (uint16_t vid = DEFAULT_VID, uint16_t pid = DEFAULT_PID, int iface = 0);

    bool isOpen () const override;
    bool send   (const uint8_t *data, size_t len) override;
    int  recv   (uint8_t *buf, size_t bufSize, int timeoutMs) override;
    void close  () override;

    const char *lastError () const override { return m_err.c_str(); }

    /// Milliseconds a single bulk write may take before it is treated as failed.
    void setWriteTimeout (unsigned ms) { m_writeTimeoutMs = ms; }

    /// Turn on libusb's own logging. Must be set before open().
    void setDebug (bool on) { m_debug = on; }

    /// Endpoint addresses actually in use, discovered during open().
    uint8_t endpointIn  () const { return m_epIn; }
    uint8_t endpointOut () const { return m_epOut; }

private:
    libusb_context       *m_ctx    = nullptr;
    libusb_device_handle *m_dev    = nullptr;
    int                   m_iface  = 0;
    bool                  m_claimed = false;
    bool                  m_debug   = false;
    uint8_t               m_epIn    = EP_BULK_IN;   ///< replaced by discovery
    uint8_t               m_epOut   = EP_BULK_OUT;  ///< replaced by discovery
    unsigned              m_writeTimeoutMs = 5000;
    std::string           m_err;

    /// Read the real bulk endpoint addresses off the claimed interface.
    void discoverEndpoints ();

    LibusbTransport (const LibusbTransport &)            = delete;
    LibusbTransport &operator= (const LibusbTransport &) = delete;
};

} // namespace usbproto

#endif // USBPROTO_LIBUSB_H
