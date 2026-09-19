#ifndef USBPROTO_FILE_H
#define USBPROTO_FILE_H

#include "usbproto.h"

#include <cstdio>
#include <string>

// =============================================================================
// FileTransport — replay a captured stream as if it were the USB pipe
// =============================================================================
//
// Reads a file of MUBD frames and serves it through the ordinary Transport
// interface, so a Session driven by a capture takes exactly the same code path
// as one driven by libusb. That makes the host tooling developable and testable
// with no board attached.
//
// Captures come from a desktop build of the firmware:
//
//   MFOES_DR_DUMP=frames.bin ./mfoes02w
//   remote_viewer --file frames.bin
//
// send() is accepted and discarded: a capture has nowhere to send to, and
// failing it would make Session::openChannel() look like an error.
//

namespace usbproto {

class FileTransport : public Transport {
public:
    FileTransport ();
    ~FileTransport () override;

    /**
     * @param path  File of MUBD frames to replay.
     * @param loop  Restart from the beginning at EOF instead of reporting
     *              end of stream — handy for staring at a short capture.
     */
    bool open (const char *path, bool loop = false);

    bool isOpen () const override;
    bool send   (const uint8_t *data, size_t len) override;
    int  recv   (uint8_t *buf, size_t bufSize, int timeoutMs) override;
    void close  () override;

    /// Bytes handed out per recv(). Small values exercise a Session's
    /// reassembly the way a real bulk pipe would. 0 means "fill the buffer".
    void setChunkSize (size_t bytes) { m_chunk = bytes; }

    bool atEnd () const { return m_ended; }
    const char *lastError () const override { return m_err.c_str(); }

private:
    FILE       *m_fp    = nullptr;
    bool        m_loop  = false;
    bool        m_ended = false;
    size_t      m_chunk = 0;
    std::string m_err;

    FileTransport (const FileTransport &)            = delete;
    FileTransport &operator= (const FileTransport &) = delete;
};

} // namespace usbproto

#endif // USBPROTO_FILE_H
