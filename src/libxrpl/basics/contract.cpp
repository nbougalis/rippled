#include <xrpl/basics/contract.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace xrpl {

namespace detail {

void
logThrow(std::string_view type, std::string_view what)
{
    JLOG(debugLog().warn()) << "Throwing " << type << ": " << what;
}

}  // namespace detail

[[noreturn]] void
logicError(std::string_view msg) noexcept
{
    // LCOV_EXCL_START
    JLOG(debugLog().fatal()) << "LogicError: " << msg;
    std::cerr << "Logic error: " << msg << std::endl;
    // Use a non-standard contract naming here (without namespace) because
    // it's the only location where various unrelated execution paths may
    // register an error; this is also why the "message" parameter is passed
    // here. For the above reasons, we want this contract to stand out.
    UNREACHABLE("LogicError", {{"message", std::string(msg)}});
    std::abort();
    // LCOV_EXCL_STOP
}

}  // namespace xrpl
