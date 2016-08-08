#include "flags.hpp"
#include "validators.hpp"

#include <gflags/gflags.h>

#undef DEFINE_validator
#define DEFINE_validator(flag, validator) \
namespace { \
    bool flag##_validator __attribute__ ((unused)) = GFLAGS_NAMESPACE::RegisterFlagValidator(&FLAGS_##flag, validator); \
}


DEFINE_string(host, "feedback.push.apple.com", "feedback service hostname");

DEFINE_int32(port, 2196, "feedback service port");
DEFINE_validator(port, flags::validateRange<uint16_t>)

DEFINE_string(cert, "", "certificate");
DEFINE_string(key, "", "private key");
