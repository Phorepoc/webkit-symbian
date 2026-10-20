// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * CryptoDigestMbedTLS.cpp -- PAL::Crypto::CryptoDigest on mbedTLS's generic message
 * digest (mbedtls_md), the shape of PAL's crypto/openssl/CryptoDigestOpenSSL.cpp. SHA-224 is refused like the OpenSSL version.
 */

#include "config.h"
#include "CryptoDigest.h"

#include <mbedtls/md.h>

namespace PAL::Crypto {

struct CryptoDigestContext {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(CryptoDigestContext);

    explicit CryptoDigestContext(mbedtls_md_type_t type)
    {
        mbedtls_md_init(&context);
        info = mbedtls_md_info_from_type(type);
        RELEASE_ASSERT(info && !mbedtls_md_setup(&context, info, 0) && !mbedtls_md_starts(&context));
    }
    ~CryptoDigestContext() { mbedtls_md_free(&context); }

    mbedtls_md_context_t context;
    const mbedtls_md_info_t* info { nullptr };
};

CryptoDigest::CryptoDigest() = default;

CryptoDigest::~CryptoDigest() = default;

static mbedtls_md_type_t mdType(CryptoDigest::Algorithm algorithm)
{
    switch (algorithm) {
    case CryptoDigest::Algorithm::SHA_1:
        return MBEDTLS_MD_SHA1;
    case CryptoDigest::Algorithm::DEPRECATED_SHA_224:
        RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE("SHA224 is not supported.");
    case CryptoDigest::Algorithm::SHA_256:
        return MBEDTLS_MD_SHA256;
    case CryptoDigest::Algorithm::SHA_384:
        return MBEDTLS_MD_SHA384;
    case CryptoDigest::Algorithm::SHA_512:
        return MBEDTLS_MD_SHA512;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

std::unique_ptr<CryptoDigest> CryptoDigest::create(CryptoDigest::Algorithm algorithm)
{
    std::unique_ptr<CryptoDigest> digest = WTF::makeUnique<CryptoDigest>();
    digest->m_context = makeUnique<CryptoDigestContext>(mdType(algorithm));
    return digest;
}

void CryptoDigest::addBytes(std::span<const uint8_t> input)
{
    ASSERT(m_context);
    mbedtls_md_update(&m_context->context, input.data(), input.size());
}

Vector<uint8_t> CryptoDigest::computeHash()
{
    ASSERT(m_context);
    Vector<uint8_t> result(mbedtls_md_get_size(m_context->info));
    mbedtls_md_finish(&m_context->context, result.mutableSpan().data());
    return result;
}

} // namespace PAL::Crypto
