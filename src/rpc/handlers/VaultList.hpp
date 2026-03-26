#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/JS.hpp"
#include "rpc/common/Modifiers.hpp"
#include "rpc/common/Specs.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/Validators.hpp"

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace rpc {

/**
 * @brief The vault_list command retrieves all Vaults created for a given Token.
 *
 * Given an MPT Issuance ID (token_id), this handler looks up the token issuer and traverses
 * their owner directory to find all Vault ledger objects. For each vault found, a summary is
 * returned containing the vault ID, pseudo account, owner, total assets, total shares, status,
 * and flags.
 */
class VaultListHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLIMIT_MIN = 10;
    static constexpr auto kLIMIT_MAX = 400;
    static constexpr auto kLIMIT_DEFAULT = 200;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string tokenID;
        boost::json::array vaults;
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::optional<std::string> marker;
        uint32_t limit{};
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    struct Input {
        std::string tokenID;
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
        uint32_t limit = kLIMIT_DEFAULT;
        std::optional<std::string> marker;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new VaultListHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    VaultListHandler(std::shared_ptr<BackendInterface> sharedPtrBackend);

    /**
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static RpcSpecConstRef
    spec([[maybe_unused]] uint32_t apiVersion)
    {
        static auto const kRPC_SPEC = RpcSpec{
            {"token_id",
             validation::Required{},
             validation::CustomValidators::uint192HexStringValidator},
            {JS(ledger_hash), validation::CustomValidators::uint256HexStringValidator},
            {JS(ledger_index), validation::CustomValidators::ledgerIndexValidator},
            {JS(limit),
             validation::Type<uint32_t>{},
             validation::Min(1u),
             modifiers::Clamp<int32_t>(kLIMIT_MIN, kLIMIT_MAX)},
            {JS(marker), validation::CustomValidators::accountMarkerValidator},
        };

        return kRPC_SPEC;
    }

    /**
     * @brief Process the VaultList command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    Result
    process(Input const& input, Context const& ctx) const;

private:
    /**
     * @brief Convert the Output to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param output The output to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Output const& output);

    /**
     * @brief Convert a JSON object to Input type
     *
     * @param jv The JSON object to convert
     * @return Input parsed from the JSON object
     */
    friend Input
    tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);
};

}  // namespace rpc
