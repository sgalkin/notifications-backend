#include "flags.hpp"
#include "validators.hpp"

#include <gflags/gflags.h>

#undef DEFINE_validator
#define DEFINE_validator(flag, validator) \
namespace { \
    bool flag##_validator __attribute__ ((unused)) = GFLAGS_NAMESPACE::RegisterFlagValidator(&FLAGS_##flag, validator); \
}

DEFINE_string(apns_cert, "", "APNS certificate");
DEFINE_validator(apns_cert, flags::validateAccessible);
DEFINE_string(apns_key, "", "APNS private key");
DEFINE_validator(apns_key, flags::validateAccessible);

DEFINE_string(https_cert, "", "HTTPS certificate");
DEFINE_validator(https_cert, flags::validateAccessible);
DEFINE_string(https_key, "", "HTTPS private key");
DEFINE_validator(https_key, flags::validateAccessible);
DEFINE_int32(https_port, 443, "HTTPS server port");
DEFINE_validator(https_port, flags::validateRange<uint16_t>)
