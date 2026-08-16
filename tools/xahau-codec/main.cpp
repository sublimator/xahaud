//------------------------------------------------------------------------------
// Host codec CLI: encode/decode ST types, dump static server_definitions,
// compute keylets. Does not launch xahaud.
//==============================================================================

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/ServerDefinitions.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <boost/regex.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ripple {

constexpr char const* kSupportedTypes =
    "stobject, amount, currency, issue, iou-value, number,\n"
    "                            ledger-entry-type, transaction-type,\n"
    "                            bridge, pathset, vector256, quality";

static int
doServerDefinitions()
{
    std::cout << Json::StyledWriter().write(getStaticServerDefinitions());
    return EXIT_SUCCESS;
}

static void
requireFullyConsumed(SerialIter const& sit, std::string_view codecType)
{
    if (!sit.empty())
    {
        Throw<std::runtime_error>(
            "trailing bytes after " + std::string(codecType) + ": " +
            std::to_string(sit.getBytesLeft()));
    }
}

static Number
parseNumberFromString(std::string const& amount)
{
    // Digit grammar matches amountFromString; no IOU exponent clamp.
    static boost::regex const reNumber(
        "^"
        "([-+]?)"
        "(0|[1-9][0-9]*)"
        "(\\.([0-9]+))?"
        "([eE]([+-]?)([0-9]+))?"
        "$",
        boost::regex_constants::optimize);

    boost::smatch match;
    if (!boost::regex_match(amount, match, reNumber))
        Throw<std::runtime_error>("Number '" + amount + "' is not valid");

    bool const negative = match[1].matched && (match[1] == "-");
    std::string digits = std::string(match[2]);
    std::int64_t exponent = 0;
    if (match[4].matched)
    {
        digits += match[4];
        if (match[4].length() >
            static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
            Throw<std::overflow_error>("Number exponent overflow");
        exponent -= static_cast<std::int64_t>(match[4].length());
    }
    if (match[5].matched)
    {
        auto const e =
            beast::lexicalCastThrow<std::int64_t>(std::string(match[7]));
        if (match[6].matched && (match[6] == "-"))
        {
            if (exponent < std::numeric_limits<std::int64_t>::min() + e)
                Throw<std::overflow_error>("Number exponent overflow");
            exponent -= e;
        }
        else
        {
            if (exponent > std::numeric_limits<std::int64_t>::max() - e)
                Throw<std::overflow_error>("Number exponent overflow");
            exponent += e;
        }
    }

    auto const firstSignificant = digits.find_first_not_of('0');
    if (firstSignificant == std::string::npos)
        return Number{};
    digits.erase(0, firstSignificant);

    // Reduce arbitrary-length decimal input to Number's 16-digit normalized
    // mantissa in one round-to-nearest-even step. Truncating to an
    // intermediate width first can double-round or ignore a later sticky bit.
    constexpr std::size_t maxDigits = 16;
    if (digits.size() > maxDigits)
    {
        auto const discarded = digits.size() - maxDigits;
        if (discarded > static_cast<std::size_t>(
                            std::numeric_limits<std::int64_t>::max()) ||
            exponent > std::numeric_limits<std::int64_t>::max() -
                    static_cast<std::int64_t>(discarded))
            Throw<std::overflow_error>("Number exponent overflow");
        exponent += static_cast<std::int64_t>(discarded);

        char const guard = digits[maxDigits];
        bool const sticky =
            digits.find_first_not_of('0', maxDigits + 1) != std::string::npos;
        bool const retainedIsOdd = (digits[maxDigits - 1] - '0') % 2 != 0;
        bool const roundUp =
            guard > '5' || (guard == '5' && (sticky || retainedIsOdd));

        digits.resize(maxDigits);
        auto man = beast::lexicalCastThrow<std::int64_t>(digits);
        if (roundUp)
            ++man;
        if (man > Number::maxMantissa)
        {
            man /= 10;
            if (exponent == std::numeric_limits<std::int64_t>::max())
                Throw<std::overflow_error>("Number exponent overflow");
            ++exponent;
        }
        if (negative)
            man = -man;

        if (exponent < std::numeric_limits<int>::min() ||
            exponent > std::numeric_limits<int>::max())
            Throw<std::overflow_error>("Number exponent overflow");
        Number n{man, static_cast<int>(exponent)};
        if (n == Number{})
            Throw<std::runtime_error>("Number '" + amount + "' underflows");
        return n;
    }

    auto man = beast::lexicalCastThrow<std::int64_t>(digits);
    if (negative)
        man = -man;

    if (exponent < std::numeric_limits<int>::min() ||
        exponent > std::numeric_limits<int>::max())
        Throw<std::overflow_error>("Number exponent overflow");
    Number n{man, static_cast<int>(exponent)};
    if (n == Number{})
        Throw<std::runtime_error>("Number '" + amount + "' underflows");
    return n;
}

static Number
numberFromInput(Json::Value const& json, std::string const& raw)
{
    if (json.isString())
        return parseNumberFromString(json.asString());
    if (json.isInt())
        return Number{static_cast<std::int64_t>(json.asInt())};
    if (json.isUInt())
        return Number{static_cast<std::int64_t>(json.asUInt())};
    return parseNumberFromString(raw);
}

static int
doEncode(std::string const& input, std::string const& codecType)
{
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(input, json))
    {
        if (codecType == "stobject")
        {
            std::cerr << "Error: invalid JSON input\n";
            return EXIT_FAILURE;
        }
        // Failed-parse quotes must not become part of the scalar.
        if (input.size() >= 2 && input.front() == '"' && input.back() == '"')
            json = input.substr(1, input.size() - 2);
        else
            json = input;
    }

    try
    {
        if (codecType == "stobject")
        {
            STParsedJSONObject parsed("input", json);
            if (!parsed.object)
            {
                std::cerr << "Error: " << parsed.error.toStyledString();
                return EXIT_FAILURE;
            }
            Serializer s;
            parsed.object->add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "amount")
        {
            auto const amount = amountFromJson(sfGeneric, json);
            Serializer s;
            amount.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "currency")
        {
            auto const currency = currencyFromJson(sfGeneric, json);
            Serializer s;
            currency.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "issue")
        {
            auto const issue = issueFromJson(sfGeneric, json);
            Serializer s;
            issue.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "number")
        {
            auto const value = numberFromInput(json, input);
            Serializer s;
            s.add64(value.mantissa());
            s.add32(value.exponent());
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "iou-value")
        {
            auto const valueStr = json.isString() ? json.asString() : input;
            Json::Value fakeAmount;
            fakeAmount[jss::value] = valueStr;
            fakeAmount[jss::currency] = "USD";
            fakeAmount[jss::issuer] = "rrrrrrrrrrrrrrrrrrrrBZbvji";
            auto const amount = amountFromJson(sfGeneric, fakeAmount);
            Serializer s;
            amount.add(s);
            auto const& data = s.peekData();
            std::cout << strHex(Slice(data.data(), 8)) << "\n";
        }
        else if (codecType == "ledger-entry-type")
        {
            auto const name = json.isString() ? json.asString() : input;
            auto const type =
                LedgerFormats::getInstance().findTypeByName(name);
            Serializer s;
            s.add16(static_cast<std::uint16_t>(type));
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "transaction-type")
        {
            auto const name = json.isString() ? json.asString() : input;
            auto const type = TxFormats::getInstance().findTypeByName(name);
            Serializer s;
            s.add16(static_cast<std::uint16_t>(type));
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "bridge")
        {
            STXChainBridge bridge(sfXChainBridge, json);
            Serializer s;
            bridge.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "pathset")
        {
            Json::Value wrapper = Json::objectValue;
            wrapper["Paths"] = json;
            STParsedJSONObject parsed("pathset", wrapper);
            if (!parsed.object)
                Throw<std::runtime_error>("invalid pathset JSON");
            auto const& paths = parsed.object->getField(sfPaths);
            if (paths.isDefault())
                Throw<std::runtime_error>("empty pathset is not encodable");
            Serializer s;
            paths.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "vector256")
        {
            Json::Value wrapper = Json::objectValue;
            wrapper["Amendments"] = json;
            STParsedJSONObject parsed("vector256", wrapper);
            if (!parsed.object)
                Throw<std::runtime_error>("invalid vector256 JSON");
            auto const& vec = parsed.object->getField(sfAmendments);
            Serializer s;
            vec.add(s);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else if (codecType == "quality")
        {
            auto const out = amountFromJson(sfGeneric, json["out"]);
            auto const in_ = amountFromJson(sfGeneric, json["in"]);
            auto const q = getRate(out, in_);
            Serializer s;
            s.add64(q);
            std::cout << strHex(s.peekData()) << "\n";
        }
        else
        {
            std::cerr << "Error: unknown codec type '" << codecType << "'\n";
            std::cerr << "Supported: " << kSupportedTypes << "\n";
            return EXIT_FAILURE;
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error encoding: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int
doDecode(std::string const& hexInput, std::string const& codecType)
{
    auto const blob = strUnHex(hexInput);
    if (!blob || blob->empty())
    {
        std::cerr << "Error: invalid hex input\n";
        return EXIT_FAILURE;
    }

    try
    {
        SerialIter sit(makeSlice(*blob));

        if (codecType == "stobject")
        {
            STObject obj(sit, sfGeneric);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                obj.getJson(JsonOptions::none));
        }
        else if (codecType == "amount")
        {
            STAmount amount(sit, sfGeneric);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                amount.getJson(JsonOptions::none));
        }
        else if (codecType == "currency")
        {
            STCurrency currency(sit, sfGeneric);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                currency.getJson(JsonOptions::none));
        }
        else if (codecType == "issue")
        {
            STIssue issue(sit, sfGeneric);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                issue.getJson(JsonOptions::none));
        }
        else if (codecType == "number")
        {
            STNumber number(sit, sfGeneric);
            requireFullyConsumed(sit, codecType);
            std::cout << number.getText() << "\n";
        }
        else if (codecType == "ledger-entry-type")
        {
            auto const type = static_cast<LedgerEntryType>(sit.get16());
            requireFullyConsumed(sit, codecType);
            auto const* item = LedgerFormats::getInstance().findByType(type);
            if (!item)
            {
                std::cerr << "Error: unknown ledger entry type "
                          << static_cast<std::uint16_t>(type) << "\n";
                return EXIT_FAILURE;
            }
            std::cout << item->getName() << "\n";
        }
        else if (codecType == "transaction-type")
        {
            auto const type = static_cast<TxType>(sit.get16());
            requireFullyConsumed(sit, codecType);
            auto const* item = TxFormats::getInstance().findByType(type);
            if (!item)
            {
                std::cerr << "Error: unknown transaction type "
                          << static_cast<std::uint16_t>(type) << "\n";
                return EXIT_FAILURE;
            }
            std::cout << item->getName() << "\n";
        }
        else if (codecType == "bridge")
        {
            STXChainBridge bridge(sit, sfXChainBridge);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                bridge.getJson(JsonOptions::none));
        }
        else if (codecType == "pathset")
        {
            STPathSet paths(sit, sfPaths);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                paths.getJson(JsonOptions::none));
        }
        else if (codecType == "vector256")
        {
            STVector256 vec(sit, sfAmendments);
            requireFullyConsumed(sit, codecType);
            std::cout << Json::StyledWriter().write(
                vec.getJson(JsonOptions::none));
        }
        else if (codecType == "quality")
        {
            auto const q = sit.get64();
            requireFullyConsumed(sit, codecType);
            Quality quality{q};
            auto const rate = quality.rate();
            auto const j = rate.getJson(JsonOptions::none);
            std::cout << j[jss::value].asString() << "\n";
        }
        else if (codecType == "iou-value")
        {
            if (blob->size() != 8)
            {
                std::cerr
                    << "Error: iou-value expects exactly 8 bytes of hex\n";
                return EXIT_FAILURE;
            }
            static auto const dummySuffix = []() {
                Json::Value fakeAmount;
                fakeAmount[jss::value] = "1";
                fakeAmount[jss::currency] = "USD";
                fakeAmount[jss::issuer] = "rrrrrrrrrrrrrrrrrrrrBZbvji";
                auto const amount = amountFromJson(sfGeneric, fakeAmount);
                Serializer s;
                amount.add(s);
                auto const& data = s.peekData();
                return Blob(data.begin() + 8, data.end());
            }();

            Blob full = *blob;
            full.insert(full.end(), dummySuffix.begin(), dummySuffix.end());
            SerialIter amountSit(makeSlice(full));
            STAmount amount(amountSit, sfGeneric);
            requireFullyConsumed(amountSit, codecType);
            auto const j = amount.getJson(JsonOptions::none);
            std::cout << j[jss::value].asString() << "\n";
        }
        else
        {
            std::cerr << "Error: unknown codec type '" << codecType << "'\n";
            std::cerr << "Supported: " << kSupportedTypes << "\n";
            return EXIT_FAILURE;
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error decoding: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static std::string
reconstructWireHex(Json::Value const& node)
{
    std::string out;
    if (node.isObject())
    {
        if (node.isMember("header"))
            out += node["header"].asString();
        if (node.isMember("vl"))
            out += node["vl"].asString();
        if (node.isMember("value"))
            out += node["value"].asString();
        if (node.isMember("fields"))
            out += reconstructWireHex(node["fields"]);
        if (node.isMember("elements"))
            out += reconstructWireHex(node["elements"]);
        if (node.isMember("end_marker"))
            out += node["end_marker"].asString();
    }
    else if (node.isArray())
    {
        for (Json::Value const& child : node)
            out += reconstructWireHex(child);
    }
    return out;
}

static Json::Value
withoutStandaloneFieldHeader(Json::Value debug)
{
    if (debug.isObject())
        debug.removeMember("header");
    return debug;
}

static int
outputDebugJson(
    std::string const& codecType,
    Json::Value const& sourceJson,
    std::string const& blobHex,
    Json::Value const& fieldsJson)
{
    Json::Value result(Json::objectValue);
    result["codec_type"] = codecType;
    result["commit"] = BuildInfo::getGitCommitString();
    result["branch"] = BuildInfo::getGitBranch();
    result["json"] = sourceJson;
    result["blob"] = blobHex;
    result["fields"] = fieldsJson.isArray() ? fieldsJson : [&]() {
        Json::Value arr(Json::arrayValue);
        arr.append(fieldsJson);
        return arr;
    }();

    // Materialization can normalize accepted wire: NOPs disappear and
    // STObject fields are ordered. Preserve the accepted bytes in `blob` and
    // expose the materialized representation separately.
    result["canonical_blob"] = reconstructWireHex(result["fields"]);

    // Self-check assertion: all emitted keys must be documented in schema
    static std::unordered_set<std::string> const kKnownKeys = {
        "name", "header", "vl", "value", "fields", "elements", "end_marker", "parts",
        "type", "drops", "mantissa", "exponent", "negative", "currency", "issuer", "zero", "mpt_id"
    };
    std::function<bool(Json::Value const&)> validateKeys = [&](Json::Value const& v) {
        if (v.isObject())
        {
            for (auto const& member : v.getMemberNames())
            {
                if (!kKnownKeys.contains(member))
                {
                    std::cerr << "Error: undocumented JSON key '" << member
                              << "' in debug-json output!\n";
                    return false;
                }
                if (!validateKeys(v[member]))
                    return false;
            }
        }
        else if (v.isArray())
        {
            for (Json::Value const& elem : v)
            {
                if (!validateKeys(elem))
                    return false;
            }
        }
        return true;
    };
    if (!validateKeys(result["fields"]))
        return EXIT_FAILURE;

    std::cout << Json::StyledWriter().write(result);
    return EXIT_SUCCESS;
}

static int
doDebugJson(std::string const& input, std::string const& codecType)
{
    // 1. Try decoding as hex
    auto const blob = strUnHex(input);
    if (blob && !blob->empty())
    {
        try
        {
            SerialIter sit(makeSlice(*blob));
            std::string const blobHex = strHex(makeSlice(*blob));

            if (codecType == "stobject")
            {
                STObject obj(sit, sfGeneric);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    obj.getJson(JsonOptions::none),
                    blobHex,
                    obj.getJsonDebug());
            }
            else if (codecType == "amount")
            {
                STAmount amount(sit, sfGeneric);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    amount.getJson(JsonOptions::none),
                    blobHex,
                    amount.getJsonDebug());
            }
            else if (codecType == "currency")
            {
                STCurrency currency(sit, sfGeneric);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    currency.getJson(JsonOptions::none),
                    blobHex,
                    currency.getJsonDebug());
            }
            else if (codecType == "issue")
            {
                STIssue issue(sit, sfGeneric);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    issue.getJson(JsonOptions::none),
                    blobHex,
                    issue.getJsonDebug());
            }
            else if (codecType == "bridge")
            {
                STXChainBridge bridge(sit, sfXChainBridge);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    bridge.getJson(JsonOptions::none),
                    blobHex,
                    bridge.getJsonDebug());
            }
            else if (codecType == "pathset")
            {
                STPathSet paths(sit, sfPaths);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    paths.getJson(JsonOptions::none),
                    blobHex,
                    withoutStandaloneFieldHeader(paths.getJsonDebug()));
            }
            else if (codecType == "vector256")
            {
                STVector256 vec(sit, sfAmendments);
                requireFullyConsumed(sit, codecType);
                return outputDebugJson(
                    codecType,
                    vec.getJson(JsonOptions::none),
                    blobHex,
                    withoutStandaloneFieldHeader(vec.getJsonDebug()));
            }
            else
            {
                std::cerr << "Error: unknown codec type '" << codecType << "'\n";
                std::cerr << "Supported: " << kSupportedTypes << "\n";
                return EXIT_FAILURE;
            }
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error decoding: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    // 2. Try parsing as JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(input, json))
    {
        if (input.size() >= 2 && input.front() == '"' && input.back() == '"')
            json = input.substr(1, input.size() - 2);
        else
            json = input;
    }
    if (codecType == "amount" && (json.isInt() || json.isUInt()))
    {
        json = std::to_string(json.asUInt());
    }

    try
    {
        if (codecType == "stobject")
        {
            STParsedJSONObject parsed("input", json);
            if (!parsed.object)
            {
                std::cerr << "Error: " << parsed.error.toStyledString();
                return EXIT_FAILURE;
            }
            Serializer s;
            parsed.object->add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                parsed.object->getJsonDebug());
        }
        else if (codecType == "amount")
        {
            auto const amount = amountFromJson(sfGeneric, json);
            Serializer s;
            amount.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                amount.getJsonDebug());
        }
        else if (codecType == "currency")
        {
            auto const currency = currencyFromJson(sfGeneric, json);
            Serializer s;
            currency.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                currency.getJsonDebug());
        }
        else if (codecType == "issue")
        {
            auto const issue = issueFromJson(sfGeneric, json);
            Serializer s;
            issue.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                issue.getJsonDebug());
        }
        else if (codecType == "bridge")
        {
            STXChainBridge bridge(sfGeneric, json);
            Serializer s;
            bridge.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                bridge.getJsonDebug());
        }
        else if (codecType == "pathset")
        {
            Json::Value wrapper = Json::objectValue;
            wrapper["Paths"] = json;
            STParsedJSONObject parsed("pathset", wrapper);
            if (!parsed.object)
                Throw<std::runtime_error>("invalid pathset JSON");
            auto& paths = parsed.object->getField(sfPaths);
            if (paths.isDefault())
                Throw<std::runtime_error>("empty pathset is not encodable");
            Serializer s;
            paths.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                withoutStandaloneFieldHeader(paths.getJsonDebug()));
        }
        else if (codecType == "vector256")
        {
            Json::Value wrapper = Json::objectValue;
            wrapper["Amendments"] = json;
            STParsedJSONObject parsed("vector256", wrapper);
            if (!parsed.object)
                Throw<std::runtime_error>("invalid vector256 JSON");
            auto& vec = parsed.object->getField(sfAmendments);
            Serializer s;
            vec.add(s);
            std::string const blobHex = strHex(s.peekData());
            return outputDebugJson(
                codecType,
                json,
                blobHex,
                withoutStandaloneFieldHeader(vec.getJsonDebug()));
        }
        else
        {
            std::cerr << "Error: unknown codec type '" << codecType << "'\n";
            return EXIT_FAILURE;
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static AccountID
parseAccountID(std::string const& s)
{
    auto const id = parseBase58<AccountID>(s);
    if (!id)
        Throw<std::runtime_error>("invalid account: " + s);
    return *id;
}

static Currency
parseCurrency(std::string const& s)
{
    Currency c;
    if (!to_currency(c, s))
        Throw<std::runtime_error>("invalid currency: " + s);
    return c;
}

static uint256
parseHex256(std::string const& s)
{
    uint256 id;
    if (!id.parseHex(s))
        Throw<std::runtime_error>("invalid hex: " + s);
    return id;
}

static std::uint32_t
parseUInt32(std::string const& s)
{
    std::size_t idx = 0;
    unsigned long n = 0;
    try
    {
        n = std::stoul(s, &idx, 10);
    }
    catch (std::exception const&)
    {
        Throw<std::runtime_error>("invalid unsigned integer: " + s);
    }
    if (idx != s.size() || n > std::numeric_limits<std::uint32_t>::max())
        Throw<std::runtime_error>("invalid unsigned integer: " + s);
    return static_cast<std::uint32_t>(n);
}

static UInt32or256
parseUInt32or256(std::string const& s)
{
    if (s.size() == 64)
    {
        uint256 id;
        if (!id.parseHex(s))
            Throw<std::runtime_error>("invalid 256-bit hex: " + s);
        return id;
    }
    return parseUInt32(s);
}

static Json::Value
parseJsonValue(std::string const& s)
{
    Json::Value v;
    Json::Reader r;
    if (!r.parse(s, v))
        Throw<std::runtime_error>("invalid JSON: " + s);
    return v;
}

static bool
isAllHex(std::string_view s)
{
    return !s.empty() &&
        std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
}

static Blob
parseBlobArg(std::string const& s)
{
    std::string_view v = s;
    bool forceHex = false;
    bool forceRaw = false;
    if (v.starts_with("hex:"))
    {
        forceHex = true;
        v.remove_prefix(4);
    }
    else if (v.starts_with("raw:"))
    {
        forceRaw = true;
        v.remove_prefix(4);
    }

    if (forceRaw)
        return Blob(v.begin(), v.end());

    if (forceHex || ((v.size() % 2) == 0 && isAllHex(v)))
    {
        auto hex = strUnHex(std::string(v));
        if (!hex || (v.size() % 2) != 0)
            Throw<std::runtime_error>("invalid hex blob: " + s);
        return std::move(*hex);
    }
    if (isAllHex(v))
        Throw<std::runtime_error>(
            "odd-length hex blob (use raw: for ASCII): " + s);
    return Blob(v.begin(), v.end());
}

static int
doKeylet(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr
            << "Usage: xahau-codec keylet <type> [args...]\n"
            << "\n"
            << "Computes the 256-bit ledger key for a given object type.\n"
            << "Accounts are base58-encoded (r...), currencies are 3-letter\n"
            << "codes or 40-hex-char codes, sequences are unsigned integers.\n"
            << "\n"
            << "Account-based:\n"
            << "  account <account>                    AccountRoot\n"
            << "  owner_dir <account>                  Owner directory root\n"
            << "  signers <account>                    SignerList\n"
            << "  did <account>                        DID document\n"
            << "  hook <account>                       Hook object\n"
            << "\n"
            << "Account + sequence:\n"
            << "  offer <account> <seq>                Offer\n"
            << "  check <account> <seq>                Check\n"
            << "  escrow <account> <seq>               Escrow\n"
            << "  ticket <account> <seq>               Ticket\n"
            << "  nftoffer <account> <seq>             NFTokenOffer\n"
            << "  oracle <account> <document_id>       Oracle\n"
            << "  permissioned_domain <account> <seq>  PermissionedDomain\n"
            << "\n"
            << "Two accounts:\n"
            << "  deposit_preauth <owner> <authorized> DepositPreauth\n"
            << "\n"
            << "Trust lines & payment channels:\n"
            << "  line <account1> <account2> <currency>  RippleState\n"
            << "  pay_chan <src> <dst> <seq>              PayChannel\n"
            << "\n"
            << "NFT directories:\n"
            << "  nft_buys <nft_id_hex>                Buy offers for NFT\n"
            << "  nft_sells <nft_id_hex>               Sell offers for NFT\n"
            << "\n"
            << "AMM:\n"
            << "  amm <issue1_json> <issue2_json>      AMM instance\n"
            << "    e.g. '{\"currency\":\"XAH\"}' "
               "'{\"currency\":\"USD\",\"issuer\":\"r...\"}'\n"
            << "\n"
            << "MPT:\n"
            << "  mpt_issuance <mptid_hex>             MPTokenIssuance\n"
            << "  mptoken <mptid_hex> <holder>         MPToken\n"
            << "\n"
            << "Hooks / Xahau:\n"
            << "  hook_definition <hash_hex>           HookDefinition\n"
            << "  hook_state <account> <key_hex> <ns_hex>\n"
            << "  hook_state_dir <account> <ns_hex>\n"
            << "  emitted_dir                          Emitted directory\n"
            << "  emitted_txn <id_hex>                 Emitted transaction\n"
            << "  uritoken <issuer> <uri>              URIToken "
               "(uri hex or raw)\n"
            << "  cron <timestamp> [account]           Cron\n"
            << "  import_vlseq <pubkey_hex>            ImportVLSequence\n"
            << "  unl_report                           UNLReport\n"
            << "  credential <subject> <issuer> <type> Credential "
               "(type hex or raw)\n"
            << "\n"
            << "Global singletons (no args):\n"
            << "  amendments                           Amendments\n"
            << "  fees                                 FeeSettings\n"
            << "  negative_unl                         NegativeUNL\n"
            << "\n"
            << "Raw:\n"
            << "  unchecked <key_hex>                  Arbitrary 256-bit key\n"
            << "  skip [ledger_index]                  Skip list\n";
        return EXIT_FAILURE;
    }

    std::string const type = argv[2];
    auto const remaining = argc - 3;
    auto arg = [&](int n) -> std::string {
        if (n >= remaining)
            Throw<std::runtime_error>(type + ": expected more arguments");
        return argv[3 + n];
    };

    try
    {
        Keylet k{ltANY, uint256{}};

        if (type == "account" && remaining == 1)
        {
            k = keylet::account(parseAccountID(arg(0)));
        }
        else if (type == "owner_dir" && remaining == 1)
        {
            k = keylet::ownerDir(parseAccountID(arg(0)));
        }
        else if (type == "line" && remaining == 3)
        {
            k = keylet::line(
                parseAccountID(arg(0)),
                parseAccountID(arg(1)),
                parseCurrency(arg(2)));
        }
        else if (type == "offer" && remaining == 2)
        {
            k = keylet::offer(parseAccountID(arg(0)), parseUInt32or256(arg(1)));
        }
        else if (type == "check" && remaining == 2)
        {
            k = keylet::check(parseAccountID(arg(0)), parseUInt32or256(arg(1)));
        }
        else if (type == "escrow" && remaining == 2)
        {
            k = keylet::escrow(
                parseAccountID(arg(0)), parseUInt32or256(arg(1)));
        }
        else if (type == "pay_chan" && remaining == 3)
        {
            k = keylet::payChan(
                parseAccountID(arg(0)),
                parseAccountID(arg(1)),
                parseUInt32or256(arg(2)));
        }
        else if (type == "signers" && remaining == 1)
        {
            k = keylet::signers(parseAccountID(arg(0)));
        }
        else if (type == "ticket" && remaining == 2)
        {
            k = keylet::ticket(parseAccountID(arg(0)), parseUInt32(arg(1)));
        }
        else if (type == "deposit_preauth" && remaining == 2)
        {
            k = keylet::depositPreauth(
                parseAccountID(arg(0)), parseAccountID(arg(1)));
        }
        else if (type == "did" && remaining == 1)
        {
            k = keylet::did(parseAccountID(arg(0)));
        }
        else if (type == "oracle" && remaining == 2)
        {
            k = keylet::oracle(parseAccountID(arg(0)), parseUInt32(arg(1)));
        }
        else if (type == "nftoffer" && remaining == 2)
        {
            k = keylet::nftoffer(
                parseAccountID(arg(0)), parseUInt32or256(arg(1)));
        }
        else if (type == "nft_buys" && remaining == 1)
        {
            k = keylet::nft_buys(parseHex256(arg(0)));
        }
        else if (type == "nft_sells" && remaining == 1)
        {
            k = keylet::nft_sells(parseHex256(arg(0)));
        }
        else if (type == "amm" && remaining == 2)
        {
            auto const i1 = issueFromJson(
                sfLockingChainIssue, parseJsonValue(arg(0)));
            auto const i2 = issueFromJson(
                sfIssuingChainIssue, parseJsonValue(arg(1)));
            k = keylet::amm(i1.value(), i2.value());
        }
        else if (type == "mpt_issuance" && remaining == 1)
        {
            MPTID id;
            if (!id.parseHex(arg(0)))
                Throw<std::runtime_error>("invalid hex: " + arg(0));
            k = keylet::mptIssuance(id);
        }
        else if (type == "mptoken" && remaining == 2)
        {
            MPTID id;
            if (!id.parseHex(arg(0)))
                Throw<std::runtime_error>("invalid hex: " + arg(0));
            k = keylet::mptoken(id, parseAccountID(arg(1)));
        }
        else if (type == "permissioned_domain" && remaining == 2)
        {
            k = keylet::permissionedDomain(
                parseAccountID(arg(0)), parseUInt32(arg(1)));
        }
        else if (type == "hook" && remaining == 1)
        {
            k = keylet::hook(parseAccountID(arg(0)));
        }
        else if (type == "hook_definition" && remaining == 1)
        {
            k = keylet::hookDefinition(parseHex256(arg(0)));
        }
        else if (type == "hook_state" && remaining == 3)
        {
            k = keylet::hookState(
                parseAccountID(arg(0)),
                parseHex256(arg(1)),
                parseHex256(arg(2)));
        }
        else if (type == "hook_state_dir" && remaining == 2)
        {
            k = keylet::hookStateDir(
                parseAccountID(arg(0)), parseHex256(arg(1)));
        }
        else if (type == "emitted_dir" && remaining == 0)
        {
            k = keylet::emittedDir();
        }
        else if (type == "emitted_txn" && remaining == 1)
        {
            k = keylet::emittedTxn(parseHex256(arg(0)));
        }
        else if (type == "uritoken" && remaining == 2)
        {
            k = keylet::uritoken(parseAccountID(arg(0)), parseBlobArg(arg(1)));
        }
        else if (type == "cron" && (remaining == 1 || remaining == 2))
        {
            std::optional<AccountID> id;
            if (remaining == 2)
                id = parseAccountID(arg(1));
            k = keylet::cron(parseUInt32(arg(0)), id);
        }
        else if (type == "import_vlseq" && remaining == 1)
        {
            auto const raw = strUnHex(arg(0));
            if (!raw || raw->empty())
                Throw<std::runtime_error>("invalid hex: " + arg(0));
            auto const slice = makeSlice(*raw);
            if (!publicKeyType(slice))
                Throw<std::runtime_error>("invalid public key: " + arg(0));
            k = keylet::import_vlseq(PublicKey{slice});
        }
        else if (type == "unl_report" && remaining == 0)
        {
            k = keylet::UNLReport();
        }
        else if (type == "credential" && remaining == 3)
        {
            auto const cred = parseBlobArg(arg(2));
            k = keylet::credential(
                parseAccountID(arg(0)),
                parseAccountID(arg(1)),
                makeSlice(cred));
        }
        else if (type == "amendments" && remaining == 0)
        {
            k = keylet::amendments();
        }
        else if (type == "fees" && remaining == 0)
        {
            k = keylet::fees();
        }
        else if (type == "negative_unl" && remaining == 0)
        {
            k = keylet::negativeUNL();
        }
        else if (type == "unchecked" && remaining == 1)
        {
            k = keylet::unchecked(parseHex256(arg(0)));
        }
        else if (type == "skip" && remaining == 0)
        {
            k = keylet::skip();
        }
        else if (type == "skip" && remaining == 1)
        {
            k = keylet::skip(parseUInt32(arg(0)));
        }
        else
        {
            std::cerr << "Error: unknown keylet type '" << type
                      << "' or wrong number of arguments (" << remaining
                      << ")\n";
            return EXIT_FAILURE;
        }

        std::cout << strHex(k.key) << "\n";
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

std::optional<std::string>
getCodecSource(std::string const& codecType);

std::vector<std::string>
getAvailableCodecSources();

static void
printUsage()
{
    std::cerr
        << "Usage: xahau-codec <command> [options]\n"
        << "\n"
        << "Commands:\n"
        << "  server-definitions        Print static server definitions JSON\n"
        << "  encode <json>             Encode JSON to hex\n"
        << "  decode <hex>              Decode hex to JSON\n"
        << "  debug-json <hex|json>     Decompose hex/JSON into debug fixture JSON\n"
        << "  show-source <type>        Display embedded C++ codec implementation source\n"
        << "  keylet <type> [args...]   Compute keylet hash\n"
        << "\n"
        << "Options:\n"
        << "  --codec-type <type>       Serialization type (default: stobject)\n"
        << "                            Supported: " << kSupportedTypes << "\n"
        << "  --version, -v             Print version and build info\n"
        << "  --version-json            Print version and build info as JSON\n"
        << "\n"
        << "Input can be provided as an argument or via stdin.\n"
        << "server-definitions matches `xahaud --definitions`.\n"
        << "\n"
        << "Examples:\n"
        << "  xahau-codec server-definitions\n"
        << "  xahau-codec show-source stobject\n"
        << "  xahau-codec encode "
           "'{\"Account\": \"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh\"}'\n"
        << "  xahau-codec decode "
           "811439249EE0886DE835D4F4D47DA9D9B1D2AED83C11\n"
        << "  xahau-codec debug-json "
           "811439249EE0886DE835D4F4D47DA9D9B1D2AED83C11\n"
        << "  xahau-codec encode --codec-type amount '\"1000000\"'\n"
        << "  echo '{\"Fee\": \"10\"}' | xahau-codec encode\n";
}

static std::string
readStdin()
{
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

}  // namespace ripple

int
main(int argc, char** argv)
{
    if (argc < 2)
    {
        ripple::printUsage();
        return EXIT_FAILURE;
    }

    std::string const command = argv[1];

    if (command == "--help" || command == "-h")
    {
        ripple::printUsage();
        return EXIT_SUCCESS;
    }

    if (command == "--version" || command == "-v" || command == "version")
    {
        std::cout << "xahau-codec " << ripple::BuildInfo::getVersionString();
        if (!ripple::BuildInfo::getGitCommitHash().empty())
        {
            std::cout << " (commit "
                      << ripple::BuildInfo::getGitCommitString() << ")";
        }
        if (!ripple::BuildInfo::getGitBranch().empty())
        {
            std::cout << " branch " << ripple::BuildInfo::getGitBranch();
        }
        std::cout << std::endl;
        return EXIT_SUCCESS;
    }

    if (command == "--version-json")
    {
        Json::Value v(Json::objectValue);
        v["version"] = ripple::BuildInfo::getVersionString();
        v["full_version"] = ripple::BuildInfo::getFullVersionString();
        v["commit"] = ripple::BuildInfo::getGitCommitString();
        v["commit_hash"] = ripple::BuildInfo::getGitCommitHash();
        v["branch"] = ripple::BuildInfo::getGitBranch();
        v["dirty"] = ripple::BuildInfo::isGitDirty();
        std::cout << Json::StyledWriter().write(v);
        return EXIT_SUCCESS;
    }

    if (command == "show-source")
    {
        if (argc < 3)
        {
            std::cout << "Available show-source topics:\n";
            for (auto const& name : ripple::getAvailableCodecSources())
                std::cout << "  - " << name << "\n";
            std::cout << "\nUsage: xahau-codec show-source <topic>\n";
            return EXIT_SUCCESS;
        }
        std::string const targetType = argv[2];
        auto const src = ripple::getCodecSource(targetType);
        if (!src)
        {
            std::cerr << "Error: no source available for '" << targetType
                      << "'\n";
            std::cerr << "Available topics:\n";
            for (auto const& name : ripple::getAvailableCodecSources())
                std::cerr << "  - " << name << "\n";
            return EXIT_FAILURE;
        }
        std::cout << *src << "\n";
        return EXIT_SUCCESS;
    }

    if (command == "server-definitions")
        return ripple::doServerDefinitions();

    if (command == "keylet")
        return ripple::doKeylet(argc, argv);

    std::string codecType = "stobject";
    std::string input;

    int i = 2;
    while (i < argc)
    {
        std::string const arg = argv[i];
        if (arg == "--codec-type" && i + 1 < argc)
        {
            codecType = argv[i + 1];
            i += 2;
        }
        else if (input.empty())
        {
            input = arg;
            ++i;
        }
        else
        {
            std::cerr << "Error: unexpected argument '" << arg << "'\n";
            ripple::printUsage();
            return EXIT_FAILURE;
        }
    }

    if (input.empty())
    {
        input = ripple::readStdin();
        while (!input.empty() &&
               (input.back() == '\n' || input.back() == '\r' ||
                input.back() == ' '))
        {
            input.pop_back();
        }
    }

    if (input.empty())
    {
        std::cerr << "Error: no input provided\n";
        ripple::printUsage();
        return EXIT_FAILURE;
    }

    if (command == "encode")
        return ripple::doEncode(input, codecType);
    if (command == "decode")
        return ripple::doDecode(input, codecType);
    if (command == "debug-json")
        return ripple::doDebugJson(input, codecType);

    std::cerr << "Error: unknown command '" << command << "'\n";
    ripple::printUsage();
    return EXIT_FAILURE;
}
