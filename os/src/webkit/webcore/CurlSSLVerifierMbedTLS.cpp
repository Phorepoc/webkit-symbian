// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * CurlSSLVerifierMbedTLS.cpp -- The OpenSSL CurlSSLVerifier installs its own
 * verify callback to collect the chain. curl's mbedTLS backend owns mbedtls_ssl_conf_verify (mbed_verify_cb: verifypeer /
 * verifyhost handling, curl 8.10 lib/vtls/mbedtls.c), and CURLOPT_SSL_CTX_FUNCTION runs after it, so replacing that callback
 * would break curl's checks. The chain is read after the handshake instead: createCertificateInfo returns nothing here and
 * CurlHandle::certificateInfo() falls back to OpenSSL::createCertificateInfo(ssl) = mbedtls_ssl_get_peer_cert (the upstream
 * path for reused HTTP/2 connections).
 */

#include "config.h"
#include "CurlSSLVerifier.h"
#include <wtf/TZoneMallocInlines.h>

#if USE(CURL) && USE(MBEDTLS)

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(CurlSSLVerifier);

CurlSSLVerifier::CurlSSLVerifier(void*)
{
}

std::unique_ptr<WebCore::CertificateInfo> CurlSSLVerifier::createCertificateInfo(std::optional<long>&&)
{
    return nullptr;
}

} // namespace WebCore

#endif
