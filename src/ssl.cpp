#include "ssl.hpp"

#include <wangle/ssl/SSLUtil.h>
#include <folly/io/async/ssl/OpenSSLPtrTypes.h>
#include <folly/Likely.h>
#include <openssl/ssl.h>

namespace ssl {
folly::SSLContextPtr createContext(const std::string& cert, const std::string& key) {
    if(cert.empty() || key.empty()) {
        LOG(WARNING) << "certificate or private key is empty"; 
        return nullptr;
    }
    
    auto ctx = std::make_shared<folly::SSLContext>(); 
    ctx->setSSLLockTypes({{CRYPTO_LOCK_SSL, folly::SSLContext::LOCK_NONE}});

    CHECK(SSL_CTX_load_verify_locations(ctx->getSSLCtx(), nullptr, "/etc/ssl/certs"));
    
    ctx->setVerificationOption(folly::SSLContext::VERIFY_REQ_CLIENT_CERT);
    ctx->loadCertificate(cert.c_str());
    ctx->loadPrivateKey(key.c_str());

    VLOG(3) << "context created for CN=" << getCommonName(cert);
    return ctx;
}
/*
std::string getCommonName(folly::SSLContextPtr ctx) {
    CHECK(ctx);
    auto cn = wangle::SSLUtil::getCommonName(SSL_CTX_get0_certificate(ctx->getSSLCtx()));
    return cn ? *cn : "<unknonw>";
}
*/
std::string getCommonName(const std::string& cert) {
    static const std::string unknown = "<unknown>";
    folly::ssl::BioUniquePtrFb bio{BIO_new(BIO_s_file())};
    CHECK(LIKELY(!!bio));
    if(!BIO_read_filename(bio.get(), cert.c_str())) return unknown;

    folly::ssl::X509UniquePtr x509{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
    if(!x509) {
        LOG(ERROR) << "unable to read PEM encoded certificate from " << cert;
        return unknown;
    }

    auto cn = wangle::SSLUtil::getCommonName(x509.get());
    return cn ? *cn : unknown;
}

}
