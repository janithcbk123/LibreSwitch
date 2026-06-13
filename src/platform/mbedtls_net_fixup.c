// =====================================================================
//  platform/mbedtls_net_fixup.c — Beken mbedTLS net shim (ON-DEVICE)
// ---------------------------------------------------------------------
//  LibreTiny's shared WiFiClientSecure (MbedTLSClient.cpp) calls
//  mbedtls_net_set_nonblock(), but the BEKEN mbedTLS port does not define
//  it — LibreTiny only ships this fixup for the Realtek family
//  (cores/realtek-ambz/base/fixups/net_sockets.c), not Beken. That leaves
//  an undefined reference at link time:
//
//    MbedTLSClient.cpp:248: undefined reference to `mbedtls_net_set_nonblock'
//
//  This provides the missing function(s), mirroring the Realtek fixup.
//  The platform's lwIP supplies fcntl()/F_SETFL/O_NONBLOCK (LibreTiny's
//  own LwIPClient.cpp uses them), and mbedtls_net_context is { int fd; },
//  so the standard POSIX-socket implementation is correct here.
//
//  Compiled only on-device (guarded), so the host test build is untouched.
//  This is a PLATFORM GAP workaround, not project logic.
// =====================================================================
#if defined(LT_BUILD) || defined(ARDUINO)

#include <mbedtls/net_sockets.h>
#include <lwip/sockets.h>

// Use lwip_fcntl explicitly: on lwIP, plain fcntl() may not be the socket
// fcntl (it can resolve to the C-library file fcntl). LibreTiny's own
// LwIPClient.cpp uses lwip_fcntl for socket flags — mirror that exactly.

// Provide weak definitions so that if a future LibreTiny/Beken update DOES
// ship these, the platform's strong symbols win and this silently yields.
__attribute__((weak))
int mbedtls_net_set_nonblock(mbedtls_net_context *ctx) {
    return lwip_fcntl(ctx->fd, F_SETFL, lwip_fcntl(ctx->fd, F_GETFL, 0) | O_NONBLOCK);
}

__attribute__((weak))
int mbedtls_net_set_block(mbedtls_net_context *ctx) {
    return lwip_fcntl(ctx->fd, F_SETFL, lwip_fcntl(ctx->fd, F_GETFL, 0) & ~O_NONBLOCK);
}

#endif  // LT_BUILD || ARDUINO
