#pragma once

#include <xrpld/rpc/Role.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/json/json.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/WSSession.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

class WSInfoSub : public InfoSub
{
    std::weak_ptr<WSSession> ws_;
    std::string user_;
    std::string fwdfor_;

public:
    WSInfoSub(Source& source, std::shared_ptr<WSSession> const& ws) : InfoSub(source), ws_(ws)
    {
        auto const& h = ws->request();
        if (ipAllowed(
                beast::IPAddressConversion::fromAsio(ws->remoteEndpoint()).address(),
                ws->port().secureGatewayNetsV4,
                ws->port().secureGatewayNetsV6))
        {
            auto const it = h.find("X-User");
            if (it != h.end())
                user_ = it->value();
            fwdfor_ = std::string(::xrpl::forwardedFor(h));
        }
    }

    [[nodiscard]] std::string_view
    user() const
    {
        return user_;
    }

    [[nodiscard]] std::string_view
    forwardedFor() const
    {
        return fwdfor_;
    }

    void
    send(json::Value const& jv, bool) override
    {
        if (auto sp = ws_.lock())
        {
            sp->send(
                std::make_shared<StreambufWSMsg<boost::beast::multi_buffer>>(
                    json::toMultiBuffer(jv)));
        }
    }
};

}  // namespace xrpl
