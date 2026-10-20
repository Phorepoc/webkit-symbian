// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * OpenSSLHelperMbedTLS.cpp -- The `OpenSSL:` helper interface of
 * WebCore's curl backend (OpenSSLHelper.h) implemented on mbedTLS 3.6 (the tls layer's), the alternative to the
 * OpenSSL 3 build of curl that the port uses by default. With USE(MBEDTLS), OpenSSLHelper.h maps `SSL` to mbedtls_ssl_context and
 * `X509_STORE_CTX` to mbedtls_x509_crt (webkit/patch.py).
 *
 * Certificates travel through WebCore as PEM (CertificateInfo::Certificate), as in the OpenSSL version.
 * canonicalizeIPv6Address is a copy of the upstream helper (OpenSSLHelper.cpp, BSD-2: Sony Interactive Entertainment).
 */

#include "config.h"
#include "OpenSSLHelper.h"

#if USE(CURL) && USE(MBEDTLS)

#include <mbedtls/oid.h>
#include <mbedtls/pem.h>
#include <mbedtls/x509_crt.h>
#include <wtf/HexNumber.h>
#include <wtf/Seconds.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

namespace OpenSSL {

static constexpr ASCIILiteral pemHeader = "-----BEGIN CERTIFICATE-----\n"_s;
static constexpr ASCIILiteral pemFooter = "-----END CERTIFICATE-----\n"_s;

static std::optional<Vector<uint8_t>> pemFromDer(std::span<const uint8_t> der)
{
    size_t needed = 0;
    mbedtls_pem_write_buffer(pemHeader.characters(), pemFooter.characters(), der.data(), der.size(), nullptr, 0, &needed);
    if (!needed)
        return std::nullopt;
    Vector<uint8_t> pem(needed);
    size_t written = 0;
    if (mbedtls_pem_write_buffer(pemHeader.characters(), pemFooter.characters(), der.data(), der.size(), pem.mutableSpan().data(), pem.size(), &written))
        return std::nullopt;
    pem.shrink(written ? written - 1 : 0); // drop the terminating NUL
    return pem;
}

static WebCore::CertificateInfo::CertificateChain pemChain(const mbedtls_x509_crt* crt)
{
    WebCore::CertificateInfo::CertificateChain result;
    for (; crt && crt->raw.p; crt = crt->next) {
        auto pem = pemFromDer({ crt->raw.p, crt->raw.len });
        if (!pem)
            return { };
        result.append(WTF::move(*pem));
    }
    return result;
}

std::unique_ptr<WebCore::CertificateInfo> createCertificateInfo(std::optional<long>&& verifyResult, SSL* ssl)
{
    if (!verifyResult || !ssl)
        return nullptr;
    // the whole peer chain, leaf first (MBEDTLS_SSL_KEEP_PEER_CERTIFICATE)
    return makeUnique<WebCore::CertificateInfo>(*verifyResult, pemChain(mbedtls_ssl_get_peer_cert(ssl)));
}

WebCore::CertificateInfo::CertificateChain createCertificateChain(X509_STORE_CTX* chain)
{
    return pemChain(chain);
}

static String nameEntry(const mbedtls_x509_name* name, const char* oid, size_t oidLength)
{
    for (; name; name = name->next) {
        if (name->oid.len == oidLength && !memcmp(name->oid.p, oid, oidLength))
            return String::fromUTF8({ reinterpret_cast<const char*>(name->val.p), name->val.len });
    }
    return String();
}

static Seconds toSeconds(const mbedtls_x509_time& t)
{
    // days from civil (proleptic Gregorian), so no dependency on the C library's timegm
    int y = t.year, m = t.mon, d = t.day;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    double days = era * 146097.0 + doe - 719468.0;
    return Seconds(days * 86400.0 + t.hour * 3600.0 + t.min * 60.0 + t.sec);
}

String canonicalizeIPv6Address(std::span<uint8_t, 16> data)
{
    bool compressCurrentSection = false;
    size_t maxZeros = 0;
    std::optional<size_t> startRunner;
    std::optional<size_t> endRunner;
    std::optional<size_t> start;
    std::optional<size_t> end;

    for (int i = 0; i < 8; i++) {
        compressCurrentSection = !data[2 * i] && !data[2 * i + 1];
        if (compressCurrentSection) {
            startRunner = !startRunner.has_value() ? i : startRunner;
            endRunner = i;
            size_t len = endRunner.value() - startRunner.value() + 1;
            if (len > maxZeros) {
                start = startRunner;
                end = endRunner;
                maxZeros = len;
            }
        } else
            startRunner.reset();
    }

    size_t minimum = 1;
    StringBuilder ipAddress;
    String oldSection = ""_s;
    StringBuilder newSection;
    for (int j = 0; j < 8; j++) {
        if (j == start && maxZeros > minimum) {
            if (ipAddress.isEmpty())
                ipAddress.append(':');
            ipAddress.append(':');

            j = end.value();
            continue;
        }
        oldSection = makeString(hex(data[2 * j] << 8 | data[2 * j + 1], 4, Lowercase));
        newSection = StringBuilder();
        for (int k = 0; k < 4; k++) {
            if (oldSection[k] != '0' || !newSection.isEmpty() || k == 3)
                newSection.append(oldSection[k]);
        }
        ipAddress.append(newSection.toString());

        if (j != 7)
            ipAddress.append(':');
    }
    return ipAddress.toString();
}

std::optional<WebCore::CertificateSummary> createSummaryInfo(const Vector<uint8_t>& pem)
{
    // mbedtls_x509_crt_parse wants PEM NUL-terminated (length includes the NUL)
    Vector<uint8_t> buffer(pem);
    buffer.append(0);
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, buffer.span().data(), buffer.size())) {
        mbedtls_x509_crt_free(&crt);
        return std::nullopt;
    }

    WebCore::CertificateSummary summaryInfo;
    // Prefer the CN attribute, then O, then the whole DN (as the OpenSSL version / SecCertificateCopySubjectSummary)
    summaryInfo.subject = nameEntry(&crt.subject, MBEDTLS_OID_AT_CN, MBEDTLS_OID_SIZE(MBEDTLS_OID_AT_CN));
    if (summaryInfo.subject.isNull())
        summaryInfo.subject = nameEntry(&crt.subject, MBEDTLS_OID_AT_ORGANIZATION, MBEDTLS_OID_SIZE(MBEDTLS_OID_AT_ORGANIZATION));
    if (summaryInfo.subject.isNull()) {
        char dn[512];
        if (mbedtls_x509_dn_gets(dn, sizeof(dn), &crt.subject) > 0)
            summaryInfo.subject = String::fromUTF8(dn);
    }
    summaryInfo.validFrom = toSeconds(crt.valid_from);
    summaryInfo.validUntil = toSeconds(crt.valid_to);

    for (auto* san = &crt.subject_alt_names; san && san->buf.p; san = san->next) {
        mbedtls_x509_subject_alternative_name name;
        if (mbedtls_x509_parse_subject_alt_name(&san->buf, &name))
            continue;
        if (name.type == MBEDTLS_X509_SAN_DNS_NAME)
            summaryInfo.dnsNames.append(String::fromUTF8({ reinterpret_cast<const char*>(name.san.unstructured_name.p), name.san.unstructured_name.len }));
        else if (name.type == MBEDTLS_X509_SAN_IP_ADDRESS) {
            auto* p = name.san.unstructured_name.p;
            if (name.san.unstructured_name.len == 4)
                summaryInfo.ipAddresses.append(makeString(p[0], '.', p[1], '.', p[2], '.', p[3]));
            else if (name.san.unstructured_name.len == 16)
                summaryInfo.ipAddresses.append(canonicalizeIPv6Address(std::span<uint8_t, 16> { p, 16 }));
        }
        mbedtls_x509_free_subject_alt_name(&name);
    }
    mbedtls_x509_crt_free(&crt);
    return summaryInfo;
}

String tlsVersion(const SSL* ssl)
{
    return String::fromLatin1(mbedtls_ssl_get_version(ssl));
}

String tlsCipherName(const SSL* ssl)
{
    return String::fromLatin1(mbedtls_ssl_get_ciphersuite(ssl));
}

} // namespace OpenSSL

#endif // USE(CURL) && USE(MBEDTLS)
