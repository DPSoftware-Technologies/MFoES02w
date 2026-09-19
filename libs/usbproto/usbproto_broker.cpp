#include "usbproto_broker.h"

#include <cstring>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET sock_t;
#  define SOCK_BAD      INVALID_SOCKET
#  define CLOSE_SOCK(s) closesocket(s)
#  define SOCK_WOULDBLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
   typedef int sock_t;
#  define SOCK_BAD      (-1)
#  define CLOSE_SOCK(s) ::close(s)
#  define SOCK_WOULDBLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

namespace usbproto {

namespace {

bool setNonBlocking (sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    return ioctlsocket(s, FIONBIO, &nb) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

void disableNagle (sock_t s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

} // namespace

// =============================================================================

struct Broker::Impl {
    struct Client {
        sock_t               sock = SOCK_BAD;
        FrameParser          parser;              ///< client bytes -> frames
        std::vector<uint8_t> out;                 ///< pending, non-blocking
        size_t               outPos = 0;          ///< how much of out is sent
    };

    sock_t               listenSock = SOCK_BAD;
    std::vector<Client>  clients;
    std::string          err;
    bool                 running   = false;
    size_t               queueLimit = 8u * 1024u * 1024u;

    size_t relayed  = 0;
    size_t injected = 0;
    size_t dropped  = 0;

#ifdef _WIN32
    bool wsaUp = false;
#endif

    ~Impl () { shutdownAll(); }

    void shutdownAll () {
        for (auto &c : clients) {
            if (c.sock != SOCK_BAD) CLOSE_SOCK(c.sock);
        }
        clients.clear();
        if (listenSock != SOCK_BAD) {
            CLOSE_SOCK(listenSock);
            listenSock = SOCK_BAD;
        }
#ifdef _WIN32
        if (wsaUp) { WSACleanup(); wsaUp = false; }
#endif
        running = false;
    }

    void dropClient (size_t i) {
        if (clients[i].sock != SOCK_BAD) CLOSE_SOCK(clients[i].sock);
        clients.erase(clients.begin() + (std::ptrdiff_t)i);
    }

    /// Push whatever is queued; returns false when the client should be dropped.
    bool flush (Client &c) {
        while (c.outPos < c.out.size()) {
            const size_t remain = c.out.size() - c.outPos;
            const int n = (int)send(c.sock, (const char *)c.out.data() + c.outPos,
                                    (int)(remain > 65536 ? 65536 : remain), 0);
            if (n > 0) {
                c.outPos += (size_t)n;
                continue;
            }
            if (n < 0 && SOCK_WOULDBLOCK) break;   // socket full; try again later
            return false;                          // real error
        }
        if (c.outPos == c.out.size()) {
            c.out.clear();
            c.outPos = 0;
        } else if (c.outPos > 1u * 1024u * 1024u) {
            // Reclaim the consumed prefix occasionally rather than on every
            // write, so a busy client does not memmove constantly.
            c.out.erase(c.out.begin(), c.out.begin() + (std::ptrdiff_t)c.outPos);
            c.outPos = 0;
        }
        return true;
    }
};

// =============================================================================

Broker::Broker () : m(new Impl) {}

Broker::~Broker () { stop(); }

bool Broker::start (uint16_t port) {
    stop();
    m->err.clear();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        m->err = "WSAStartup failed";
        return false;
    }
    m->wsaUp = true;
#endif

    m->listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m->listenSock == SOCK_BAD) {
        m->err = "cannot create listening socket";
        m->shutdownAll();
        return false;
    }

    int one = 1;
    setsockopt(m->listenSock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&one, sizeof(one));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local IPC only

    if (bind(m->listenSock, (sockaddr *)&addr, sizeof(addr)) != 0) {
        m->err = "cannot bind 127.0.0.1:" + std::to_string(port)
               + " (is another broker already running?)";
        m->shutdownAll();
        return false;
    }

    if (listen(m->listenSock, 8) != 0) {
        m->err = "listen failed";
        m->shutdownAll();
        return false;
    }

    setNonBlocking(m->listenSock);
    m->running = true;
    return true;
}

void Broker::stop () {
    if (m) m->shutdownAll();
}

bool Broker::isRunning () const {
    return m && m->running;
}

void Broker::poll (int timeoutMs, const ClientFrameFn &onClientFrame) {
    if (!m || !m->running) return;

    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    sock_t maxfd = m->listenSock;
    FD_SET(m->listenSock, &rfds);

    for (auto &c : m->clients) {
        FD_SET(c.sock, &rfds);
        if (c.outPos < c.out.size()) FD_SET(c.sock, &wfds);
        if (c.sock > maxfd) maxfd = c.sock;
    }

    timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    const int rc = select((int)maxfd + 1, &rfds, &wfds, nullptr, &tv);
    if (rc <= 0) return;

    // ----- new clients -------------------------------------------------------
    if (FD_ISSET(m->listenSock, &rfds)) {
        for (;;) {
            sock_t s = accept(m->listenSock, nullptr, nullptr);
            if (s == SOCK_BAD) break;
            setNonBlocking(s);
            disableNagle(s);        // draw frames are small and latency-sensitive
            Impl::Client c;
            c.sock = s;
            m->clients.push_back(std::move(c));
        }
    }

    // ----- client traffic ----------------------------------------------------
    uint8_t buf[65536];

    for (size_t i = 0; i < m->clients.size(); ) {
        Impl::Client &c = m->clients[i];
        bool alive = true;

        if (FD_ISSET(c.sock, &wfds)) {
            alive = m->flush(c);
        }

        if (alive && FD_ISSET(c.sock, &rfds)) {
            for (;;) {
                const int n = (int)recv(c.sock, (char *)buf, (int)sizeof(buf), 0);
                if (n > 0) {
                    // Reassemble before forwarding: raw bytes from two clients
                    // could interleave on the USB pipe and corrupt it.
                    c.parser.feed(buf, (size_t)n,
                        [&](uint8_t ch, uint8_t flags, const uint8_t *p, size_t len) {
                            if (!onClientFrame) return;
                            std::vector<uint8_t> frame;
                            encodeFrame(frame, ch, flags, p, len);
                            onClientFrame(frame.data(), frame.size());
                            ++m->injected;
                        });
                    continue;
                }
                if (n == 0) { alive = false; break; }            // clean close
                if (SOCK_WOULDBLOCK) break;                      // drained
                alive = false;
                break;
            }
        }

        if (!alive) {
            m->dropClient(i);
            continue;
        }
        ++i;
    }
}

void Broker::broadcast (const uint8_t *frame, size_t len) {
    if (!m || !m->running || !frame || len == 0) return;

    for (size_t i = 0; i < m->clients.size(); ) {
        Impl::Client &c = m->clients[i];

        const size_t queued = c.out.size() - c.outPos;
        if (queued + len > m->queueLimit) {
            // Falling behind badly. Dropping beats stalling the render loop.
            ++m->dropped;
            m->dropClient(i);
            continue;
        }

        c.out.insert(c.out.end(), frame, frame + len);
        if (!m->flush(c)) {
            m->dropClient(i);
            continue;
        }
        ++i;
    }
    ++m->relayed;
}

size_t Broker::clientCount    () const { return m ? m->clients.size() : 0; }
size_t Broker::framesRelayed  () const { return m ? m->relayed  : 0; }
size_t Broker::framesInjected () const { return m ? m->injected : 0; }
size_t Broker::clientsDropped () const { return m ? m->dropped  : 0; }

void Broker::setClientQueueLimit (size_t bytes) {
    if (m && bytes) m->queueLimit = bytes;
}

const char *Broker::lastError () const {
    return m ? m->err.c_str() : "";
}

} // namespace usbproto
