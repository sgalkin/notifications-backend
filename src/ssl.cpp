#include "ssl.hpp"

#include <openssl/ssl.h>
#include <wangle/ssl/SSLUtil.h>

namespace ssl {
folly::SSLContextPtr createContext(const std::string& cert, const std::string& key) {
    if(cert.empty() || key.empty()) {
        LOG(INFO) << "certificate or private key is empty"; 
        return nullptr;
    }
    
    auto ctx = std::make_shared<folly::SSLContext>(); 
    ctx->setSSLLockTypes({{CRYPTO_LOCK_SSL, folly::SSLContext::LOCK_NONE}});

    CHECK(SSL_CTX_load_verify_locations(ctx->getSSLCtx(), nullptr, "/etc/ssl/certs"));
    
    ctx->setVerificationOption(folly::SSLContext::VERIFY_REQ_CLIENT_CERT);
    ctx->loadCertificate(cert.c_str());
    ctx->loadPrivateKey(key.c_str());

    LOG(INFO) << "context created for CN=" << getCommonName(ctx);
    return ctx;
}

std::string getCommonName(folly::SSLContextPtr ctx) {
    CHECK(ctx);
    auto cn = wangle::SSLUtil::getCommonName(SSL_CTX_get0_certificate(ctx->getSSLCtx()));
    return cn ? *cn : "<unknonw>";
}
}
