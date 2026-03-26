#include "data/Types.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/handlers/VaultList.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace rpc;
using namespace data;
using namespace testing;
namespace json = boost::json;

namespace {

constexpr auto kACCOUNT = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr auto kACCOUNT2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr auto kLEDGER_HASH = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto kINDEX1 = "1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC";
constexpr auto kMPT_ID = "000004C463C52827307480341125DA0577DEFC38405B0E3E";
constexpr auto kASSET_CURRENCY = "USD";
constexpr auto kASSET_ISSUER = "rsA2LpzuawewSBQXkiju3YQTMzW13pAAdW";
constexpr uint32_t kSEQ_START = 10;
constexpr uint32_t kSEQ = 30;
constexpr auto kAPI_VERSION = 2;

}  // namespace

struct RPCVaultListHandlerTest : HandlerBaseTest {
    RPCVaultListHandlerTest()
    {
        backend_->setRange(kSEQ_START, kSEQ);
    }
};

// -- Parameter validation tests --

struct VaultListParamTestCaseBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

struct VaultListParameterTest : RPCVaultListHandlerTest,
                                WithParamInterface<VaultListParamTestCaseBundle> {};

static auto
generateTestValuesForParametersTest()
{
    return std::vector<VaultListParamTestCaseBundle>{
        {
            .testName = "MissingTokenID",
            .testJson = R"JSON({})JSON",
            .expectedError = "invalidParams",
            .expectedErrorMessage = "Required field 'token_id' missing",
        },
        {
            .testName = "TokenIDInvalidFormat",
            .testJson = R"JSON({"token_id": "xxx"})JSON",
            .expectedError = "invalidParams",
            .expectedErrorMessage = "token_idMalformed",
        },
        {
            .testName = "TokenIDNotString",
            .testJson = R"JSON({"token_id": 123})JSON",
            .expectedError = "invalidParams",
            .expectedErrorMessage = "token_idNotString",
        },
        {
            .testName = "NonHexLedgerHash",
            .testJson = fmt::format(
                R"JSON({{
                    "token_id": "{}",
                    "ledger_hash": "xxx"
                }})JSON",
                kMPT_ID
            ),
            .expectedError = "invalidParams",
            .expectedErrorMessage = "ledger_hashMalformed",
        },
        {
            .testName = "NonStringLedgerHash",
            .testJson = fmt::format(
                R"JSON({{
                    "token_id": "{}",
                    "ledger_hash": 123
                }})JSON",
                kMPT_ID
            ),
            .expectedError = "invalidParams",
            .expectedErrorMessage = "ledger_hashNotString",
        },
        {
            .testName = "InvalidLedgerIndexString",
            .testJson = fmt::format(
                R"JSON({{
                    "token_id": "{}",
                    "ledger_index": "notvalidated"
                }})JSON",
                kMPT_ID
            ),
            .expectedError = "invalidParams",
            .expectedErrorMessage = "ledgerIndexMalformed",
        },
    };
}

INSTANTIATE_TEST_CASE_P(
    VaultListGroup,
    VaultListParameterTest,
    ValuesIn(generateTestValuesForParametersTest()),
    tests::util::kNAME_GENERATOR
);

TEST_P(VaultListParameterTest, InvalidParams)
{
    auto const testBundle = VaultListParameterTest::GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{VaultListHandler{backend_}};
        auto const req = json::parse(testBundle.testJson);
        auto const output =
            handler.process(req, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), testBundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), testBundle.expectedErrorMessage);
    });
}

TEST_F(RPCVaultListHandlerTest, LedgerNotFound)
{
    EXPECT_CALL(*backend_, fetchLedgerBySequence)
        .WillOnce(Return(std::optional<ripple::LedgerInfo>{}));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}",
                "ledger_index": "4"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCVaultListHandlerTest, TokenNotFound)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(kMPT_ID)).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _)).WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "objectNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "tokenNotFound");
    });
}

TEST_F(RPCVaultListHandlerTest, IssuerAccountNotFound)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _)).WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "Account not found.");
    });
}

TEST_F(RPCVaultListHandlerTest, EmptyResult)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Owner directory does not exist (no owned objects)
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _)).WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result->as_object().at("vaults").as_array().size(), 0);
        EXPECT_EQ(output.result->as_object().at("token_id").as_string(), kMPT_ID);
    });
}

TEST_F(RPCVaultListHandlerTest, SingleVaultListed)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    // MPT issuance lookup
    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata", 0, 500);
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    // Account lookup
    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Create vault object
    ripple::uint192 const shareMPTID{456};
    ripple::uint256 const prevTxId{2};
    uint32_t const prevTxSeq = 3;
    uint64_t const ownerNode = 4;

    auto const vault = createVault(
        kACCOUNT,
        kACCOUNT2,
        kSEQ,
        kASSET_CURRENCY,
        kASSET_ISSUER,
        shareMPTID,
        ownerNode,
        prevTxId,
        prevTxSeq
    );

    auto const vaultKeylet = ripple::keylet::vault(account, kSEQ).key;

    // Owner directory with one entry (the vault)
    auto const ownerDir = createOwnerDirLedgerObject({vaultKeylet}, ripple::strHex(vaultKeylet));
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _))
        .WillOnce(Return(ownerDir.getSerializer().peekData()));

    // Batch fetch of owned objects returns the vault
    std::vector<Blob> bbs;
    bbs.push_back(vault.getSerializer().peekData());
    EXPECT_CALL(*backend_, doFetchLedgerObjects).WillOnce(Return(bbs));

    // Share MPT issuance lookup (triggered by the vault summary builder)
    auto const shareIssuance = createMptIssuanceObject(kACCOUNT2, kSEQ, std::nullopt, 0, 1000);
    auto const shareIssuanceKk = ripple::keylet::mptIssuance(shareMPTID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(shareIssuanceKk, kSEQ, _))
        .WillOnce(Return(shareIssuance.getSerializer().peekData()));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);

        auto const& result = output.result->as_object();
        EXPECT_EQ(result.at("token_id").as_string(), kMPT_ID);
        EXPECT_EQ(result.at("ledger_index").as_uint64(), kSEQ);
        EXPECT_TRUE(result.at("validated").as_bool());

        auto const& vaults = result.at("vaults").as_array();
        ASSERT_EQ(vaults.size(), 1);

        auto const& vaultSummary = vaults[0].as_object();
        EXPECT_TRUE(vaultSummary.contains("vault_id"));
        EXPECT_EQ(vaultSummary.at("account").as_string(), kACCOUNT2);
        EXPECT_EQ(vaultSummary.at("owner").as_string(), kACCOUNT);
        EXPECT_TRUE(vaultSummary.contains("total_assets"));
        EXPECT_EQ(vaultSummary.at("total_shares").as_uint64(), 1000);
        EXPECT_EQ(vaultSummary.at("status").as_string(), "active");
        EXPECT_EQ(vaultSummary.at("flags").as_uint64(), 0);
    });
}

TEST_F(RPCVaultListHandlerTest, NonVaultObjectsFiltered)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    // MPT issuance lookup
    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    // Account lookup
    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Create a non-vault object (e.g. a RippleState / trust line)
    auto const line = createRippleStateLedgerObject(
        "USD", kASSET_ISSUER, 100, kACCOUNT, 10, kACCOUNT2, 20, kINDEX1, 123, 0
    );

    // Owner directory with one entry (the trust line, NOT a vault)
    auto const ownerDir = createOwnerDirLedgerObject({ripple::uint256{kINDEX1}}, kINDEX1);
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _))
        .WillOnce(Return(ownerDir.getSerializer().peekData()));

    std::vector<Blob> bbs;
    bbs.push_back(line.getSerializer().peekData());
    EXPECT_CALL(*backend_, doFetchLedgerObjects).WillOnce(Return(bbs));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);

        auto const& vaults = output.result->as_object().at("vaults").as_array();
        EXPECT_EQ(vaults.size(), 0);
    });
}

TEST_F(RPCVaultListHandlerTest, MultipleVaultsListed)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    // MPT issuance lookup
    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    // Account lookup
    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Create two vault objects with different share MPT IDs
    ripple::uint192 const shareMPTID1{111};
    ripple::uint192 const shareMPTID2{222};
    ripple::uint256 const prevTxId{2};

    auto const vault1 = createVault(
        kACCOUNT, kACCOUNT2, kSEQ, kASSET_CURRENCY, kASSET_ISSUER, shareMPTID1, 1, prevTxId, 3
    );
    auto const vault2 = createVault(
        kACCOUNT, kACCOUNT2, kSEQ + 1, kASSET_CURRENCY, kASSET_ISSUER, shareMPTID2, 2, prevTxId, 4
    );

    auto const vaultKey1 = ripple::keylet::vault(account, kSEQ).key;
    auto const vaultKey2 = ripple::keylet::vault(account, kSEQ + 1).key;

    // Owner directory with two entries
    auto const ownerDir =
        createOwnerDirLedgerObject({vaultKey1, vaultKey2}, ripple::strHex(vaultKey1));
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _))
        .WillOnce(Return(ownerDir.getSerializer().peekData()));

    // Batch fetch returns both vaults
    std::vector<Blob> bbs;
    bbs.push_back(vault1.getSerializer().peekData());
    bbs.push_back(vault2.getSerializer().peekData());
    EXPECT_CALL(*backend_, doFetchLedgerObjects).WillOnce(Return(bbs));

    // Share MPT issuance lookups for each vault
    auto const shareIssuance1 = createMptIssuanceObject(kACCOUNT2, kSEQ, std::nullopt, 0, 100);
    auto const shareIssuanceKk1 = ripple::keylet::mptIssuance(shareMPTID1).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(shareIssuanceKk1, kSEQ, _))
        .WillOnce(Return(shareIssuance1.getSerializer().peekData()));

    auto const shareIssuance2 = createMptIssuanceObject(kACCOUNT2, kSEQ + 1, std::nullopt, 0, 200);
    auto const shareIssuanceKk2 = ripple::keylet::mptIssuance(shareMPTID2).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(shareIssuanceKk2, kSEQ, _))
        .WillOnce(Return(shareIssuance2.getSerializer().peekData()));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);

        auto const& vaults = output.result->as_object().at("vaults").as_array();
        ASSERT_EQ(vaults.size(), 2);

        // Both should have the expected summary fields
        for (auto const& v : vaults) {
            auto const& obj = v.as_object();
            EXPECT_TRUE(obj.contains("vault_id"));
            EXPECT_TRUE(obj.contains("account"));
            EXPECT_TRUE(obj.contains("owner"));
            EXPECT_TRUE(obj.contains("total_assets"));
            EXPECT_TRUE(obj.contains("total_shares"));
            EXPECT_TRUE(obj.contains("status"));
            EXPECT_TRUE(obj.contains("flags"));
        }

        EXPECT_EQ(vaults[0].as_object().at("total_shares").as_uint64(), 100);
        EXPECT_EQ(vaults[1].as_object().at("total_shares").as_uint64(), 200);
    });
}

TEST_F(RPCVaultListHandlerTest, ShareIssuanceNotFoundFallsBackToZero)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    // MPT issuance lookup
    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    // Account lookup
    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Create vault
    ripple::uint192 const shareMPTID{789};
    auto const vault = createVault(
        kACCOUNT,
        kACCOUNT2,
        kSEQ,
        kASSET_CURRENCY,
        kASSET_ISSUER,
        shareMPTID,
        1,
        ripple::uint256{2},
        3
    );
    auto const vaultKey = ripple::keylet::vault(account, kSEQ).key;

    // Owner directory
    auto const ownerDir = createOwnerDirLedgerObject({vaultKey}, ripple::strHex(vaultKey));
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _))
        .WillOnce(Return(ownerDir.getSerializer().peekData()));

    std::vector<Blob> bbs;
    bbs.push_back(vault.getSerializer().peekData());
    EXPECT_CALL(*backend_, doFetchLedgerObjects).WillOnce(Return(bbs));

    // Share MPT issuance NOT found
    auto const shareIssuanceKk = ripple::keylet::mptIssuance(shareMPTID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(shareIssuanceKk, kSEQ, _))
        .WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);

        auto const& vaults = output.result->as_object().at("vaults").as_array();
        ASSERT_EQ(vaults.size(), 1);
        EXPECT_EQ(vaults[0].as_object().at("total_shares").as_uint64(), 0);
    });
}

TEST_F(RPCVaultListHandlerTest, WithExplicitLedgerIndex)
{
    constexpr uint32_t kEXPLICIT_SEQ = 25;
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kEXPLICIT_SEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence(kEXPLICIT_SEQ, _)).WillOnce(Return(ledgerHeader));

    // MPT issuance lookup
    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kEXPLICIT_SEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kEXPLICIT_SEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Empty owner directory
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kEXPLICIT_SEQ, _))
        .WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}",
                "ledger_index": {}
            }})JSON",
            kMPT_ID,
            kEXPLICIT_SEQ
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result->as_object().at("ledger_index").as_uint64(), kEXPLICIT_SEQ);
    });
}

TEST_F(RPCVaultListHandlerTest, WithLimitParameter)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _)).WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}",
                "limit": 50
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);
        // Limit is clamped to [10, 400], 50 is within range so stays 50
        EXPECT_EQ(output.result->as_object().at("limit").as_uint64(), 50);
    });
}

TEST_F(RPCVaultListHandlerTest, LimitClampedToMax)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _)).WillOnce(Return(std::nullopt));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}",
                "limit": 9999
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);
        // Limit is clamped to max 400
        EXPECT_EQ(output.result->as_object().at("limit").as_uint64(), 400);
    });
}

TEST_F(RPCVaultListHandlerTest, VaultWithNonZeroFlagsStatus)
{
    auto const ledgerHeader = createLedgerHeader(kLEDGER_HASH, kSEQ);
    EXPECT_CALL(*backend_, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));

    auto const mptID = ripple::uint192(kMPT_ID);
    auto const issuance = createMptIssuanceObject(kACCOUNT, kSEQ, "metadata");
    auto const issuanceKk = ripple::keylet::mptIssuance(mptID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(issuanceKk, kSEQ, _))
        .WillOnce(Return(issuance.getSerializer().peekData()));

    auto const account = getAccountIdWithString(kACCOUNT);
    auto const accountKk = ripple::keylet::account(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(accountKk, kSEQ, _))
        .WillOnce(Return(Blob{'f', 'a', 'k', 'e'}));

    // Create a vault with non-zero flags
    ripple::uint192 const shareMPTID{321};
    auto vault = createVault(
        kACCOUNT,
        kACCOUNT2,
        kSEQ,
        kASSET_CURRENCY,
        kASSET_ISSUER,
        shareMPTID,
        1,
        ripple::uint256{2},
        3
    );
    // Override flags to non-zero
    vault.setFieldU32(ripple::sfFlags, 1);

    auto const vaultKey = ripple::keylet::vault(account, kSEQ).key;

    auto const ownerDir = createOwnerDirLedgerObject({vaultKey}, ripple::strHex(vaultKey));
    auto const ownerDirKk = ripple::keylet::ownerDir(account).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(ownerDirKk, kSEQ, _))
        .WillOnce(Return(ownerDir.getSerializer().peekData()));

    std::vector<Blob> bbs;
    bbs.push_back(vault.getSerializer().peekData());
    EXPECT_CALL(*backend_, doFetchLedgerObjects).WillOnce(Return(bbs));

    auto const shareIssuance = createMptIssuanceObject(kACCOUNT2, kSEQ, std::nullopt, 0, 50);
    auto const shareIssuanceKk = ripple::keylet::mptIssuance(shareMPTID).key;
    EXPECT_CALL(*backend_, doFetchLedgerObject(shareIssuanceKk, kSEQ, _))
        .WillOnce(Return(shareIssuance.getSerializer().peekData()));

    auto static const kINPUT = json::parse(
        fmt::format(
            R"JSON({{
                "token_id": "{}"
            }})JSON",
            kMPT_ID
        )
    );

    auto const handler = AnyHandler{VaultListHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output =
            handler.process(kINPUT, Context{.yield = yield, .apiVersion = kAPI_VERSION});
        ASSERT_TRUE(output);

        auto const& vaults = output.result->as_object().at("vaults").as_array();
        ASSERT_EQ(vaults.size(), 1);
        EXPECT_EQ(vaults[0].as_object().at("status").as_string(), "modified");
        EXPECT_EQ(vaults[0].as_object().at("flags").as_uint64(), 1);
    });
}
