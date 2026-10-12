#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * A fixed-size array indexed by a scoped enumeration.
 *
 * This is a thin wrapper around std::array whose subscript operator accepts
 * the enumeration and nothing else: an integer is not accepted, and neither
 * is a different enumeration.
 *
 * @code
 * enum class Color : std::uint8_t { Red, Green, Blue };
 *
 * constexpr EnumArray<char const*, 3, Color> kColorNames{{{"red", "green", "blue"}}};
 *
 * auto const name = kColorNames[Color::Green];
 * @endcode
 *
 * This is an aggregate, and is initialized the way std::array is.
 *
 * The element for an enumerator is the one at the position given by the
 * enumerator's value, so they must range over [0, 1, ... N - 1], but we
 * cannot check or enforce this here; an enumerator with a value of N or
 * more indexes past the end of the array.
 *
 * @tparam T The element type.
 * @tparam N The number of elements.
 * @tparam Enum The scoped enumeration to index by.
 */
template <typename T, std::size_t N, typename Enum>
    requires std::is_scoped_enum_v<Enum>
struct EnumArray
{
    /**
     * The elements, in enumerator order.
     *
     * This is public only so that the class is an aggregate. Use it directly
     * when a position is needed instead of an enumerator.
     */
    std::array<T, N> values;

    /**
     * Access the element for an enumerator.
     *
     * @param e The enumerator.
     * @return A reference to the corresponding element.
     */
    [[nodiscard]] constexpr T&
    operator[](Enum e) noexcept
    {
        if !consteval
        {
            XRPL_ASSERT(
                static_cast<std::size_t>(std::to_underlying(e)) < N,
                "xrpl::EnumArray::operator[] : index in range");
        }

        return values[static_cast<std::size_t>(std::to_underlying(e))];
    }

    /**
     * @copydoc operator[](Enum)
     */
    [[nodiscard]] constexpr T const&
    operator[](Enum e) const noexcept
    {
        if !consteval
        {
            XRPL_ASSERT(
                static_cast<std::size_t>(std::to_underlying(e)) < N,
                "xrpl::EnumArray::operator[] : index in range");
        }

        return values[static_cast<std::size_t>(std::to_underlying(e))];
    }

    /**
     * @return An iterator to the first element.
     */
    [[nodiscard]] constexpr auto
    begin() noexcept
    {
        return values.begin();
    }

    /**
     * @copydoc begin()
     */
    [[nodiscard]] constexpr auto
    begin() const noexcept
    {
        return values.begin();
    }

    /**
     * @return An iterator one past the last element.
     */
    [[nodiscard]] constexpr auto
    end() noexcept
    {
        return values.end();
    }

    /**
     * @copydoc end()
     */
    [[nodiscard]] constexpr auto
    end() const noexcept
    {
        return values.end();
    }

    /**
     * @return The number of elements.
     */
    [[nodiscard]] static constexpr std::size_t
    size() noexcept
    {
        return N;
    }
};

}  // namespace xrpl
