#pragma once

/**
 * An embedded database wrapper with an intuitive, type-safe interface.
 *
 * This collection of classes let's you access embedded SQLite databases
 * using C++ syntax that is very similar to regular SQL.
 *
 * This module requires the @ref beast_sqlite external module.
 */

#include <soci/blob.h>
#include <soci/session.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <type_traits>

namespace xrpl {

class BasicConfig;

/**
 * Returns the connection string for a database named in the config.
 *
 * @param config Supplies the backend and the database directory.
 * @param name Name of the database. For sqlite it becomes part of the
 *             filename, with the extension chosen from the name.
 *
 * @throws std::runtime_error if the configured backend is not sqlite, or
 *         the name is empty.
 */
[[nodiscard]] std::string
setupSession(BasicConfig const& config, std::string_view name);

/**
 * Opens a soci session.
 *
 * @param s Session to open.
 * @param setup Typically from setupSession(); an empty
 *              string opens a temporary database.
 */
void
openSession(soci::session& s, std::string const& setup);

/**
 * A per-database memory counter that getKBUsedDB() can report.
 *
 * These are the sqlite status codes whose value is a byte count; the counters
 * that report hits, misses or object counts are deliberately absent, since
 * reporting them in kilobytes would be meaningless.
 */
enum class DbMemoryUse { Cache, SharedCache };

/**
 * Returns the memory sqlite has allocated process-wide, in kilobytes.
 */
std::uint32_t
getKBUsedAll();

/**
 * Returns the memory a single database is using, in kilobytes.
 *
 * @throws std::logic_error if the session is not backed by sqlite.
 */
std::uint32_t
getKBUsedDB(soci::session& s, DbMemoryUse use = DbMemoryUse::Cache);

namespace detail {

template <class R>
concept ByteRange = std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    sizeof(std::ranges::range_value_t<R>) == 1 &&
    std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;

template <class C>
concept ByteBuffer = ByteRange<C> && std::ranges::output_range<C, std::ranges::range_value_t<C>> &&
    requires(C& c, std::size_t n) { c.resize(n); };

}  // namespace detail

template <detail::ByteRange R>
void
convert(R const& from, soci::blob& to)
{
    auto const size = std::ranges::size(from);

    if (to.get_len() > size)
        to.trim(size);

    if (size != 0)
    {
#if SOCI_VERSION >= 400100
        to.write_from_start(std::ranges::data(from), size);
#else
        to.write(0, reinterpret_cast<char const*>(std::ranges::data(from)), size);
#endif
    }
}

template <detail::ByteBuffer C>
void
convert(soci::blob& from, C& to)
{
    // resize_and_overwrite invokes its callback with C::value_type*, so
    // op is declared as taking exactly that, so that the requires probe
    // below sees op as a valid argument.
    auto op = [&from](std::ranges::range_value_t<C>* buf, std::size_t n) -> std::size_t {
        if (n == 0)
            return 0;

#if SOCI_VERSION >= 400100
        return from.read_from_start(buf, n);
#else
        return from.read(0, reinterpret_cast<char*>(buf), n);
#endif
    };

    auto const len = from.get_len();

    // ByteBuffer only guarantees that resize is present, but some types
    // offer a better alternative in C++23: resize_and_overwrite. Detect
    // whether it is available and, if it is, use it.
    if constexpr (requires { to.resize_and_overwrite(len, op); })
        to.resize_and_overwrite(len, op);
    else
    {
        to.resize(len);
        op(std::ranges::data(to), len);
    }
}

}  // namespace xrpl
