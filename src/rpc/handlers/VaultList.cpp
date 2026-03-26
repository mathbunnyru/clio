#include "rpc/handlers/VaultList.hpp"

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include "util/Assert.hpp"
#include "util/JsonUtils.hpp"

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

VaultListHandler::VaultListHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
    : sharedPtrBackend_(std::move(sharedPtrBackend))
{
}

VaultListHandler::Result
VaultListHandler::process(VaultListHandler::Input const& input, Context const& ctx) const
{
    auto const range = sharedPtrBackend_->fetchLedgerRange();
    ASSERT(range.has_value(), "VaultList's ledger range must be available");

    auto const expectedLgrInfo = getLedgerHeaderFromHashOrSeq(
        *sharedPtrBackend_, ctx.yield, input.ledgerHash, input.ledgerIndex, range->maxSequence
    );

    if (not expectedLgrInfo.has_value())
        return Error{expectedLgrInfo.error()};

    auto const& lgrInfo = *expectedLgrInfo;

    // Parse the MPT Issuance ID from input
    auto const mptID = ripple::uint192{input.tokenID.c_str()};

    // Fetch the MPT Issuance object to validate it exists and get the issuer
    auto const issuanceKeylet = ripple::keylet::mptIssuance(mptID).key;
    auto const issuanceObject =
        sharedPtrBackend_->fetchLedgerObject(issuanceKeylet, lgrInfo.seq, ctx.yield);

    if (!issuanceObject)
        return Error{Status{RippledError::rpcOBJECT_NOT_FOUND, "tokenNotFound"}};

    ripple::STLedgerEntry const issuanceSle{
        ripple::SerialIter{issuanceObject->data(), issuanceObject->size()}, issuanceKeylet
    };

    auto const issuerAccountID = issuanceSle.getAccountID(ripple::sfIssuer);

    // Verify the issuer account exists
    auto const accountKeylet = ripple::keylet::account(issuerAccountID);
    auto const accountLedgerObject =
        sharedPtrBackend_->fetchLedgerObject(accountKeylet.key, lgrInfo.seq, ctx.yield);

    if (!accountLedgerObject)
        return Error{Status{RippledError::rpcACT_NOT_FOUND}};

    // Traverse owned nodes filtering for Vault objects
    Output response;
    response.tokenID = input.tokenID;
    response.ledgerHash = ripple::strHex(lgrInfo.hash);
    response.ledgerIndex = lgrInfo.seq;
    response.limit = input.limit;

    auto const addToResponse = [&](ripple::SLE&& sle) {
        if (sle.getType() != ripple::ltVAULT)
            return true;

        boost::json::object summary;

        auto vaultSle = std::move(sle);

        summary[JS(vault_id)] = ripple::strHex(vaultSle.key());
        summary[JS(account)] = ripple::toBase58(vaultSle.getAccountID(ripple::sfAccount));
        summary[JS(owner)] = ripple::toBase58(vaultSle.getAccountID(ripple::sfOwner));

        // AssetsTotal is an STNumber field; extract via JSON serialization
        auto const vaultJson = toBoostJson(vaultSle.getJson(ripple::JsonOptions::none));
        if (vaultJson.as_object().contains("AssetsTotal")) {
            summary["total_assets"] = vaultJson.as_object().at("AssetsTotal");
        } else {
            summary["total_assets"] = "0";
        }

        // Fetch the share MPT issuance to get total_shares (OutstandingAmount)
        auto const issuanceKeylet = ripple::keylet::mptIssuance(vaultSle[ripple::sfShareMPTID]).key;
        auto const shareIssuanceObject =
            sharedPtrBackend_->fetchLedgerObject(issuanceKeylet, lgrInfo.seq, ctx.yield);

        if (shareIssuanceObject.has_value()) {
            ripple::STLedgerEntry const shareSle{
                ripple::SerialIter{shareIssuanceObject->data(), shareIssuanceObject->size()},
                issuanceKeylet
            };
            summary["total_shares"] = shareSle.getFieldU64(ripple::sfOutstandingAmount);
        } else {
            summary["total_shares"] = std::uint64_t{0};
        }

        // Status derived from flags
        auto const flags = vaultSle.getFlags();
        // A vault with no special flags set is considered active
        summary[JS(status)] = (flags == 0) ? "active" : "modified";
        summary[JS(flags)] = flags;

        response.vaults.push_back(summary);
        return true;
    };

    auto const expectedNext = traverseOwnedNodes(
        *sharedPtrBackend_,
        issuerAccountID,
        lgrInfo.seq,
        input.limit,
        input.marker,
        ctx.yield,
        addToResponse,
        /* nftIncluded =*/false
    );

    if (not expectedNext.has_value())
        return Error{expectedNext.error()};

    auto const& nextMarker = expectedNext.value();
    if (nextMarker.isNonZero())
        response.marker = nextMarker.toString();

    return response;
}

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    VaultListHandler::Output const& output
)
{
    jv = boost::json::object{
        {"token_id", output.tokenID},
        {JS(ledger_hash), output.ledgerHash},
        {JS(ledger_index), output.ledgerIndex},
        {JS(validated), output.validated},
        {JS(limit), output.limit},
        {"vaults", output.vaults},
    };

    if (output.marker.has_value())
        jv.as_object()[JS(marker)] = *(output.marker);
}

VaultListHandler::Input
tag_invoke(boost::json::value_to_tag<VaultListHandler::Input>, boost::json::value const& jv)
{
    auto input = VaultListHandler::Input{};
    auto const& jsonObject = jv.as_object();

    input.tokenID = boost::json::value_to<std::string>(jsonObject.at("token_id"));

    if (jsonObject.contains(JS(ledger_hash)))
        input.ledgerHash = boost::json::value_to<std::string>(jsonObject.at(JS(ledger_hash)));

    if (jsonObject.contains(JS(ledger_index))) {
        auto const expectedLedgerIndex = util::getLedgerIndex(jsonObject.at(JS(ledger_index)));
        if (expectedLedgerIndex.has_value())
            input.ledgerIndex = *expectedLedgerIndex;
    }

    if (jsonObject.contains(JS(limit)))
        input.limit = util::integralValueAs<uint32_t>(jsonObject.at(JS(limit)));

    if (jsonObject.contains(JS(marker)))
        input.marker = boost::json::value_to<std::string>(jsonObject.at(JS(marker)));

    return input;
}

}  // namespace rpc
