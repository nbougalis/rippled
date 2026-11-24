#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

json::Value
doServerInfo(rpc::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    ret[jss::info] = context.netOps.getServerInfo(
        true,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) && context.params[jss::counters].asBool());

    return ret;
}

}  // namespace xrpl
