// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * CertificateInfoCurlMbedTLS.cpp -- CertificateInfoCurl.cpp with the verification
 * error text from mbedTLS. The error code is curl's CURLINFO_SSL_VERIFYRESULT, which the mbedTLS backend fills with the
 * mbedtls_x509_crt_verify flags (MBEDTLS_X509_BADCERT_*), not an X509_V_ERR_* code.
 */

#include "config.h"
#include "CertificateInfo.h"

#if USE(CURL) && USE(MBEDTLS)

#include "OpenSSLHelper.h"
#include <mbedtls/x509_crt.h>
#include <wtf/CrossThreadCopier.h>

namespace WebCore {

CertificateInfo::CertificateInfo(int verificationError, CertificateChain&& certificateChain)
    : m_verificationError(verificationError)
    , m_certificateChain(WTF::move(certificateChain))
{
}

CertificateInfo CertificateInfo::isolatedCopy() const
{
    return { m_verificationError, crossThreadCopy(m_certificateChain) };
}

String CertificateInfo::verificationErrorDescription() const
{
    if (!m_verificationError)
        return "ok"_s;
    char text[512];
    int length = mbedtls_x509_crt_verify_info(text, sizeof(text), "", static_cast<uint32_t>(m_verificationError));
    if (length <= 0)
        return "certificate verification failed"_s;
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == ' '))
        text[--length] = 0;
    return String::fromLatin1(text);
}

CertificateInfo::Certificate CertificateInfo::makeCertificate(std::span<const uint8_t> buffer)
{
    Certificate certificate;
    certificate.append(buffer);
    return certificate;
}

std::optional<CertificateSummary> CertificateInfo::summary() const
{
    if (!m_certificateChain.size())
        return std::nullopt;

    return OpenSSL::createSummaryInfo(m_certificateChain.at(0));
}

}

#endif
