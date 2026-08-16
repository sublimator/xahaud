//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/detail/TransactionSign.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/ServerDefinitions.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doServerDefinitions(RPC::JsonContext& context)
{
    auto& params = context.params;

    uint256 reqHash;
    if (params.isMember(jss::hash))
    {
        if (!params[jss::hash].isString() ||
            !reqHash.parseHex(params[jss::hash].asString()))
            return RPC::invalid_field_error(jss::hash);
    }

    uint32_t curLgrSeq = context.ledgerMaster.getValidatedLedger()->info().seq;

    // static values used for cache
    static thread_local uint32_t lastGenerated =
        0;  // last ledger seq it was generated
    static thread_local Json::Value lastFeatures{
        Json::objectValue};  // the actual features JSON last generated
    static thread_local uint256
        lastFeatureHash;  // the hash of the features JSON last time
                          // it was generated

    // if a flag ledger has passed since it was last generated, regenerate it,
    // update the cache above
    if (curLgrSeq > ((lastGenerated >> 8) + 1) << 8 || lastGenerated == 0)
    {
        majorityAmendments_t majorities;
        if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
            majorities = getMajorityAmendments(*valLedger);
        auto& table = context.app.getAmendmentTable();
        bool const isAdmin = true;
        auto features = table.getJson(isAdmin);
        for (auto const& [h, t] : majorities)
            features[to_string(h)][jss::majority] =
                t.time_since_epoch().count();

        lastFeatures = features;
        {
            const std::string out = Json::FastWriter().write(features);
            lastFeatureHash =
                ripple::sha512Half(ripple::Slice{out.data(), out.size()});
        }
        lastGenerated = curLgrSeq;
    }

    // the hash is the xor of the two parts
    uint256 retHash = lastFeatureHash ^ getStaticServerDefinitionsHash();

    if (reqHash == retHash)
    {
        Json::Value jv = Json::objectValue;
        jv[jss::hash] = to_string(retHash);
        return jv;
    }

    // definitions
    Json::Value ret = getStaticServerDefinitions();
    ret[jss::hash] = to_string(retHash);
    ret[jss::features] = lastFeatures;

    return ret;
}

}  // namespace ripple
