#pragma once

#include <limits>

namespace flags
{
template<typename T, T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max()>
bool validateRange(const char* flag, int32_t value)
{
    return min <= value && value <= max;
}
}

#if 0
namespace {
auto x = [](const char* flag, const std::string& value)
{
    struct stat st;
    folly::checkUnixError(stat(value.c_str(), &st), flag, " = ", value.c_str());
    return S_ISREG(st.st_mode);
};

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
