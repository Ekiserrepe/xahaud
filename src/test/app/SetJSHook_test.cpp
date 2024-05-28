//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 XRPL Labs

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
#include <ripple/app/hook/Enum.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/tx/impl/SetHook.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/app/SetJSHook_wasm.h>
#include <test/jtx.h>
#include <test/jtx/hook.h>
#include <unordered_map>

// DA TODO: Move duplicated functions to jtx
// JSSMap
// JSSHasher
// JSSEq
// overrideFlag

namespace ripple {

namespace test {

#define DEBUG_TESTS 1

using TestHook = std::vector<uint8_t> const&;

using JSSMap =
    std::unordered_map<Json::StaticString, Json::Value, JSSHasher, JSSEq>;

// Identical to BEAST_EXPECT except it returns from the function
// if the condition isn't met (and would otherwise therefore cause a crash)
#define BEAST_REQUIRE(x)     \
    {                        \
        BEAST_EXPECT(!!(x)); \
        if (!(x))            \
            return;          \
    }

#define HASH_WASM(x)                                                           \
    uint256 const x##_hash =                                                   \
        ripple::sha512Half_s(ripple::Slice(x##_wasm.data(), x##_wasm.size())); \
    std::string const x##_hash_str = to_string(x##_hash);                      \
    Keylet const x##_keylet = keylet::hookDefinition(x##_hash);

class SetJSHook_test : public beast::unit_test::suite
{
private:
    // helper
    void static overrideFlag(Json::Value& jv)
    {
        jv[jss::Flags] = hsfOVERRIDE;
    }

public:
// This is a large fee, large enough that we can set most small test hooks
// without running into fee issues we only want to test fee code specifically in
// fee unit tests, the rest of the time we want to ignore it.
#define HSFEE fee(100'000'000)
#define M(m) memo(m, "", "")
    void
    testHooksOwnerDir(FeatureBitset features)
    {
        testcase("Test owner directory");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(USD(100000), alice);
        env.close();
        env(pay(gw, alice, USD(10000)));

        for (int i = 1; i < 34; ++i)
        {
            std::string const uri(i, '?');
            env(uritoken::mint(alice, uri));
        }
        env.close();

        env(ripple::test::jtx::hook(
                alice, {{hsov1(accept_wasm, 1, overrideFlag)}}, 0),
            HSFEE,
            ter(tesSUCCESS));
        env.close();

        env(ripple::test::jtx::hook(
                alice, {{hsov1(accept_wasm, 1, overrideFlag)}}, 0),
            HSFEE,
            ter(tesSUCCESS));
        env.close();

        // delete hook
        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};
        Json::Value iv;
        iv[jss::Flags] = 1;
        iv[jss::CreateCode] = "";
        jv[jss::Hooks][0U][jss::Hook] = iv;

        env(jv, HSFEE, ter(tesSUCCESS));
        env.close();
    }

    void
    testHooksDisabled(FeatureBitset features)
    {
        testcase("Check for disabled amendment");
        using namespace jtx;
        Env env{*this, features - featureHooks};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        env(ripple::test::jtx::hook(
                alice, {{hsov1(accept_wasm, 1, overrideFlag)}}, 0),
            M("Hooks Disabled"),
            HSFEE,
            ter(temDISABLED));
    }

    void
    testTxStructure(FeatureBitset features)
    {
        testcase("Checks malformed transactions");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        // Test outer structure

        env(ripple::test::jtx::hook(alice, {}, 0),
            M("Must have a hooks field"),
            HSFEE,
            ter(temMALFORMED));

        env(ripple::test::jtx::hook(alice, {{}}, 0),
            M("Must have a non-empty hooks field"),
            HSFEE,
            ter(temMALFORMED));

        env(ripple::test::jtx::hook(
                alice,
                {{hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1),
                  hsov1(accept_wasm, 1)}},
                0),
            M("Must have fewer than 11 entries"),
            HSFEE,
            ter(temMALFORMED));

        {
            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = jss::SetHook;
            jv[jss::Flags] = 0;
            jv[jss::Hooks] = Json::Value{Json::arrayValue};

            Json::Value iv;
            iv[jss::MemoData] = "DEADBEEF";
            iv[jss::MemoFormat] = "";
            iv[jss::MemoType] = "";
            jv[jss::Hooks][0U][jss::Memo] = iv;
            env(jv,
                M("Hooks Array must contain Hook objects"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }
    }

    void
    testInstall(FeatureBitset features)
    {
        testcase("Checks malformed install operation");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        auto const bob = Account{"bob"};
        env.fund(XRP(10000), bob);

        // create a hook that we can then install
        {
            env(ripple::test::jtx::hook(
                    bob, {{hsov1(accept_wasm, 1), hsov1(rollback_wasm, 1)}}, 0),
                M("First set = tesSUCCESS"),
                HSFEE,
                ter(tesSUCCESS));
        }

        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};

        // can't set api version
        {
            Json::Value iv;
            iv[jss::HookHash] = accept_hash_str;
            iv[jss::HookApiVersion] = 0U;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook Install operation cannot set apiversion"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // can't set non-existent hook
        {
            Json::Value iv;
            iv[jss::HookHash] =
                "DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBE"
                "EF";
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook Install operation cannot set non existent hook hash"),
                HSFEE,
                ter(terNO_HOOK));
            env.close();
        }

        // can set extant hook
        {
            Json::Value iv;
            iv[jss::HookHash] = accept_hash_str;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook Install operation can set extant hook hash"),
                HSFEE,
                ter(tesSUCCESS));
            env.close();
        }

        // can't set extant hook over other hook without override flag
        {
            Json::Value iv;
            iv[jss::HookHash] = rollback_hash_str;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook Install operation can set extant hook hash"),
                HSFEE,
                ter(tecREQUIRES_FLAG));
            env.close();
        }

        // can set extant hook over other hook with override flag
        {
            Json::Value iv;
            iv[jss::HookHash] = rollback_hash_str;
            iv[jss::Flags] = hsfOVERRIDE;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook Install operation can set extant hook hash"),
                HSFEE,
                ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testDelete(FeatureBitset features)
    {
        testcase("Checks malformed delete operation");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};

        // flag required
        {
            Json::Value iv;
            iv[jss::CreateCode] = "";
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook DELETE operation must include hsfOVERRIDE flag"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // invalid flags
        {
            Json::Value iv;
            iv[jss::CreateCode] = "";
            iv[jss::Flags] = "2147483648";
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook DELETE operation must include hsfOVERRIDE flag"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // grants, parameters, hookon, hookapiversion, hooknamespace keys must
        // be absent
        for (auto const& [key, value] : JSSMap{
                 {jss::HookGrants, Json::arrayValue},
                 {jss::HookParameters, Json::arrayValue},
                 {jss::HookOn,
                  "000000000000000000000000000000000000000000000000000000000000"
                  "0000"},
                 {jss::HookApiVersion, "1"},
                 {jss::HookNamespace, to_string(uint256{beast::zero})}})
        {
            Json::Value iv;
            iv[jss::CreateCode] = "";
            iv[key] = value;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook DELETE operation cannot include: grants, params, "
                  "hookon, apiversion, namespace"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // create and delete single hook
        {
            {
                Json::Value jv = ripple::test::jtx::hook(
                    alice, {{hsov1(accept_wasm, 1)}}, 0);
                env(jv, M("Normal accept create"), HSFEE, ter(tesSUCCESS));
                env.close();
            }

            BEAST_REQUIRE(env.le(accept_keylet));

            Json::Value iv;
            iv[jss::CreateCode] = "";
            iv[jss::Flags] = hsfOVERRIDE;
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv, M("Normal hook DELETE"), HSFEE);
            env.close();

            // check to ensure definition is deleted and hooks object too
            auto const def = env.le(accept_keylet);
            auto const hook = env.le(keylet::hook(Account("alice").id()));

            BEAST_EXPECT(!def);
            BEAST_EXPECT(!hook);
        }

        // create four hooks then delete the second last one
        {
            // create
            {
                Json::Value jv = ripple::test::jtx::hook(
                    alice,
                    {{hsov1(accept_wasm, 1),
                      hsov1(makestate_wasm, 1),
                      hsov1(rollback_wasm, 1),
                      hsov1(accept2_wasm, 1)}},
                    0);
                env(jv, M("Create four"), HSFEE, ter(tesSUCCESS));
                env.close();
            }

            // delete third and check
            {
                Json::Value iv;
                iv[jss::CreateCode] = "";
                iv[jss::Flags] = hsfOVERRIDE;
                for (uint8_t i = 0; i < 4; ++i)
                    jv[jss::Hooks][i][jss::Hook] = Json::Value{};
                jv[jss::Hooks][2U][jss::Hook] = iv;

                env(jv, M("Normal hooki DELETE (third pos)"), HSFEE);
                env.close();

                // check the hook definitions are consistent with reference
                // count dropping to zero on the third
                auto const accept_def = env.le(accept_keylet);
                auto const rollback_def = env.le(rollback_keylet);
                auto const makestate_def = env.le(makestate_keylet);
                auto const accept2_def = env.le(accept2_keylet);

                BEAST_REQUIRE(accept_def);
                BEAST_EXPECT(!rollback_def);
                BEAST_REQUIRE(makestate_def);
                BEAST_REQUIRE(accept2_def);

                // check the hooks array is correct
                auto const hook = env.le(keylet::hook(Account("alice").id()));
                BEAST_REQUIRE(hook);

                auto const& hooks = hook->getFieldArray(sfHooks);
                BEAST_REQUIRE(hooks.size() == 4);

                // make sure only the third is deleted
                BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookHash));
                BEAST_REQUIRE(hooks[1].isFieldPresent(sfHookHash));
                BEAST_EXPECT(!hooks[2].isFieldPresent(sfHookHash));
                BEAST_REQUIRE(hooks[3].isFieldPresent(sfHookHash));

                // check hashes on the three remaining
                BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);
                BEAST_EXPECT(
                    hooks[1].getFieldH256(sfHookHash) == makestate_hash);
                BEAST_EXPECT(hooks[3].getFieldH256(sfHookHash) == accept2_hash);
            }

            // delete rest and check
            {
                Json::Value iv;
                iv[jss::CreateCode] = "";
                iv[jss::Flags] = hsfOVERRIDE;
                for (uint8_t i = 0; i < 4; ++i)
                {
                    if (i != 2U)
                        jv[jss::Hooks][i][jss::Hook] = iv;
                    else
                        jv[jss::Hooks][i][jss::Hook] = Json::Value{};
                }

                env(jv,
                    M("Normal hook DELETE (first, second, fourth pos)"),
                    HSFEE);
                env.close();

                // check the hook definitions are consistent with reference
                // count dropping to zero on the third
                auto const accept_def = env.le(accept_keylet);
                auto const rollback_def = env.le(rollback_keylet);
                auto const makestate_def = env.le(makestate_keylet);
                auto const accept2_def = env.le(accept2_keylet);

                BEAST_EXPECT(!accept_def);
                BEAST_EXPECT(!rollback_def);
                BEAST_EXPECT(!makestate_def);
                BEAST_EXPECT(!accept2_def);

                // check the hooks object is gone
                auto const hook = env.le(keylet::hook(Account("alice").id()));
                BEAST_EXPECT(!hook);
            }
        }
    }

    void
    testNSDelete(FeatureBitset features)
    {
        testcase("Checks malformed nsdelete operation");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        auto const bob = Account{"bob"};
        env.fund(XRP(10000), bob);

        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};

        for (auto const& [key, value] : JSSMap{
                 {jss::HookGrants, Json::arrayValue},
                 {jss::HookParameters, Json::arrayValue},
                 {jss::HookOn,
                  "000000000000000000000000000000000000000000000000000000000000"
                  "0000"},
                 {jss::HookApiVersion, "1"},
             })
        {
            Json::Value iv;
            iv[key] = value;
            iv[jss::Flags] = hsfNSDELETE;
            iv[jss::HookNamespace] = to_string(uint256{beast::zero});
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Hook NSDELETE operation cannot include: grants, params, "
                  "hookon, apiversion"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        auto const key = uint256::fromVoid(
            (std::array<uint8_t, 32>{
                 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                 0x00U, 0x00U, 0x00U, 0x00U, 'k',   'e',   'y',   0x00U})
                .data());

        auto const ns = uint256::fromVoid(
            (std::array<uint8_t, 32>{
                 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU})
                .data());

        auto const stateKeylet =
            keylet::hookState(Account("alice").id(), key, ns);

        // create a namespace
        std::string ns_str =
            "CAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFE";
        {
            // create hook
            Json::Value jv =
                ripple::test::jtx::hook(alice, {{hsov1(makestate_wasm, 1)}}, 0);

            jv[jss::Hooks][0U][jss::Hook][jss::HookNamespace] = ns_str;
            env(jv, M("Create makestate hook"), HSFEE, ter(tesSUCCESS));
            env.close();

            // run hook
            env(pay(bob, alice, XRP(1)),
                M("Run create state hook"),
                fee(XRP(1)));
            env.close();

            // check if the hookstate object was created
            auto const hookstate = env.le(stateKeylet);
            BEAST_EXPECT(!!hookstate);

            // check if the value was set correctly
            auto const& data = hookstate->getFieldVL(sfHookStateData);

            BEAST_REQUIRE(data.size() == 6);
            BEAST_EXPECT(
                data[0] == 'v' && data[1] == 'a' && data[2] == 'l' &&
                data[3] == 'u' && data[4] == 'e' && data[5] == '\0');
        }

        // delete the namespace
        {
            Json::Value iv;
            iv[jss::Flags] = hsfNSDELETE;
            iv[jss::HookNamespace] = ns_str;
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv, M("Normal NSDELETE operation"), HSFEE, ter(tesSUCCESS));
            env.close();

            // ensure the hook is still installed
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);

            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() > 0);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == makestate_hash);

            // ensure the directory is gone
            auto const dirKeylet =
                keylet::hookStateDir(Account("alice").id(), ns);
            BEAST_EXPECT(!env.le(dirKeylet));

            // ensure the state object is gone
            BEAST_EXPECT(!env.le(stateKeylet));
        }
    }

    void
    testCreate(FeatureBitset features)
    {
        testcase("Checks malformed create operation");
        using namespace jtx;
        Env env{*this, features};

        auto const bob = Account{"bob"};
        env.fund(XRP(10000), bob);

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        // test normal create and missing override flag
        {
            env(ripple::test::jtx::hook(bob, {{hsov1(accept_wasm, 1)}}, 0),
                M("First set = tesSUCCESS"),
                HSFEE,
                ter(tesSUCCESS));

            env(ripple::test::jtx::hook(bob, {{hsov1(accept_wasm, 1)}}, 0),
                M("Second set = tecREQUIRES_FLAG"),
                HSFEE,
                ter(tecREQUIRES_FLAG));
            env.close();
        }

        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};

        // DA TODO: FAILING
        // // payload too large
        // {
        //     env(ripple::test::jtx::hook(alice, {{hsov1(long_wasm, 1)}}, 0),
        //         M("If CreateCode is present, then it must be less than 64kib"),
        //         HSFEE,
        //         ter(temMALFORMED));
        //     env.close();
        // }

        // namespace missing
        {
            Json::Value iv;
            iv[jss::CreateCode] = strHex(accept_wasm);
            iv[jss::HookApiVersion] = 1U;
            iv[jss::HookOn] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("HSO Create operation must contain namespace"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // api version missing
        {
            Json::Value iv;
            iv[jss::CreateCode] = strHex(accept_wasm);
            iv[jss::HookNamespace] = to_string(uint256{beast::zero});
            iv[jss::HookOn] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("HSO Create operation must contain api version"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // api version wrong
        {
            Json::Value iv;
            iv[jss::CreateCode] = strHex(accept_wasm);
            iv[jss::HookNamespace] = to_string(uint256{beast::zero});
            iv[jss::HookApiVersion] = 2U;
            iv[jss::HookOn] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("HSO Create operation must contain valid api version"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // hookon missing
        {
            Json::Value iv;
            iv[jss::CreateCode] = strHex(accept_wasm);
            iv[jss::HookNamespace] = to_string(uint256{beast::zero});
            iv[jss::HookApiVersion] = 1U;
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("HSO Create operation must contain hookon"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // hook hash present
        {
            Json::Value jv =
                ripple::test::jtx::hook(alice, {{hsov1(accept_wasm, 1)}}, 0);
            Json::Value iv = jv[jss::Hooks][0U];
            iv[jss::Hook][jss::HookHash] = to_string(uint256{beast::zero});
            jv[jss::Hooks][0U] = iv;
            env(jv,
                M("Cannot have both CreateCode and HookHash"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // correctly formed
        {
            Json::Value jv =
                ripple::test::jtx::hook(alice, {{hsov1(accept_wasm, 1)}}, 0);
            env(jv, M("Normal accept"), HSFEE, ter(tesSUCCESS));
            env.close();

            auto const def = env.le(accept_keylet);
            auto const hook = env.le(keylet::hook(Account("alice").id()));

            // check if the hook definition exists
            BEAST_EXPECT(!!def);

            // check if the user account has a hooks object
            BEAST_EXPECT(!!hook);

            // check if the hook is correctly set at position 1
            BEAST_EXPECT(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() > 0);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check if the wasm binary was correctly set
            BEAST_EXPECT(def->isFieldPresent(sfCreateCode));
            auto const& wasm = def->getFieldVL(sfCreateCode);
            auto const wasm_hash =
                sha512Half_s(ripple::Slice(wasm.data(), wasm.size()));
            BEAST_EXPECT(wasm_hash == accept_hash);
        }

        // add a second hook
        {
            Json::Value jv =
                ripple::test::jtx::hook(alice, {{hsov1(accept_wasm, 1)}}, 0);
            Json::Value iv = jv[jss::Hooks][0U];
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = Json::Value{};
            jv[jss::Hooks][1U] = iv;
            env(jv,
                M("Normal accept, second position"),
                HSFEE,
                ter(tesSUCCESS));
            env.close();

            auto const def = env.le(accept_keylet);
            auto const hook = env.le(keylet::hook(Account("alice").id()));

            // check if the hook definition exists
            BEAST_EXPECT(!!def);

            // check if the user account has a hooks object
            BEAST_EXPECT(!!hook);

            // check if the hook is correctly set at position 2
            BEAST_EXPECT(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() > 1);
            BEAST_EXPECT(hooks[1].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[1].getFieldH256(sfHookHash) == accept_hash);

            // check if the reference count was correctly incremented
            BEAST_EXPECT(def->isFieldPresent(sfReferenceCount));
            // two references from alice, one from bob (first test above)
            BEAST_EXPECT(def->getFieldU64(sfReferenceCount) == 3ULL);
        }

        auto const rollback_hash = ripple::sha512Half_s(
            ripple::Slice(rollback_wasm.data(), rollback_wasm.size()));

        // test override
        {
            Json::Value jv =
                ripple::test::jtx::hook(alice, {{hsov1(rollback_wasm, 1)}}, 0);
            jv[jss::Hooks][0U][jss::Hook][jss::Flags] = hsfOVERRIDE;
            env(jv, M("Rollback override"), HSFEE, ter(tesSUCCESS));
            env.close();

            auto const rollback_def = env.le(rollback_keylet);
            auto const accept_def = env.le(accept_keylet);
            auto const hook = env.le(keylet::hook(Account("alice").id()));

            // check if the hook definition exists
            BEAST_EXPECT(rollback_def);
            BEAST_EXPECT(accept_def);

            // check if the user account has a hooks object
            BEAST_EXPECT(hook);

            // check if the hook is correctly set at position 1
            BEAST_EXPECT(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() > 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == rollback_hash);
            BEAST_EXPECT(hooks[1].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[1].getFieldH256(sfHookHash) == accept_hash);

            // check if the wasm binary was correctly set
            BEAST_EXPECT(rollback_def->isFieldPresent(sfCreateCode));
            auto const& wasm = rollback_def->getFieldVL(sfCreateCode);
            auto const wasm_hash =
                sha512Half_s(ripple::Slice(wasm.data(), wasm.size()));
            BEAST_EXPECT(wasm_hash == rollback_hash);

            // check if the reference count was correctly incremented
            BEAST_EXPECT(rollback_def->isFieldPresent(sfReferenceCount));
            BEAST_EXPECT(rollback_def->getFieldU64(sfReferenceCount) == 1ULL);

            // check if the reference count was correctly decremented
            BEAST_EXPECT(accept_def->isFieldPresent(sfReferenceCount));
            BEAST_EXPECT(accept_def->getFieldU64(sfReferenceCount) == 2ULL);
        }
    }

    void
    testUpdate(FeatureBitset features)
    {
        testcase("Checks malformed update operation");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        auto const bob = Account{"bob"};
        env.fund(XRP(10000), bob);

        Json::Value jv;
        jv[jss::Account] = alice.human();
        jv[jss::TransactionType] = jss::SetHook;
        jv[jss::Flags] = 0;
        jv[jss::Hooks] = Json::Value{Json::arrayValue};

        // first create the hook
        {
            Json::Value iv;
            iv[jss::CreateCode] = strHex(accept_wasm);
            iv[jss::HookNamespace] = to_string(uint256{beast::zero});
            iv[jss::HookApiVersion] = 1U;
            iv[jss::HookOn] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            iv[jss::HookParameters] = Json::Value{Json::arrayValue};
            iv[jss::HookParameters][0U] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterName] = "AAAAAAAAAAAA";
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterValue] = "BBBBBB";

            iv[jss::HookParameters][1U] = Json::Value{};
            iv[jss::HookParameters][1U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][1U][jss::HookParameter]
              [jss::HookParameterName] = "CAFE";
            iv[jss::HookParameters][1U][jss::HookParameter]
              [jss::HookParameterValue] = "FACADE";

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv, M("Create accept"), HSFEE, ter(tesSUCCESS));
            env.close();
        }

        // all alice operations below are then updates

        // must not specify override flag
        {
            Json::Value iv;
            iv[jss::Flags] = hsfOVERRIDE;
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("Override flag not allowed on update"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // must not specify NSDELETE unless also Namespace
        {
            Json::Value iv;
            iv[jss::Flags] = hsfNSDELETE;
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("NSDELETE flag not allowed on update unless HookNamespace "
                  "also present"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // api version not allowed in update
        {
            Json::Value iv;
            iv[jss::HookApiVersion] = 1U;
            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;

            env(jv,
                M("ApiVersion not allowed in update"),
                HSFEE,
                ter(temMALFORMED));
            env.close();
        }

        // try individually updating the various allowed fields
        {
            Json::Value params{Json::arrayValue};
            params[0U][jss::HookParameter] = Json::Value{};
            params[0U][jss::HookParameter][jss::HookParameterName] = "CAFE";
            params[0U][jss::HookParameter][jss::HookParameterValue] = "BABE";

            Json::Value grants{Json::arrayValue};
            grants[0U][jss::HookGrant] = Json::Value{};
            grants[0U][jss::HookGrant][jss::HookHash] = accept_hash_str;

            for (auto const& [key, value] : JSSMap{
                     {jss::HookOn,
                      "00000000000000000000000000000000000000000000000000000000"
                      "00000001"},
                     {jss::HookNamespace,
                      "CAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFE"
                      "CAFECAFE"},
                     {jss::HookParameters, params},
                     {jss::HookGrants, grants}})
            {
                Json::Value iv;
                iv[key] = value;
                jv[jss::Hooks][0U] = Json::Value{};
                jv[jss::Hooks][0U][jss::Hook] = iv;

                env(jv, M("Normal update"), HSFEE, ter(tesSUCCESS));
                env.close();
            }

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check all fields were updated to correct values
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookOn));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookOn) == UINT256_BIT[0]);

            auto const ns = uint256::fromVoid(
                (std::array<uint8_t, 32>{
                     0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                     0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                     0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU,
                     0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU, 0xCAU, 0xFEU})
                    .data());
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookNamespace));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookNamespace) == ns);

            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookParameters));
            const auto& p = hooks[0].getFieldArray(sfHookParameters);
            BEAST_REQUIRE(p.size() == 1);
            BEAST_REQUIRE(p[0].isFieldPresent(sfHookParameterName));
            BEAST_REQUIRE(p[0].isFieldPresent(sfHookParameterValue));

            const auto pn = p[0].getFieldVL(sfHookParameterName);
            BEAST_REQUIRE(pn.size() == 2);
            BEAST_REQUIRE(pn[0] == 0xCAU && pn[1] == 0xFEU);

            const auto pv = p[0].getFieldVL(sfHookParameterValue);
            BEAST_REQUIRE(pv.size() == 2);
            BEAST_REQUIRE(pv[0] == 0xBAU && pv[1] == 0xBEU);

            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookGrants));
            const auto& g = hooks[0].getFieldArray(sfHookGrants);
            BEAST_REQUIRE(g.size() == 1);
            BEAST_REQUIRE(g[0].isFieldPresent(sfHookHash));
            BEAST_REQUIRE(g[0].getFieldH256(sfHookHash) == accept_hash);
        }

        // reset hookon and namespace to defaults
        {
            for (auto const& [key, value] : JSSMap{
                     {jss::HookOn,
                      "00000000000000000000000000000000000000000000000000000000"
                      "00000000"},
                     {jss::HookNamespace, to_string(uint256{beast::zero})}})
            {
                Json::Value iv;
                iv[key] = value;
                jv[jss::Hooks][0U] = Json::Value{};
                jv[jss::Hooks][0U][jss::Hook] = iv;

                env(jv, M("Reset to default"), HSFEE, ter(tesSUCCESS));
                env.close();
            }

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // ensure the two fields are now absent (because they were reset to
            // the defaults on the hook def)
            BEAST_EXPECT(!hooks[0].isFieldPresent(sfHookOn));
            BEAST_EXPECT(!hooks[0].isFieldPresent(sfHookNamespace));
        }

        // add three additional parameters
        std::map<ripple::Blob, ripple::Blob> params{
            {{0xFEU, 0xEDU, 0xFAU, 0xCEU}, {0xF0U, 0x0DU}},
            {{0xA0U}, {0xB0U}},
            {{0xCAU, 0xFEU}, {0xBAU, 0xBEU}},
            {{0xAAU}, {0xBBU, 0xCCU}}};
        {
            Json::Value iv;
            iv[jss::HookParameters] = Json::Value{Json::arrayValue};
            iv[jss::HookParameters][0U] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterName] = "FEEDFACE";
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterValue] = "F00D";

            iv[jss::HookParameters][1U] = Json::Value{};
            iv[jss::HookParameters][1U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][1U][jss::HookParameter]
              [jss::HookParameterName] = "A0";
            iv[jss::HookParameters][1U][jss::HookParameter]
              [jss::HookParameterValue] = "B0";

            iv[jss::HookParameters][2U] = Json::Value{};
            iv[jss::HookParameters][2U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][2U][jss::HookParameter]
              [jss::HookParameterName] = "AA";
            iv[jss::HookParameters][2U][jss::HookParameter]
              [jss::HookParameterValue] = "BBCC";

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv, M("Add three parameters"), HSFEE, ter(tesSUCCESS));
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check all the previous parameters plus the new ones
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookParameters));
            const auto& p = hooks[0].getFieldArray(sfHookParameters);
            BEAST_REQUIRE(p.size() == params.size());

            std::set<ripple::Blob> already;

            for (uint8_t i = 0; i < params.size(); ++i)
            {
                const auto pn = p[i].getFieldVL(sfHookParameterName);
                const auto pv = p[i].getFieldVL(sfHookParameterValue);

                // make sure it's not a duplicate entry
                BEAST_EXPECT(already.find(pn) == already.end());

                // make  sure it exists
                BEAST_EXPECT(params.find(pn) != params.end());

                // make sure the value matches
                BEAST_EXPECT(params[pn] == pv);
                already.emplace(pn);
            }
        }

        // try to reset CAFE parameter to default
        {
            Json::Value iv;
            iv[jss::HookParameters] = Json::Value{Json::arrayValue};
            iv[jss::HookParameters][0U] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterName] = "CAFE";

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Reset cafe param to default using Absent Value"),
                HSFEE,
                ter(tesSUCCESS));
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            params.erase({0xCAU, 0xFEU});

            // check there right number of parameters exist
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookParameters));
            const auto& p = hooks[0].getFieldArray(sfHookParameters);
            BEAST_REQUIRE(p.size() == params.size());

            // and that they still have the expected values and that there are
            // no duplicates
            std::set<ripple::Blob> already;
            for (uint8_t i = 0; i < params.size(); ++i)
            {
                const auto pn = p[i].getFieldVL(sfHookParameterName);
                const auto pv = p[i].getFieldVL(sfHookParameterValue);

                // make sure it's not a duplicate entry
                BEAST_EXPECT(already.find(pn) == already.end());

                // make  sure it exists
                BEAST_EXPECT(params.find(pn) != params.end());

                // make sure the value matches
                BEAST_EXPECT(params[pn] == pv);
                already.emplace(pn);
            }
        }

        // now re-add CAFE parameter but this time as an explicit blank (Empty
        // value)
        {
            Json::Value iv;
            iv[jss::HookParameters] = Json::Value{Json::arrayValue};
            iv[jss::HookParameters][0U] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter] = Json::Value{};
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterName] = "CAFE";
            iv[jss::HookParameters][0U][jss::HookParameter]
              [jss::HookParameterValue] = "";

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv,
                M("Set cafe param to blank using Empty Value"),
                HSFEE,
                ter(tesSUCCESS));
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            params[Blob{0xCAU, 0xFEU}] = Blob{};

            // check there right number of parameters exist
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookParameters));
            const auto& p = hooks[0].getFieldArray(sfHookParameters);
            BEAST_REQUIRE(p.size() == params.size());

            // and that they still have the expected values and that there are
            // no duplicates
            std::set<ripple::Blob> already;
            for (uint8_t i = 0; i < params.size(); ++i)
            {
                const auto pn = p[i].getFieldVL(sfHookParameterName);
                const auto pv = p[i].getFieldVL(sfHookParameterValue);

                // make sure it's not a duplicate entry
                BEAST_EXPECT(already.find(pn) == already.end());

                // make  sure it exists
                BEAST_EXPECT(params.find(pn) != params.end());

                // make sure the value matches
                BEAST_EXPECT(params[pn] == pv);
                already.emplace(pn);
            }
        }

        // try to delete all parameters (reset to defaults) using EMA (Empty
        // Parameters Array)
        {
            Json::Value iv;
            iv[jss::HookParameters] = Json::Value{Json::arrayValue};

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = iv;
            env(jv, M("Unset all params on hook"), HSFEE, ter(tesSUCCESS));
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check there right number of parameters exist
            BEAST_REQUIRE(!hooks[0].isFieldPresent(sfHookParameters));
        }

        // try to set each type of field on a non existent hook
        {
            Json::Value params{Json::arrayValue};
            params[0U][jss::HookParameter] = Json::Value{};
            params[0U][jss::HookParameter][jss::HookParameterName] = "CAFE";
            params[0U][jss::HookParameter][jss::HookParameterValue] = "BABE";

            Json::Value grants{Json::arrayValue};
            grants[0U][jss::HookGrant] = Json::Value{};
            grants[0U][jss::HookGrant][jss::HookHash] = accept_hash_str;

            for (auto const& [key, value] : JSSMap{
                     {jss::HookOn,
                      "00000000000000000000000000000000000000000000000000000000"
                      "00000001"},
                     {jss::HookNamespace,
                      "CAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFECAFE"
                      "CAFECAFE"},
                     {jss::HookParameters, params},
                     {jss::HookGrants, grants}})
            {
                Json::Value iv;
                iv[key] = value;
                jv[jss::Hooks][0U] = Json::Value{};
                jv[jss::Hooks][0U][jss::Hook] = Json::Value{};
                jv[jss::Hooks][1U] = Json::Value{};
                jv[jss::Hooks][1U][jss::Hook] = iv;

                env(jv,
                    M("Invalid update on non existent hook"),
                    HSFEE,
                    ter(tecNO_ENTRY));
                env.close();
            }

            // ensure hook still exists and that there was no created new entry
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);
            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 1);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);
        }

        // test adding multiple grants
        {
            {
                // add a second hook
                env(ripple::test::jtx::hook(
                        alice, {{{}, hsov1(accept_wasm, 1)}}, 0),
                    M("Add second hook"),
                    HSFEE,
                    ter(tesSUCCESS));
            }

            Json::Value grants{Json::arrayValue};
            grants[0U][jss::HookGrant] = Json::Value{};
            grants[0U][jss::HookGrant][jss::HookHash] = rollback_hash_str;
            grants[0U][jss::HookGrant][jss::Authorize] = bob.human();

            grants[1U][jss::HookGrant] = Json::Value{};
            grants[1U][jss::HookGrant][jss::HookHash] = accept_hash_str;

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = Json::objectValue;
            jv[jss::Hooks][1U] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook][jss::HookGrants] = grants;

            env(jv, M("Add grants"), HSFEE);
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);

            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 2);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check there right number of grants exist
            // hook 0 should have 1 grant
            BEAST_REQUIRE(hooks[0].isFieldPresent(sfHookGrants));
            BEAST_REQUIRE(hooks[0].getFieldArray(sfHookGrants).size() == 1);
            // hook 1 should have 2 grants
            {
                BEAST_REQUIRE(hooks[1].isFieldPresent(sfHookGrants));
                auto const& grants = hooks[1].getFieldArray(sfHookGrants);
                BEAST_REQUIRE(grants.size() == 2);

                BEAST_REQUIRE(grants[0].isFieldPresent(sfHookHash));
                BEAST_REQUIRE(grants[0].isFieldPresent(sfAuthorize));
                BEAST_REQUIRE(grants[1].isFieldPresent(sfHookHash));
                BEAST_EXPECT(!grants[1].isFieldPresent(sfAuthorize));

                BEAST_EXPECT(
                    grants[0].getFieldH256(sfHookHash) == rollback_hash);
                BEAST_EXPECT(grants[0].getAccountID(sfAuthorize) == bob.id());

                BEAST_EXPECT(grants[1].getFieldH256(sfHookHash) == accept_hash);
            }
        }

        // update grants
        {
            Json::Value grants{Json::arrayValue};
            grants[0U][jss::HookGrant] = Json::Value{};
            grants[0U][jss::HookGrant][jss::HookHash] = makestate_hash_str;

            jv[jss::Hooks][0U] = Json::Value{};
            jv[jss::Hooks][0U][jss::Hook] = Json::objectValue;
            jv[jss::Hooks][1U] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook][jss::HookGrants] = grants;

            env(jv, M("update grants"), HSFEE);
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);

            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 2);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check there right number of grants exist
            // hook 1 should have 1 grant
            {
                BEAST_REQUIRE(hooks[1].isFieldPresent(sfHookGrants));
                auto const& grants = hooks[1].getFieldArray(sfHookGrants);
                BEAST_REQUIRE(grants.size() == 1);
                BEAST_REQUIRE(grants[0].isFieldPresent(sfHookHash));
                BEAST_EXPECT(
                    grants[0].getFieldH256(sfHookHash) == makestate_hash);
            }
        }

        // use an empty grants array to reset the grants
        {
            jv[jss::Hooks][0U] = Json::objectValue;
            jv[jss::Hooks][0U][jss::Hook] = Json::objectValue;
            jv[jss::Hooks][1U] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook] = Json::Value{};
            jv[jss::Hooks][1U][jss::Hook][jss::HookGrants] = Json::arrayValue;

            env(jv, M("clear grants"), HSFEE);
            env.close();

            // ensure hook still exists
            auto const hook = env.le(keylet::hook(Account("alice").id()));
            BEAST_REQUIRE(hook);

            BEAST_REQUIRE(hook->isFieldPresent(sfHooks));
            auto const& hooks = hook->getFieldArray(sfHooks);
            BEAST_EXPECT(hooks.size() == 2);
            BEAST_EXPECT(hooks[0].isFieldPresent(sfHookHash));
            BEAST_EXPECT(hooks[0].getFieldH256(sfHookHash) == accept_hash);

            // check there right number of grants exist
            // hook 1 should have 0 grants
            BEAST_REQUIRE(!hooks[1].isFieldPresent(sfHookGrants));
        }
    }

    void
    testWithTickets(FeatureBitset features)
    {
        testcase("with tickets");
        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        std::uint32_t const aliceSeq{env.seq(alice)};
        env.require(owners(alice, 10));

        env(ripple::test::jtx::hook(alice, {{hsov1(accept_wasm, 1)}}, 0),
            HSFEE,
            ticket::use(aliceTicketSeq++),
            ter(tesSUCCESS));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(owners(alice, 9 + 1));
    }

    // void
    // testWasm(FeatureBitset features)
    // {
    //     testcase("Checks malformed hook binaries");
    //     using namespace jtx;
    //     Env env{*this, features};

    //     auto const alice = Account{"alice"};
    //     env.fund(XRP(10000), alice);

    //     env(ripple::test::jtx::hook(alice, {{hsov1(illegalfunc_wasm, 1)}}, 0),
    //         M("Must only contain hook and cbak"),
    //         HSFEE,
    //         ter(temMALFORMED));
    // }

    void
    test_accept(FeatureBitset features)
    {
        testcase("Test accept() hookapi");
        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);

        env(ripple::test::jtx::hook(alice, {{hsov1(accept_wasm, 1)}}, 0),
            M("Install Accept Hook"),
            HSFEE);
        env.close();

        env(pay(bob, alice, XRP(1)), M("Test Accept Hook"), fee(XRP(1)));
        env.close();
    }

    void
    test_rollback(FeatureBitset features)
    {
        testcase("Test rollback() hookapi");
        using namespace jtx;
        Env env{*this, features};

        auto const bob = Account{"bob"};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);

        env(ripple::test::jtx::hook(alice, {{hsov1(rollback_wasm, 1)}}, 0),
            M("Install Rollback Hook"),
            HSFEE);
        env.close();

        env(pay(bob, alice, XRP(1)),
            M("Test Rollback Hook"),
            fee(XRP(1)),
            ter(tecHOOK_REJECTED));
        env.close();
    }

    // void
    // test_emit(FeatureBitset features)

    void
    test_otxn_field(FeatureBitset features)
    {
        testcase("Test otxn_field");
        using namespace jtx;
        Env env{*this, features};

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);

        TestHook hook = jswasm[R"[test.hook](
            const INVALID_ARGUMENT = -7
            const sfAccount = 0x80001

            const ASSERT = (x, code) => {
                if (!x) {
                    rollback(x.toString(), code);
                }
            }

            const Hook = (arg) => {
                ASSERT(otxn_field(sfAccount) == 20);
                ASSERT(otxn_field(1) == INVALID_ARGUMENT);
                
                let acc2 = hook_account();
                ASSERT(acc2 == 20);

                for (var i = 0; i < 20; ++i)
                    ASSERT(acc[i] == acc2[i]);

                return accept("0", 0);
            }
        )[test.hook]"];

        // install the hook on alice
        env(ripple::test::jtx::hook(alice, {{hso(hook, overrideFlag)}}, 0),
            M("set otxn_field"),
            HSFEE);
        env.close();

        // invoke the hook
        env(pay(alice, bob, XRP(1)), M("test otxn_field"), fee(XRP(1)));
    }

    void
    test_hook_account(FeatureBitset features)
    {
        testcase("Test hook_account");
        using namespace jtx;

        auto const test = [&](Account alice) -> void {
            Env env{*this, features};

            // Env env{*this, envconfig(), features, nullptr,
            //     beast::severities::kTrace
            // };

            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice);
            env.fund(XRP(10000), bob);

            TestHook hook = {0x43U, 0x0bU, 0x0cU, 0x41U, 0x53U, 0x53U, 0x45U, 0x52U, 0x54U, 0x08U,
      0x48U, 0x6fU, 0x6fU, 0x6bU, 0x28U, 0x77U, 0x61U, 0x73U, 0x6dU, 0x6aU,
      0x73U, 0x2fU, 0x74U, 0x65U, 0x73U, 0x74U, 0x2dU, 0x31U, 0x2dU, 0x67U,
      0x65U, 0x6eU, 0x2eU, 0x6aU, 0x73U, 0x02U, 0x78U, 0x08U, 0x63U, 0x6fU,
      0x64U, 0x65U, 0x10U, 0x72U, 0x6fU, 0x6cU, 0x6cU, 0x62U, 0x61U, 0x63U,
      0x6bU, 0x06U, 0x61U, 0x72U, 0x67U, 0x08U, 0x61U, 0x63U, 0x63U, 0x32U,
      0x18U, 0x68U, 0x6fU, 0x6fU, 0x6bU, 0x5fU, 0x61U, 0x63U, 0x63U, 0x6fU,
      0x75U, 0x6eU, 0x74U, 0x0aU, 0x74U, 0x72U, 0x61U, 0x63U, 0x65U, 0x0cU,
      0x61U, 0x63U, 0x63U, 0x65U, 0x70U, 0x74U, 0x0cU, 0x00U, 0x06U, 0x00U,
      0xa2U, 0x01U, 0x00U, 0x01U, 0x00U, 0x01U, 0x00U, 0x02U, 0x32U, 0x01U,
      0xa4U, 0x01U, 0x00U, 0x00U, 0x00U, 0x3fU, 0xe3U, 0x00U, 0x00U, 0x00U,
      0x80U, 0x3fU, 0xe4U, 0x00U, 0x00U, 0x00U, 0x80U, 0x3eU, 0xe3U, 0x00U,
      0x00U, 0x00U, 0x80U, 0x3eU, 0xe4U, 0x00U, 0x00U, 0x00U, 0x80U, 0xc2U,
      0x00U, 0x4dU, 0xe3U, 0x00U, 0x00U, 0x00U, 0x3aU, 0xe3U, 0x00U, 0x00U,
      0x00U, 0xc2U, 0x01U, 0x4dU, 0xe4U, 0x00U, 0x00U, 0x00U, 0x3aU, 0xe4U,
      0x00U, 0x00U, 0x00U, 0xc7U, 0x28U, 0xcaU, 0x03U, 0x01U, 0x07U, 0x3dU,
      0x00U, 0x0cU, 0x0cU, 0x00U, 0x0cU, 0x0eU, 0x0cU, 0x02U, 0x06U, 0x00U,
      0x00U, 0x02U, 0x00U, 0x02U, 0x03U, 0x00U, 0x00U, 0x16U, 0x02U, 0xccU,
      0x03U, 0x00U, 0x01U, 0x00U, 0xceU, 0x03U, 0x00U, 0x01U, 0x00U, 0xd3U,
      0x97U, 0xecU, 0x12U, 0x38U, 0xe8U, 0x00U, 0x00U, 0x00U, 0xd3U, 0x42U,
      0x38U, 0x00U, 0x00U, 0x00U, 0x24U, 0x00U, 0x00U, 0xd4U, 0xf2U, 0x0eU,
      0x29U, 0xcaU, 0x03U, 0x02U, 0x03U, 0x03U, 0x17U, 0x59U, 0x0cU, 0x02U,
      0x06U, 0x00U, 0x00U, 0x01U, 0x01U, 0x01U, 0x04U, 0x00U, 0x00U, 0x34U,
      0x02U, 0xd2U, 0x03U, 0x00U, 0x01U, 0x00U, 0xd4U, 0x03U, 0x01U, 0x00U,
      0x20U, 0x61U, 0x00U, 0x00U, 0x38U, 0xebU, 0x00U, 0x00U, 0x00U, 0xf0U,
      0xcbU, 0x38U, 0xecU, 0x00U, 0x00U, 0x00U, 0x04U, 0xeaU, 0x00U, 0x00U,
      0x00U, 0x62U, 0x00U, 0x00U, 0x09U, 0xf3U, 0x0eU, 0x38U, 0xe3U, 0x00U,
      0x00U, 0x00U, 0x62U, 0x00U, 0x00U, 0xebU, 0xbfU, 0x14U, 0xaaU, 0xf1U,
      0x0eU, 0x38U, 0xedU, 0x00U, 0x00U, 0x00U, 0x62U, 0x00U, 0x00U, 0xb7U,
      0x23U, 0x02U, 0x00U, 0xcaU, 0x03U, 0x08U, 0x04U, 0x12U, 0x26U, 0x53U,
      0x49U};

            // TestHook hook = jswasm[
            //     R"[test.hook](
            //     const ASSERT = (x, code) => {
            //     if (!x) {
            //     rollback(x.toString(), code);
            //     }
            //     }

            //     const Hook = (arg) => {
            //     let acc2 = hook_account();
            //     trace("acc2", acc2, false);
            //     ASSERT(acc2.length == 20);
            //     return accept(acc2, 0);
            //     }
            // )[test.hook]"];

            std::cout << "TEST: 1" << "\n";

            // install the hook on alice
            env(ripple::test::jtx::hook(alice, {{hsov1(hook, 1, overrideFlag)}}, 0),
                M("set hook_account"),
                HSFEE);
            env.close();

            std::cout << "TEST: 2" << "\n";

            // invoke the hook
            env(pay(bob, alice, XRP(1)), M("test hook_account"), fee(XRP(1)));

            std::cout << "TEST: 3" << "\n";
            {
                auto meta = env.meta();

                // ensure hook execution occured
                BEAST_REQUIRE(meta);
                BEAST_REQUIRE(meta->isFieldPresent(sfHookExecutions));

                std::cout << "TEST: 3.1" << "\n";

                // ensure there was only one hook execution
                auto const hookExecutions =
                    meta->getFieldArray(sfHookExecutions);
                BEAST_REQUIRE(hookExecutions.size() == 1);

                std::cout << "TEST: 3.2" << "\n";

                // get the data in the return string of the extention
                auto const retStr =
                    hookExecutions[0].getFieldVL(sfHookReturnString);

                std::cout << "TEST: 3.3" << "\n";

                // check that it matches the account id
                BEAST_EXPECT(retStr.size() == 20);
                auto const a = alice.id();
                BEAST_EXPECT(memcmp(retStr.data(), a.data(), 20) == 0);
            }

            std::cout << "TEST: 4" << "\n";

            // install the same hook bob
            env(ripple::test::jtx::hook(bob, {{hsov1(hook, 1, overrideFlag)}}, 0),
                M("set hook_account 2"),
                HSFEE);
            env.close();

            std::cout << "TEST: 5" << "\n";

            // invoke the hook
            env(pay(bob, alice, XRP(1)), M("test hook_account 2"), fee(XRP(1)));

            // there should be two hook executions, the first should be bob's
            // address the second should be alice's
            {
                auto meta = env.meta();

                // ensure hook execution occured
                BEAST_REQUIRE(meta);
                BEAST_REQUIRE(meta->isFieldPresent(sfHookExecutions));

                // ensure there were two hook executions
                auto const hookExecutions =
                    meta->getFieldArray(sfHookExecutions);
                BEAST_REQUIRE(hookExecutions.size() == 2);

                {
                    // get the data in the return string of the extention
                    auto const retStr =
                        hookExecutions[0].getFieldVL(sfHookReturnString);

                    // check that it matches the account id
                    BEAST_EXPECT(retStr.size() == 20);
                    auto const b = bob.id();
                    BEAST_EXPECT(memcmp(retStr.data(), b.data(), 20) == 0);
                }

                {
                    // get the data in the return string of the extention
                    auto const retStr =
                        hookExecutions[1].getFieldVL(sfHookReturnString);

                    // check that it matches the account id
                    BEAST_EXPECT(retStr.size() == 20);
                    auto const a = alice.id();
                    BEAST_EXPECT(memcmp(retStr.data(), a.data(), 20) == 0);
                }
            }
        };

        test(Account{"alice"});
        test(Account{"cho"});
    }

    void
    testWithFeatures(FeatureBitset features)
    {
        // testHooksOwnerDir(features);
        // testHooksDisabled(features);
        // testTxStructure(features);
        // // testInferHookSetOperation(); // Not Version Specific
        // // testParams(features); // Not Version Specific
        // // testGrants(features); // Not Version Specific

        // testInstall(features);
        // testDelete(features);
        // testNSDelete(features);
        // testCreate(features);
        // testUpdate(features);
        // testWithTickets(features);

        // // DA TODO: illegalfunc_wasm
        // // testWasm(features);
        // test_accept(features);
        // test_rollback(features);

        // DA HERE

        // testGuards(features); // Not Used in JSHooks

        // test_emit(features);  //
        // // test_etxn_burden(features);       // tested above
        // // test_etxn_generation(features);   // tested above
        // // test_otxn_burden(features);       // tested above
        // // test_otxn_generation(features);   // tested above
        // test_etxn_details(features);   //
        // test_etxn_fee_base(features);  //
        // test_etxn_nonce(features);     //
        // test_etxn_reserve(features);   //
        // test_fee_base(features);       //

        // test_otxn_field(features);  //

        // test_ledger_keylet(features);  //

        // test_float_compare(features);   //
        // test_float_divide(features);    //
        // test_float_int(features);       //
        // test_float_invert(features);    //
        // test_float_log(features);       //
        // test_float_mantissa(features);  //
        // test_float_mulratio(features);  //
        // test_float_multiply(features);  //
        // test_float_negate(features);    //
        // test_float_one(features);       //
        // test_float_root(features);      //
        // test_float_set(features);       //
        // test_float_sign(features);      //
        // test_float_sto(features);       //
        // test_float_sto_set(features);   //
        // test_float_sum(features);       //

        test_hook_account(features);    //
        // test_hook_again(features);      //
        // test_hook_hash(features);       //
        // test_hook_param(features);      //
        // test_hook_param_set(features);  //
        // test_hook_pos(features);        //
        // test_hook_skip(features);       //

        // test_ledger_last_hash(features);  //
        // test_ledger_last_time(features);  //
        // test_ledger_nonce(features);      //
        // test_ledger_seq(features);        //

        // test_meta_slot(features);  //

        // test_otxn_id(features);     //
        // test_otxn_slot(features);   //
        // test_otxn_type(features);   //
        // test_otxn_param(features);  //

        // test_slot(features);           //
        // test_slot_clear(features);     //
        // test_slot_count(features);     //
        // test_slot_float(features);     //
        // test_slot_set(features);       //
        // test_slot_size(features);      //
        // test_slot_subarray(features);  //
        // test_slot_subfield(features);  //
        // test_slot_type(features);      //

        // test_state(features);                  //
        // test_state_foreign(features);          //
        // test_state_foreign_set(features);      //
        // test_state_foreign_set_max(features);  //
        // test_state_set(features);              //

        // test_sto_emplace(features);   //
        // test_sto_erase(features);     //
        // test_sto_subarray(features);  //
        // test_sto_subfield(features);  //
        // test_sto_validate(features);  //

        // test_trace(features);        //
        // test_trace_float(features);  //
        // test_trace_num(features);    //

        // test_util_accid(features);    //
        // test_util_keylet(features);   //
        // test_util_raddr(features);    //
        // test_util_sha512h(features);  //
        // test_util_verify(features);   //
    }

    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeatures(sa);
    }

private:
    TestHook accept_wasm =  // WASM: 0
        jswasm[
            R"[test.hook](
            const Hook = (arg) => {
            return accept("0", 0);
            }
        )[test.hook]"];

    HASH_WASM(accept);

    TestHook rollback_wasm =  // WASM: 1
        jswasm[
            R"[test.hook](
            const Hook = (arg) => {
            return rollback("0", 0);
            }
        )[test.hook]"];

    HASH_WASM(rollback);

    TestHook illegalfunc_wasm =  // WASM: 3
        jswasm[
            R"[test.hook](
            const Hook = (arg) => {
            console.log("HERE");
            return accept(ret, 0);
            }
        )[test.hook]"];

    TestHook long_wasm =  // WASM: 4
        jswasm[
            R"[test.hook](
            const M_REPEAT_10 = (X) => X.repeat(10);
            const M_REPEAT_100 = (X) => M_REPEAT_10(X).repeat(10);
            const M_REPEAT_1000 = (X) => M_REPEAT_100(X).repeat(10);
            const Hook = (arg) => {
            const ret = M_REPEAT_1000("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz01234567890123");
            return accept(ret, 0);
            }
        )[test.hook]"];

    TestHook makestate_wasm =  // WASM: 5
        jswasm[
            R"[test.hook](
            const Hook = (arg) => {
            const test_key = "0000000000000000000000000000000000000000000000006b657900";
            const test_value = "76616C756500";
            return accept("0", state_set(test_value, test_key));
            }
        )[test.hook]"];

    HASH_WASM(makestate);

    // this is just used as a second small hook with a unique hash
    TestHook accept2_wasm =  // WASM: 6
        jswasm[
            R"[test.hook](
            const Hook = (arg) => {
            return accept("0", 2);
            }
        )[test.hook]"];

    HASH_WASM(accept2);
};
BEAST_DEFINE_TESTSUITE(SetJSHook, app, ripple);
}  // namespace test
}  // namespace ripple
#undef M
