
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json.h>
#include <xrpl/protocol/STParsedJSON.h>  // STParsedJSONObject

#include <string>

namespace xrpl {

namespace inner_object_formats_unit_test_detail {

struct TestJSONTxt
{
    std::string const txt;
    bool const expectFail;
};

static TestJSONTxt const kTestArray[] = {

    // Valid SignerEntry
    {
        .txt = R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})",
        .expectFail = false,
    },

    // SignerEntry missing Account
    {
        .txt = R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})",
        .expectFail = true,
    },

    // SignerEntry missing SignerWeight
    {
        .txt = R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux"
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})",
        .expectFail = true,
    },

    // SignerEntry with unexpected Amount
    {
        .txt = R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Amount" : "1000000",
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})",
        .expectFail = true,
    },

    // SignerEntry with no Account and unexpected Amount
    {
        .txt = R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Amount" : "10000000",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})",
        .expectFail = true,
    },

};

}  // namespace inner_object_formats_unit_test_detail

class InnerObjectFormatsParsedJSON_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        using namespace inner_object_formats_unit_test_detail;

        // Instantiate a jtx::Env so debugLog writes are exercised.
        test::jtx::Env const env(*this);

        for (auto const& test : kTestArray)
        {
            auto const req = json::load(test.txt);

            if (!req)
            {
                fail("Unable to parse JSON:\n" + test.txt);
                continue;
            }

            STParsedJSONObject const parsed("request", *req);
            bool const noObj = !parsed.object.has_value();
            if (noObj == test.expectFail)
            {
                pass();
            }
            else
            {
                std::string errStr("Unexpected STParsedJSON result on:\n");
                errStr += test.txt;
                fail(errStr);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(InnerObjectFormatsParsedJSON, protocol, xrpl);

}  // namespace xrpl
