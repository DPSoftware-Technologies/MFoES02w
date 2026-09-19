#include "usbproto_file.h"

namespace usbproto {

FileTransport::FileTransport () = default;

FileTransport::~FileTransport () {
    close();
}

bool FileTransport::open (const char *path, bool loop) {
    close();
    m_err.clear();

    if (!path || !*path) {
        m_err = "no path given";
        return false;
    }

    m_fp = fopen(path, "rb");
    if (!m_fp) {
        m_err = std::string("cannot open ") + path;
        return false;
    }

    m_loop  = loop;
    m_ended = false;
    return true;
}

bool FileTransport::isOpen () const {
    return m_fp != nullptr;
}

bool FileTransport::send (const uint8_t *, size_t) {
    // A capture has no far end. Accept and discard so channel setup succeeds.
    return true;
}

int FileTransport::recv (uint8_t *buf, size_t bufSize, int /*timeoutMs*/) {
    if (!m_fp || !buf || bufSize == 0) return -1;
    if (m_ended) return -1;

    size_t want = (m_chunk && m_chunk < bufSize) ? m_chunk : bufSize;

    size_t got = fread(buf, 1, want, m_fp);
    if (got > 0) return (int)got;

    // EOF.
    if (m_loop) {
        if (fseek(m_fp, 0, SEEK_SET) != 0) {
            m_err  = "rewind failed";
            m_ended = true;
            return -1;
        }
        got = fread(buf, 1, want, m_fp);
        if (got > 0) return (int)got;
        m_err = "capture is empty";   // looping an empty file would spin
    }

    m_ended = true;
    return -1;
}

void FileTransport::close () {
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
    m_ended = false;
}

} // namespace usbproto
