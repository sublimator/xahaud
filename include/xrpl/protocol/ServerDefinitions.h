//------------------------------------------------------------------------------
/*
    This file is part of xahaud: https://github.com/Xahau/xahaud
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

#ifndef RIPPLE_PROTOCOL_SERVERDEFINITIONS_H_INCLUDED
#define RIPPLE_PROTOCOL_SERVERDEFINITIONS_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>

namespace ripple {

/** Static `server_definitions` payload (no live amendment table).

    Same JSON as `xahaud --definitions`. The hash field is sha512Half of the
    rest of the object, matching the RPC handler's static half.
*/
Json::Value
getStaticServerDefinitions();

/** Hash of the static definitions object, before features are mixed in. */
uint256 const&
getStaticServerDefinitionsHash();

}  // namespace ripple

#endif
