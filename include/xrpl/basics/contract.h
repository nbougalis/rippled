#pragma once

#include <xrpl/basics/sanitizers.h>

#include <boost/core/type_name.hpp>

#include <concepts>
#include <exception>
#include <string_view>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace detail {

/**
 * Logs the type and message of an exception that is about to be thrown.
 *
 * @param type The demangled name of the exception type.
 * @param what The exception message.
 */
void
logThrow(std::string_view type, std::string_view what);

}  // namespace detail

/**
 * Throws an existing exception, logging its type and message before doing so.
 *
 * An lvalue is copied into the thrown object; an rvalue is moved. The
 * thrown object has the static type of @p e (cv- and ref-stripped).
 *
 * This function is usable in constant evaluation: when evaluated at compile
 * time the logging call is skipped and the throw-expression causes the
 * constant evaluation to fail, as intended.
 *
 * ASAN can't handle sudden jumps in control flow very well. This function
 * is marked as XRPL_NO_SANITIZE_ADDRESS to prevent it from triggering
 * false positives, since it throws.
 *
 * @tparam E The exception type, deduced from @p e. Must derive from
 *           std::exception.
 *
 * @param e The exception to throw.
 */
template <class E>
    requires std::derived_from<std::remove_cvref_t<E>, std::exception>
[[noreturn]] XRPL_NO_SANITIZE_ADDRESS constexpr void
Throw(E&& e)
{
    if (!std::is_constant_evaluated())
        detail::logThrow(boost::core::type_name<std::remove_cvref_t<E>>(), e.what());

    throw std::forward<E>(e);
}

/**
 * Constructs an instance of @p E from @p args and delegates to Throw(E&&).
 *
 * @tparam E    The exception type. Must derive from std::exception.
 * @tparam Args Constructor argument types for E.
 *
 * @param args Arguments forwarded to the constructor of E.
 */
template <class E, class... Args>
    requires std::derived_from<E, std::exception>
[[noreturn]] XRPL_NO_SANITIZE_ADDRESS constexpr void
Throw(Args&&... args)
{
    Throw(E(std::forward<Args>(args)...));
}

/**
 * Logs a fatal message and terminates the process unconditionally.
 *
 * Called when faulty logic causes a broken invariant or a condition from
 * which recovery is not possible.
 *
 * @param msg A description of the error.
 */
[[noreturn]] void
logicError(std::string_view msg) noexcept;

}  // namespace xrpl
