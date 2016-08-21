#pragma once

#include <folly/Exception.h>
#include <folly/ScopeGuard.h>
#include <limits>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace flags
{
template<typename T, T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max()>
bool validateRange(const char* flag, int32_t value)
{
    return min <= value && value <= max;
}
bool validateExists(const char* flag, const std::string& path)
{
    struct stat st;
    folly::checkUnixError(stat(path.c_str(), &st), flag, " = `", path.c_str(), "'");
    return S_ISREG(st.st_mode);
}
template<mode_t mode = O_RDONLY>
bool validateAccessible(const char* flag, const std::string& path)
{
    int handle = open(path.c_str(), 0, mode);
    auto guard = folly::makeGuard([&]() {
            folly::checkUnixError(close(handle), flag, " = `", path.c_str(), "' close error");
        });
    folly::checkUnixError(handle, flag, " = `", path.c_str(), "' inaccessible");
    return handle > 0;
}
}

#if 0
namespace {

struct Validatate {
    template<typename T, typename V>
    explicit Validatate(T flag, V validator) { google::RegisterFlagValidator(flag, validator); }
    ~Validatate() {}
};
//Validatate cert{&FLAGS_cert, &x};
//Validatate key{&FLAGS_key, &x};
//Validatate port{&FLAGS_port, &validateRange<uint16_t>};
}

#endif
