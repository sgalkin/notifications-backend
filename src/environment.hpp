#pragma once

#include <string>

namespace apn {
namespace binary {
namespace sandbox {
    const std::string gateway = "gateway.sandbox.push.apple.com:2195";
    const std::string feedback = "feedback.sandbox.push.apple.com:2196";
}

namespace production {
    const std::string gateway = "gateway.push.apple.com:2195";
    const std::string feedback = "feedback.push.apple.com:2196";
}
}
}
