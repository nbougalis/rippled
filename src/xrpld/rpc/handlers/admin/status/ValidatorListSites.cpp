#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json.h>

namespace xrpl {

json::Value
doValidatorListSites(rpc::JsonContext& context)
{
    return context.app.getValidatorSites().getJson();
}

}  // namespace xrpl
