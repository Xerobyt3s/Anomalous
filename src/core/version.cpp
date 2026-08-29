#include "core/version.h"

namespace anom {

Version version()
{
    return Version{0, 1, 0};
}

std::string_view version_string()
{
    return "0.1.0";
}

std::string_view build_config()
{
#if ANOMALOUS_DEBUG
    return "debug";
#else
    return "release";
#endif
}

} // namespace anom
