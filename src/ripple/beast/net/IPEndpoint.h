//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_NET_IPENDPOINT_H_INCLUDED
#define BEAST_NET_IPENDPOINT_H_INCLUDED

#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/net/IPAddress.h>

#include <boost/optional.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <ios>
#include <string>

namespace beast {
namespace IP {

using Port = std::uint16_t;

/** A version-independent IP address and port combination. */
class Endpoint
{
public:
    /** Create an unspecified endpoint. */
    Endpoint();

    /** Create an endpoint from the address and optional port. */
    explicit Endpoint(boost::asio::ip::address const& addr, Port port = 0);

    /** Create an Endpoint from a string.
        If the port is omitted, the endpoint will have a zero port.
        @return An optional endpoint; will be `boost::none` on failure
    */
    static boost::optional<Endpoint>
    from_string_checked(std::string const& s);
    static Endpoint
    from_string(std::string const& s);

    /** Returns a string representing the endpoint. */
    std::string
    to_string() const;

    /** Returns the port number on the endpoint. */
    Port
    port() const
    {
        return m_port;
    }

    /** Returns a new Endpoint with a different port. */
    Endpoint
    at_port(Port port) const
    {
        return Endpoint(m_addr, port);
    }

    /** Returns the address portion of this endpoint. */
    boost::asio::ip::address const&
    address() const
    {
        return m_addr;
    }

    /** Arithmetic comparison. */
    /** @{ */
    friend bool
    operator==(Endpoint const& lhs, Endpoint const& rhs);
    friend bool
    operator<(Endpoint const& lhs, Endpoint const& rhs);

    friend bool
    operator!=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(lhs == rhs);
    }
    friend bool
    operator>(Endpoint const& lhs, Endpoint const& rhs)
    {
        return rhs < lhs;
    }
    friend bool
    operator<=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(lhs > rhs);
    }
    friend bool
    operator>=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(rhs > lhs);
    }
    /** @} */

    template <class Hasher>
    friend void
    hash_append(Hasher& h, Endpoint const& endpoint)
    {
        using ::beast::hash_append;
        hash_append(h, endpoint.m_addr, endpoint.m_port);
    }

private:
    boost::asio::ip::address m_addr;
    Port m_port;
};

//------------------------------------------------------------------------------

/** Output stream conversion. */
template <typename OutputStream>
OutputStream&
operator<<(OutputStream& os, Endpoint const& endpoint)
{
    os << endpoint.to_string();
    return os;
}

/** Input stream conversion. */
std::istream&
operator>>(std::istream& is, Endpoint& endpoint);

}  // namespace IP
}  // namespace beast

//------------------------------------------------------------------------------

namespace std {
/** std::hash support. */
template <>
struct hash<::beast::IP::Endpoint>
{
    explicit hash() = default;

    std::size_t
    operator()(::beast::IP::Endpoint const& endpoint) const
    {
        return ::beast::uhash<>{}(endpoint);
    }
};

template <>
struct hash<::boost::asio::ip::address_v4>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address_v4 const& address) const
    {
        return ::beast::uhash<>{}(address.to_bytes());
    }
};

template <>
struct hash<::boost::asio::ip::address_v6>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address_v6 const& address) const
    {
        return ::beast::uhash<>{}(address.to_bytes());
    }
};

template <>
struct hash<::boost::asio::ip::address>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address const& address) const
    {
        if (address.is_v4())
            return ::beast::uhash<>{}(address.to_v4());
        return ::beast::uhash<>{}(address.to_v6());
    }
};

template <>
struct hash<::boost::asio::ip::tcp::endpoint>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::tcp::endpoint const& endpoint) const
    {
        std::size_t result = ::beast::uhash<>{}(endpoint.address());
        if (endpoint.port())
            boost::hash_combine(result, 60649 * endpoint.port());
        return result;
    }
};
}  // namespace std

namespace boost {
/** boost::hash support. */
template <>
struct hash<::beast::IP::Endpoint>
{
    explicit hash() = default;

    std::size_t
    operator()(::beast::IP::Endpoint const& endpoint) const
    {
        return ::beast::uhash<>{}(endpoint);
    }
};

template <>
struct hash<::boost::asio::ip::address_v4>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address_v4 const& address) const
    {
        return ::beast::uhash<>{}(address.to_bytes());
    }
};

template <>
struct hash<::boost::asio::ip::address_v6>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address_v6 const& address) const
    {
        return ::beast::uhash<>{}(address.to_bytes());
    }
};

template <>
struct hash<::boost::asio::ip::address>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::address const& address) const
    {
        if (address.is_v4())
            return ::beast::uhash<>{}(address.to_v4());
        return ::beast::uhash<>{}(address.to_v6());
    }
};

template <>
struct hash<::boost::asio::ip::tcp::endpoint>
{
    explicit hash() = default;

    std::size_t
    operator()(::boost::asio::ip::tcp::endpoint const& endpoint) const
    {
        std::size_t result = ::beast::uhash<>{}(endpoint.address());
        if (endpoint.port())
            boost::hash_combine(result, 60649 * endpoint.port());
        return result;
    }
};
}  // namespace boost

#endif
