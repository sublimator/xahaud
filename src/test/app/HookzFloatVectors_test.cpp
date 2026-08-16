//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 XRPL-Labs

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
//
// Host-oracle vectors for the hookz Python float model.
//
// Run (from this worktree):
//   x-run-tests -- ripple.app.HookzFloatVectors
//
// Prints one JSON document to stdout (Json::pretty), wrapped in markers:
//   ---HOOKZ_FLOAT_VECTORS_BEGIN---
//   { ... }
//   ---HOOKZ_FLOAT_VECTORS_END---
//
// All integers that can exceed JSON-safe range are emitted as:
//   {"type":"u64"|"i64"|"i32"|"error","val":"<decimal string>"}
// so nothing is rounded through double (Json::UInt is 32-bit here).
//
// Host sources (xahaud:path:line — same checkout as this worktree):
//   limits:     xahaud:src/xrpld/app/hook/HookAPI.h:39-42
//   normalize:  xahaud:src/xrpld/app/hook/HookAPI.h:184-289
//   float_one:  xahaud:src/xrpld/app/hook/HookAPI.h:291-292
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1372-1375
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3684-3686
//   float_set:  xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:986-1005
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3448-3456
//   float_sum:  xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1105-1145
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3552-3565
//   float_compare:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1060-1099
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3531-3549
//   float_multiply:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1008-1021
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2509-2537
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3481-3494
//   float_mulratio:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1023-1048
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2540-2559
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3497-3514
//               xahaud:src/libxrpl/protocol/IOUAmount.cpp:183-315
//   float_divide:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1367-1369
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2562-2636
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3668-3681
//   float_invert:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1356-1364
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3689-3701
//   float_negate:
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1052-1057
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3519-3528
//   float_sto / float_sto_set (host dump still SetHook_test + hookz fixtures;
//   extend this suite when wiring a direct float_sto oracle dump):
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1146-1284
//               xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1287-1350
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3568-3663
//               xahaud:src/test/app/SetHook_test.cpp:6242+
//   invalid-float gate:
//               xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392
//   INVALID_FLOAT / DIVISION_BY_ZERO:
//               xahaud:include/xrpl/hook/Enum.h:362-364
//   prior suite vectors:
//               xahaud:src/test/app/HookAPI_test.cpp:1759-1818 (test_float_set)
//               xahaud:src/test/app/HookAPI_test.cpp:1941-2011 (test_float_sum)
//               xahaud:src/test/app/SetHook_test.cpp:6082-6087 (wasm float_set)
//
// todo:xahaud-bug-candidate (track in hookz ports, do not "fix" quietly):
//   - float_sto_set XRP path skips first amount byte's low 6 man bits
//     (HookAPI.cpp:1329-1340) vs float_sto XRP packing (HookAPI.cpp:1241-1249)
//   - float_sto_set length>8 always attempts header strip; no length==48
//     exemption for bare amount+currency+issuer (HookAPI.cpp:1293-1320)
//   - float_sto XRP shift>15 returns XFL_OVERFLOW (issues/586,
//   HookAPI.cpp:1230)
//   - mulRatio log10Floor *end() UB (IOUAmount.cpp:213-218)
//
// Intended consumers: hooks-testing float residual ports (integer float_sum,
// float_compare, float_multiply, float_divide, and normalize_xfl log10 edges).
//
//==============================================================================

#include <test/jtx.h>
#include <xrpld/app/hook/HookAPI.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

class HookzFloatVectors_test : public beast::unit_test::suite
{
private:
    ApplyContext
    createApplyContext(jtx::Env& env, OpenView& ov, STTx const& tx)
    {
        ApplyContext applyCtx{
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            env.journal};
        return applyCtx;
    }

    /** Typed integer leaf — full precision as a decimal string. */
    static Json::Value
    typed(char const* type, std::string const& val)
    {
        Json::Value v{Json::objectValue};
        v["type"] = type;
        v["val"] = val;
        return v;
    }

    static Json::Value
    u64v(std::uint64_t x)
    {
        return typed("u64", std::to_string(x));
    }

    static Json::Value
    i64v(std::int64_t x)
    {
        return typed("i64", std::to_string(x));
    }

    static Json::Value
    i32v(std::int32_t x)
    {
        return typed("i32", std::to_string(x));
    }

    static Json::Value
    errv(hook::HookReturnCode code)
    {
        // HookReturnCode is a negative int64 constant set; keep as signed
        // decimal string under type "error".
        return typed("error", std::to_string(static_cast<std::int64_t>(code)));
    }

    /** Expected<uint64_t, HookReturnCode> → {ok, value|error} with typed vals.
     */
    static Json::Value
    expectedU64(Expected<std::uint64_t, hook::HookReturnCode> const& r)
    {
        Json::Value o{Json::objectValue};
        // Prefer has_value() over operator bool — some Expected impls differ.
        if (r.has_value())
        {
            o["ok"] = true;
            o["value"] = u64v(*r);
        }
        else
        {
            o["ok"] = false;
            o["error"] = errv(r.error());
        }
        return o;
    }

    /**
     * Wrapper admission: RETURN_IF_INVALID_FLOAT
     * xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392
     *
     * The API *body* assumes admitted floats and calls
     * get_mantissa(...).value(), which throws on Unexpected for negative
     * encodings. Oracle dump must apply the same gate the wasm wrapper does.
     */
    // Returns nullopt if admitted; otherwise INVALID_FLOAT (etc.)
    // Mirrors RETURN_IF_INVALID_FLOAT
    // (xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392).
    static std::optional<hook::HookReturnCode>
    admitFloatError(std::uint64_t raw)
    {
        using enum hook_api::hook_return_code;
        using namespace hook::hook_float;
        auto const asSigned = static_cast<std::int64_t>(raw);
        if (asSigned < 0)
            return INVALID_FLOAT;
        if (raw == 0)
            return std::nullopt;
        auto man = get_mantissa(asSigned);
        if (!man.has_value())
            return man.error();
        auto exp = get_exponent(asSigned);
        if (!exp.has_value())
            return exp.error();
        if (man.value() < minMantissa || man.value() > maxMantissa ||
            exp.value() > maxExponent || exp.value() < minExponent)
            return INVALID_FLOAT;
        return std::nullopt;
    }

public:
    using enum hook_api::hook_return_code;

    void
    dumpVectors()
    {
        using namespace jtx;
        using namespace hook_api;
        using namespace compare_mode;

        try
        {
            auto const alice = Account{"alice"};
            auto const fixedDivideFeatures =
                supported_amendments() | fixFloatDivide;
            Env env{*this, fixedDivideFeatures};
            STTx invokeTx = STTx(ttINVOKE, [&](STObject& obj) {});
            OpenView ov{*env.current()};
            ApplyContext applyCtx = createApplyContext(env, ov, invokeTx);
            auto hookCtx =
                makeStubHookContext(applyCtx, alice.id(), alice.id(), {});
            auto& api = hookCtx.api();
            BEAST_EXPECT(applyCtx.view().rules().enabled(fixFloatDivide));

            Env unfixedDivideEnv{
                *this, supported_amendments() - fixFloatDivide};
            OpenView unfixedDivideOv{*unfixedDivideEnv.current()};
            ApplyContext unfixedDivideApplyCtx =
                createApplyContext(unfixedDivideEnv, unfixedDivideOv, invokeTx);
            auto unfixedDivideHookCtx = makeStubHookContext(
                unfixedDivideApplyCtx, alice.id(), alice.id(), {});
            auto& unfixedDivideApi = unfixedDivideHookCtx.api();
            BEAST_EXPECT(
                !unfixedDivideApplyCtx.view().rules().enabled(fixFloatDivide));

            // xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1372-1375
            // xahaud:src/xrpld/app/hook/HookAPI.h:291-292 (float_one_internal)
            auto const one = api.float_one();
            BEAST_EXPECT(one != 0);

            // ------------------------------------------------------------------
            // float_set — normalize / log10 / bounds / host suite vectors
            // Body:    xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:986-1005
            // Normalize: xahaud:src/xrpld/app/hook/HookAPI.h:184-289
            // Limits:  xahaud:src/xrpld/app/hook/HookAPI.h:39-42
            //   minMantissa=1e15, maxMantissa=9999999999999999
            //   minExponent=-96, maxExponent=80
            // INT64_MIN nudge: xahaud:src/xrpld/app/hook/HookAPI.h:189-190
            // log10 step: xahaud:src/xrpld/app/hook/HookAPI.h:205-206
            // Under/overflow → INVALID_FLOAT:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:997-1002
            // Prior vectors:
            //   xahaud:src/test/app/HookAPI_test.cpp:1759-1818
            //   xahaud:src/test/app/SetHook_test.cpp:6082-6087
            // ------------------------------------------------------------------
            std::vector<std::pair<std::int32_t, std::int64_t>> setCases = {
                {0, 0},
                {-5, 0},
                {50, 0},
                {-97, 1},  // exp < minExponent → INVALID_FLOAT
                {97, 1},   // exp > maxExponent path → INVALID_FLOAT
                {-96, 1},
                {80, 1},
                {0, 1},
                {0, 10},
                {0, 100},
                {0, 500},
                {0, 1000},
                {0, 10000},
                {0, 1'000'000'000'000'000LL},   // minMantissa
                {0, 9'999'999'999'999'999LL},   // maxMantissa
                {0, 10'000'000'000'000'000LL},  // one past maxMantissa
                {0, 9'999'999'999'999'998LL},
                {0, 1'000'000'000'000'001LL},
                // near 2^53 — host log10(double) residual vs digit-count models
                // xahaud:src/xrpld/app/hook/HookAPI.h:205-206
                {0, (1LL << 53) - 1},
                {0, (1LL << 53)},
                {0, (1LL << 53) + 1},
                {0, (1LL << 53) + 2},
                {0, std::numeric_limits<std::int64_t>::max()},
                {0, std::numeric_limits<std::int64_t>::max() - 1},
                {0, -1},
                {0, -100},
                {0, -1'000'000'000'000'000LL},
                {0, std::numeric_limits<std::int64_t>::min() + 1},
                // INT64_MIN nudge — xahaud:src/xrpld/app/hook/HookAPI.h:189-190
                {0, std::numeric_limits<std::int64_t>::min()},
                // host suite vectors —
                // xahaud:src/test/app/HookAPI_test.cpp:1788-1809
                {-5, 6541432897943971LL},
                {-83, 7906202688397446LL},
                {76, 4760131426754533LL},
                {37, -8019384286534438LL},
                {50, 5145342538007840LL},
                {-70, 4387341302202416LL},
                {-26, -1754544005819476LL},
                {36, 8261761545780560LL},
                {35, 7975622850695472LL},
                {17, -4478222822793996LL},
                {-53, 5506604247857835LL},
                {-60, 5120164869507050LL},
                {41, 5176113875683063LL},
                {-54, -3477931844992923LL},
                {21, 6345031894305479LL},
                {-23, 5091583691147091LL},
                {-33, 7509684078851678LL},
                {-72, -1847771838890268LL},
                {71, -9138413713437220LL},
                {28, 4933894067102586LL},
                // == float_one_internal —
                // xahaud:src/xrpld/app/hook/HookAPI.h:291-292
                {-15, 1'000'000'000'000'000LL},
                {0, 1'000'000'000'000'000LL},  // value 1e15, NOT one
            };

            std::cerr << "phase: float_set\n" << std::flush;
            Json::Value floatSet{Json::arrayValue};
            for (auto const& [exp, man] : setCases)
            {
                auto r = api.float_set(exp, man);
                Json::Value row{Json::objectValue};
                row["exp"] = i32v(exp);
                row["man"] = i64v(man);
                row["result"] = expectedU64(r);
                floatSet.append(std::move(row));
                BEAST_EXPECT(true);
            }

            // ------------------------------------------------------------------
            // float_sum / xahauFloatV1 add+subtract oracle
            // Body: xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1105-1145
            //   (IOUAmount += + make_float; zero identity:
            //    xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1107-1110)
            // Live transaction selector:
            //   xahaud:src/xrpld/app/tx/detail/apply.cpp:148
            // Legacy branch and ambient default:
            //   xahaud:src/libxrpl/protocol/IOUAmount.cpp:34-48
            //   xahaud:src/libxrpl/protocol/IOUAmount.cpp:138-173
            // Wrapper admission:
            //   xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3557-3558
            // Negate for cancellation:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1052-1057
            //
            // This direct HookAPI harness is not transaction apply. Its ambient
            // STNumber switchover defaults true, so an unguarded float_sum dump
            // is the Number/ties-even branch rather than the live legacy branch
            // while fixUniversalNumber is disabled. Every accepted xahauFloatV1
            // row is therefore evaluated inside NumberSO{false}. The paired
            // guard row proves deleting that guard changes the last digit.
            // ------------------------------------------------------------------
            enum class ArithmeticOperation {
                add,
                subtract,
            };

            struct ArithmeticCase
            {
                char const* id;
                char const* category;
                ArithmeticOperation operation;
                std::uint64_t a;
                std::uint64_t b;
            };

            // Construct already-canonical operands directly. float_set
            // normalizes through floating log10 and is therefore not an exact
            // constructor at mantissa extrema (for example
            // 9,999,999,999,999,999e80).
            // xahaud:src/xrpld/app/hook/HookAPI.h:146-170
            auto requireCanonicalFloat =
                [&](std::int32_t exponent,
                    std::uint64_t mantissa) -> std::uint64_t {
                auto value =
                    hook::hook_float::make_float(mantissa, exponent, false);
                if (!value.has_value())
                    throw std::runtime_error(
                        "xahauFloatV1 oracle operand is not canonical: " +
                        std::to_string(mantissa) + "e" +
                        std::to_string(exponent) + " error=" +
                        std::to_string(
                            static_cast<std::int64_t>(value.error())));
                return *value;
            };

            auto evaluateArithmetic = [&](ArithmeticCase const& c)
                -> Expected<std::uint64_t, hook::HookReturnCode> {
                if (auto e = admitFloatError(c.a))
                    return Unexpected(*e);
                if (auto e = admitFloatError(c.b))
                    return Unexpected(*e);

                auto rhs = c.b;
                if (c.operation == ArithmeticOperation::subtract)
                    rhs = rhs == 0 ? 0 : api.float_negate(rhs);
                return api.float_sum(c.a, rhs);
            };

            auto const zero = std::uint64_t{0};
            auto const posMinMantissa =
                requireCanonicalFloat(0, 1'000'000'000'000'000ULL);
            auto const negMinMantissa = api.float_negate(posMinMantissa);
            auto const posMaxMantissa =
                requireCanonicalFloat(0, 9'999'999'999'999'999ULL);
            auto const negMaxMantissa = api.float_negate(posMaxMantissa);
            auto const posMinExponent =
                requireCanonicalFloat(-96, 1'000'000'000'000'000ULL);
            auto const negMinExponent = api.float_negate(posMinExponent);
            auto const posMaxExponent =
                requireCanonicalFloat(80, 9'999'999'999'999'999ULL);
            auto const negMaxExponent = api.float_negate(posMaxExponent);
            auto const odd = requireCanonicalFloat(0, 1'000'000'000'000'001ULL);
            auto const negOdd = api.float_negate(odd);
            auto const halfUlp =
                requireCanonicalFloat(-16, 5'000'000'000'000'000ULL);
            auto const sixTenthsUlp =
                requireCanonicalFloat(-16, 6'000'000'000'000'000ULL);
            auto const alignDelta1 =
                requireCanonicalFloat(-1, 5'000'000'000'000'000ULL);
            auto const alignDelta15 =
                requireCanonicalFloat(-15, 5'000'000'000'000'000ULL);
            auto const alignDelta17 =
                requireCanonicalFloat(-17, 5'000'000'000'000'000ULL);
            auto const alignMaxGap =
                requireCanonicalFloat(-96, 5'000'000'000'000'000ULL);
            auto const nearCancelHigh =
                requireCanonicalFloat(0, 1'000'000'000'000'100ULL);
            auto const nearCancelLow =
                requireCanonicalFloat(0, 1'000'000'000'000'000ULL);
            auto const minExpDustHigh =
                requireCanonicalFloat(-96, 1'000'000'000'000'011ULL);
            auto const minExpDustLow =
                requireCanonicalFloat(-96, 1'000'000'000'000'001ULL);
            auto const posLegacyClampTen =
                requireCanonicalFloat(0, 1'000'000'000'000'010ULL);
            auto const negLegacyClampTen = api.float_negate(posLegacyClampTen);

            std::vector<ArithmeticCase> arithmeticCases = {
                {"add.zero-zero",
                 "zero_identity",
                 ArithmeticOperation::add,
                 zero,
                 zero},
                {"add.zero-positive",
                 "zero_identity",
                 ArithmeticOperation::add,
                 zero,
                 posMinMantissa},
                {"add.positive-zero",
                 "zero_identity",
                 ArithmeticOperation::add,
                 posMaxMantissa,
                 zero},
                {"add.zero-negative",
                 "zero_identity",
                 ArithmeticOperation::add,
                 zero,
                 negMinMantissa},
                {"add.negative-zero",
                 "zero_identity",
                 ArithmeticOperation::add,
                 negMaxMantissa,
                 zero},
                {"add.same-exponent-positive",
                 "same_exponent",
                 ArithmeticOperation::add,
                 posMinMantissa,
                 odd},
                {"add.same-exponent-negative",
                 "same_exponent",
                 ArithmeticOperation::add,
                 negMinMantissa,
                 negOdd},
                {"add.exact-cancellation",
                 "cancellation",
                 ArithmeticOperation::add,
                 odd,
                 negOdd},
                {"add.renormalizing-cancellation",
                 "borrow",
                 ArithmeticOperation::add,
                 nearCancelHigh,
                 api.float_negate(nearCancelLow)},
                {"add.min-exponent-dust-zero",
                 "underflow_to_zero",
                 ArithmeticOperation::add,
                 minExpDustHigh,
                 api.float_negate(minExpDustLow)},
                {"add.align-delta-1",
                 "alignment",
                 ArithmeticOperation::add,
                 odd,
                 alignDelta1},
                {"add.align-delta-15",
                 "alignment",
                 ArithmeticOperation::add,
                 odd,
                 alignDelta15},
                {"add.align-delta-16-half-odd",
                 "last_digit_guard",
                 ArithmeticOperation::add,
                 odd,
                 halfUlp},
                {"add.align-delta-16-six-tenths",
                 "last_digit_guard",
                 ArithmeticOperation::add,
                 posMinMantissa,
                 sixTenthsUlp},
                {"add.align-delta-17",
                 "alignment_dust",
                 ArithmeticOperation::add,
                 odd,
                 alignDelta17},
                {"add.align-delta-176",
                 "maximum_alignment",
                 ArithmeticOperation::add,
                 posMaxExponent,
                 alignMaxGap},
                {"add.carry-positive",
                 "carry",
                 ArithmeticOperation::add,
                 posMaxMantissa,
                 posMinMantissa},
                {"add.carry-negative",
                 "carry",
                 ArithmeticOperation::add,
                 negMaxMantissa,
                 negMinMantissa},
                {"add.overflow-positive",
                 "overflow",
                 ArithmeticOperation::add,
                 posMaxExponent,
                 requireCanonicalFloat(80, 1'000'000'000'000'000ULL)},
                {"add.overflow-negative",
                 "overflow",
                 ArithmeticOperation::add,
                 negMaxExponent,
                 api.float_negate(
                     requireCanonicalFloat(80, 1'000'000'000'000'000ULL))},
                {"add.max-exponent-zero",
                 "zero_precedes_overflow",
                 ArithmeticOperation::add,
                 posMaxExponent,
                 zero},
                {"add.last-digit-negative",
                 "last_digit_guard",
                 ArithmeticOperation::add,
                 negOdd,
                 api.float_negate(halfUlp)},
                {"add.legacy-clamp-positive-ten",
                 "legacy_clamp",
                 ArithmeticOperation::add,
                 posLegacyClampTen,
                 negMinMantissa},
                {"add.legacy-clamp-negative-ten",
                 "legacy_clamp",
                 ArithmeticOperation::add,
                 negLegacyClampTen,
                 posMinMantissa},
                {"subtract.zero-zero",
                 "zero_identity",
                 ArithmeticOperation::subtract,
                 zero,
                 zero},
                {"subtract.zero-positive",
                 "zero_identity",
                 ArithmeticOperation::subtract,
                 zero,
                 posMinMantissa},
                {"subtract.positive-zero",
                 "zero_identity",
                 ArithmeticOperation::subtract,
                 posMaxMantissa,
                 zero},
                {"subtract.self",
                 "cancellation",
                 ArithmeticOperation::subtract,
                 odd,
                 odd},
                {"subtract.same-exponent-positive",
                 "same_exponent",
                 ArithmeticOperation::subtract,
                 nearCancelHigh,
                 nearCancelLow},
                {"subtract.same-exponent-negative",
                 "same_exponent",
                 ArithmeticOperation::subtract,
                 api.float_negate(nearCancelHigh),
                 api.float_negate(nearCancelLow)},
                {"subtract.align-delta-15",
                 "alignment",
                 ArithmeticOperation::subtract,
                 odd,
                 alignDelta15},
                {"subtract.align-delta-16-half-odd",
                 "last_digit_guard",
                 ArithmeticOperation::subtract,
                 odd,
                 halfUlp},
                {"subtract.min-exponent-dust-zero",
                 "underflow_to_zero",
                 ArithmeticOperation::subtract,
                 minExpDustHigh,
                 minExpDustLow},
                {"subtract.overflow-positive",
                 "overflow",
                 ArithmeticOperation::subtract,
                 posMaxExponent,
                 negMaxExponent},
                {"subtract.overflow-negative",
                 "overflow",
                 ArithmeticOperation::subtract,
                 negMaxExponent,
                 posMaxExponent},
                {"subtract.min-exponent-sign",
                 "minimum_exponent",
                 ArithmeticOperation::subtract,
                 posMinExponent,
                 negMinExponent},
                {"subtract.legacy-clamp-positive-ten",
                 "legacy_clamp",
                 ArithmeticOperation::subtract,
                 posLegacyClampTen,
                 posMinMantissa},
                {"subtract.legacy-clamp-negative-ten",
                 "legacy_clamp",
                 ArithmeticOperation::subtract,
                 posMinMantissa,
                 posLegacyClampTen},
            };

            std::cerr << "phase: xahauFloatV1 arithmetic n="
                      << arithmeticCases.size() << "\n"
                      << std::flush;
            Json::Value floatSum{Json::arrayValue};
            Json::Value arithmeticRows{Json::arrayValue};
            {
                NumberSO legacyArithmetic{false};
                BEAST_EXPECT(!getSTNumberSwitchover());
                for (auto const& c : arithmeticCases)
                {
                    auto r = evaluateArithmetic(c);
                    if (std::string(c.category) == "legacy_clamp")
                    {
                        BEAST_EXPECT(r.has_value());
                        if (r.has_value())
                            BEAST_EXPECT(*r == 0);
                    }
                    Json::Value row{Json::objectValue};
                    row["id"] = c.id;
                    row["category"] = c.category;
                    row["operation"] = c.operation == ArithmeticOperation::add
                        ? "add"
                        : "subtract";
                    row["a"] = u64v(c.a);
                    row["b"] = u64v(c.b);
                    row["result"] = expectedU64(r);
                    arithmeticRows.append(row);
                    if (c.operation == ArithmeticOperation::add)
                        floatSum.append(std::move(row));
                    BEAST_EXPECT(true);
                }
            }

            ArithmeticCase const guardCase{
                "guard.number-so-odd-half-ulp",
                "switchover_guard",
                ArithmeticOperation::add,
                odd,
                halfUlp};
            Expected<std::uint64_t, hook::HookReturnCode> legacyGuard =
                Unexpected(INTERNAL_ERROR);
            Expected<std::uint64_t, hook::HookReturnCode> numberGuard =
                Unexpected(INTERNAL_ERROR);
            {
                NumberSO legacyArithmetic{false};
                legacyGuard = evaluateArithmetic(guardCase);
            }
            {
                NumberSO numberArithmetic{true};
                numberGuard = evaluateArithmetic(guardCase);
            }
            BEAST_EXPECT(legacyGuard.has_value());
            BEAST_EXPECT(numberGuard.has_value());
            if (legacyGuard.has_value() && numberGuard.has_value())
                BEAST_EXPECT(*legacyGuard != *numberGuard);

            Json::Value arithmeticOracle{Json::objectValue};
            arithmeticOracle["schema"] = "xahau.xfl-arithmetic-oracle.v1";
            arithmeticOracle["profile"] = "xahauFloatV1";
            arithmeticOracle["xahaud_source_commit"] =
                "bb244ef7729503a0317bcff0f8fdaa93ca5cb7d2";
            arithmeticOracle["fix_universal_number"] = false;
            arithmeticOracle["number_so"] = false;
            arithmeticOracle["cases"] = std::move(arithmeticRows);
            {
                Json::Value guard{Json::objectValue};
                guard["id"] = guardCase.id;
                guard["a"] = u64v(guardCase.a);
                guard["b"] = u64v(guardCase.b);
                guard["number_so_false"] = expectedU64(legacyGuard);
                guard["number_so_true"] = expectedU64(numberGuard);
                arithmeticOracle["guard_drop_control"] = std::move(guard);
            }

            // ------------------------------------------------------------------
            // float_compare
            // Body: xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1060-1099
            //   mode checks:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1067-1071
            //   IOUAmount compare:
            //     xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1073-1096
            // Wrapper admission first:
            //   xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3541-3542
            //   (invalid float → INVALID_FLOAT before mode body)
            // ------------------------------------------------------------------
            std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint32_t>>
                cmpCases;
            auto addCmp =
                [&](std::uint64_t a, std::uint64_t b, std::uint32_t mode) {
                    cmpCases.emplace_back(a, b, mode);
                };
            auto const twoOpt = api.float_set(0, 2);
            BEAST_EXPECT(twoOpt.has_value());
            if (!twoOpt)
                return;
            auto const two = twoOpt.value();
            addCmp(one, one, EQUAL);
            addCmp(one, two, LESS);
            addCmp(two, one, GREATER);
            addCmp(two, one, EQUAL);
            addCmp(one, two, GREATER | LESS);  // not-equal
            addCmp(one, two, 0);               // INVALID_ARGUMENT
            addCmp(one, two, 0b111U);          // INVALID_ARGUMENT
            for (std::int64_t base :
                 {(1LL << 53),
                  9'000'000'000'000'000LL,
                  9'999'999'999'999'990LL})
            {
                auto a = api.float_set(0, base);
                auto b = api.float_set(0, base + 1);
                if (a && b)
                {
                    addCmp(a.value(), b.value(), EQUAL);
                    addCmp(a.value(), b.value(), LESS);
                    addCmp(a.value(), b.value(), GREATER);
                    addCmp(a.value(), b.value(), GREATER | LESS);
                }
            }
            // invalid encodings — RETURN_IF_INVALID_FLOAT
            // xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392
            // xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3541-3542
            addCmp(static_cast<std::uint64_t>(-1), one, EQUAL);
            addCmp(one, static_cast<std::uint64_t>(-1), LESS);
            addCmp(static_cast<std::uint64_t>(-1), 0, 0);  // invalid + bad mode

            std::cerr << "phase: float_compare\n" << std::flush;
            Json::Value floatCompare{Json::arrayValue};
            for (auto const& [a, b, mode] : cmpCases)
            {
                auto r =
                    [&]() -> Expected<std::uint64_t, hook::HookReturnCode> {
                    if (auto e = admitFloatError(a))
                        return Unexpected(*e);
                    if (auto e = admitFloatError(b))
                        return Unexpected(*e);
                    return api.float_compare(a, b, mode);
                }();
                Json::Value row{Json::objectValue};
                row["a"] = u64v(a);
                row["b"] = u64v(b);
                // mode is a small flag bitset; still typed as i32 for
                // consistency
                row["mode"] = i32v(static_cast<std::int32_t>(mode));
                row["result"] = expectedU64(r);
                floatCompare.append(std::move(row));
                BEAST_EXPECT(true);
            }

            // ------------------------------------------------------------------
            // float_multiply / float_divide
            // multiply outer:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1008-1021
            // multiply inner (truncating man product):
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2509-2537
            // divide outer:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1367-1369
            // divide inner:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2562-2636
            // FixFloatDivide:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2565
            // div-by-one:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2573-2574
            // wrappers:
            //   xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3486-3487
            //   xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3673-3674
            // ------------------------------------------------------------------
            struct BinaryArithmeticCase
            {
                char const* id;
                char const* category;
                std::uint64_t a;
                std::uint64_t b;
            };

            auto const exactThree =
                requireCanonicalFloat(-15, 3'000'000'000'000'000ULL);
            auto const exactTen =
                requireCanonicalFloat(-14, 1'000'000'000'000'000ULL);
            auto const exactTenth =
                requireCanonicalFloat(-16, 1'000'000'000'000'000ULL);
            auto const maxExponentMinMantissa =
                requireCanonicalFloat(80, 1'000'000'000'000'000ULL);
            auto const fixedDivideGuardDividend =
                requireCanonicalFloat(0, 2'967'558'483'383'827ULL);
            auto const fixedDivideGuardDivisor =
                requireCanonicalFloat(0, 2'254'467'625'494'646ULL);
            auto const mantissaExtremaPeer =
                requireCanonicalFloat(0, 2'000'000'000'000'000ULL);
            auto const divideOrder15BelowNumerator =
                requireCanonicalFloat(0, 1'999'999'999'999'995ULL);
            auto const divideOrder15AboveNumerator =
                requireCanonicalFloat(0, 1'999'999'999'999'996ULL);
            auto const order16Below =
                requireCanonicalFloat(0, 9'999'999'999'999'978ULL);
            auto const order16Above =
                requireCanonicalFloat(0, 9'999'999'999'999'979ULL);
            auto const order17BelowPeer =
                requireCanonicalFloat(0, 9'999'999'999'999'960ULL);
            auto const order17AbovePeer =
                requireCanonicalFloat(0, 9'999'999'999'999'961ULL);
            auto const order17RetainedDigitPeer =
                requireCanonicalFloat(0, 9'999'999'999'999'962ULL);
            auto const divideOrder15InwardNumerator =
                requireCanonicalFloat(0, 1'000'000'000'000'009ULL);
            auto const divideOrder15InwardDenominator =
                requireCanonicalFloat(0, 1'000'000'000'000'010ULL);
            auto const restoringDigit10Numerator =
                requireCanonicalFloat(0, 2'000'000'000'000'009ULL);
            auto const restoringDigit10Denominator =
                requireCanonicalFloat(0, 1'000'000'000'000'009ULL);
            auto const restoringCoefficient18Numerator =
                requireCanonicalFloat(0, 1'000'000'000'000'010ULL);
            auto const restoringCoefficient18Denominator =
                requireCanonicalFloat(0, 1'900'000'000'000'000ULL);
            auto const minExponentPeer =
                requireCanonicalFloat(-96, 1'000'000'000'000'001ULL);
            auto const invalidFloat = std::numeric_limits<std::uint64_t>::max();

            auto const fixedRestoringCoefficients = [](std::uint64_t man1,
                                                       std::uint64_t man2) {
                std::vector<int> coefficients;
                while (man2 > man1)
                    man2 /= 10;
                while (man2 < man1)
                {
                    if (man2 * 10 > man1)
                        break;
                    man2 *= 10;
                }
                while (man2 > 0)
                {
                    int coefficient = 0;
                    for (; man1 >= man2; man1 -= man2, ++coefficient)
                        ;
                    coefficients.push_back(coefficient);
                    man2 /= 10;
                }
                return coefficients;
            };
            auto const coefficient18Trace = fixedRestoringCoefficients(
                1'000'000'000'000'010ULL, 1'900'000'000'000'000ULL);
            auto const expectedCoefficient18Trace =
                std::vector<int>{5, 2, 6, 3, 1, 5, 7, 8, 9, 4, 7, 3, 6, 8, 18};
            BEAST_EXPECT(coefficient18Trace == expectedCoefficient18Trace);
            BEAST_EXPECT(!coefficient18Trace.empty());
            if (!coefficient18Trace.empty())
            {
                BEAST_EXPECT(coefficient18Trace.size() <= 16);
                BEAST_EXPECT(coefficient18Trace.front() <= 9);
                for (std::size_t i = 1; i + 1 < coefficient18Trace.size(); ++i)
                    BEAST_EXPECT(coefficient18Trace[i] <= 10);
                BEAST_EXPECT(coefficient18Trace.back() == 18);
                int totalSubtractions = 0;
                for (auto const coefficient : coefficient18Trace)
                    totalSubtractions += coefficient;
                BEAST_EXPECT(totalSubtractions <= 167);
            }

            std::vector<BinaryArithmeticCase> multiplyCases = {
                {"multiply.zero-left", "zero_order", zero, two},
                {"multiply.zero-right", "zero_order", two, zero},
                {"multiply.zero-zero", "zero_order", zero, zero},
                {"multiply.positive-positive", "sign", two, exactThree},
                {"multiply.negative-positive",
                 "sign",
                 api.float_negate(two),
                 exactThree},
                {"multiply.positive-negative",
                 "sign",
                 two,
                 api.float_negate(exactThree)},
                {"multiply.negative-negative",
                 "sign",
                 api.float_negate(two),
                 api.float_negate(exactThree)},
                {"multiply.min-mantissa-by-one",
                 "mantissa_extrema",
                 posMinMantissa,
                 one},
                {"multiply.max-mantissa-by-one",
                 "mantissa_extrema",
                 posMaxMantissa,
                 one},
                {"multiply.min-exponent-by-one",
                 "exponent_extrema",
                 posMinExponent,
                 one},
                {"multiply.max-exponent-by-one",
                 "exponent_extrema",
                 maxExponentMinMantissa,
                 one},
                {"multiply.normalization-carry",
                 "normalization",
                 posMaxMantissa,
                 two},
                {"multiply.truncation-tail", "last_digit", odd, odd},
                {"multiply.negative-truncation-tail",
                 "last_digit",
                 api.float_negate(odd),
                 odd},
                {"multiply.order-16-below",
                 "decimal_order_transition",
                 order16Below,
                 one},
                {"multiply.order-16-above",
                 "decimal_order_transition",
                 order16Above,
                 one},
                {"multiply.order-17-below",
                 "decimal_order_transition",
                 posMaxMantissa,
                 order17BelowPeer},
                {"multiply.order-17-above",
                 "decimal_order_transition",
                 posMaxMantissa,
                 order17AbovePeer},
                {"multiply.order-17-retained-digit",
                 "decimal_order_transition",
                 posMaxMantissa,
                 order17RetainedDigitPeer},
                {"multiply.max-by-max",
                 "mantissa_extrema",
                 posMaxMantissa,
                 posMaxMantissa},
                {"multiply.underflow-to-zero",
                 "underflow_to_zero",
                 posMinExponent,
                 exactTenth},
                {"multiply.overflow",
                 "overflow",
                 maxExponentMinMantissa,
                 exactTen},
                {"multiply.invalid-left-zero",
                 "precedence",
                 invalidFloat,
                 zero},
                {"multiply.zero-invalid-right",
                 "precedence",
                 zero,
                 invalidFloat},
                {"multiply.invalid-left-valid",
                 "precedence",
                 invalidFloat,
                 one},
            };

            auto evaluateMultiply = [&](BinaryArithmeticCase const& c)
                -> Expected<std::uint64_t, hook::HookReturnCode> {
                if (auto e = admitFloatError(c.a))
                    return Unexpected(*e);
                if (auto e = admitFloatError(c.b))
                    return Unexpected(*e);
                return api.float_multiply(c.a, c.b);
            };

            std::cerr << "phase: float_multiply n=" << multiplyCases.size()
                      << "\n"
                      << std::flush;
            Json::Value floatMultiply{Json::arrayValue};
            for (auto const& c : multiplyCases)
            {
                auto const r = evaluateMultiply(c);
                Json::Value row{Json::objectValue};
                row["id"] = c.id;
                row["category"] = c.category;
                row["operation"] = "multiply";
                row["a"] = u64v(c.a);
                row["b"] = u64v(c.b);
                row["result"] = expectedU64(r);
                floatMultiply.append(std::move(row));
            }

            // ------------------------------------------------------------------
            // float_mulratio — remainder scaling and directional rounding.
            // Body: xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1023-1048
            // Internal adapter:
            //   xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2540-2559
            // Decimal ratio and the post-round IOUAmount reconstruction:
            //   xahaud:src/libxrpl/protocol/IOUAmount.cpp:183-315
            // Wrapper admission:
            //   xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3497-3514
            // ------------------------------------------------------------------
            struct MulRatioCase
            {
                std::uint64_t a;
                std::uint32_t roundUp;
                std::uint32_t numerator;
                std::uint32_t denominator;
                char const* note;
            };
            std::vector<MulRatioCase> mulRatioCases;
            auto addMulRatio = [&](std::uint64_t a,
                                   std::uint32_t roundUp,
                                   std::uint32_t numerator,
                                   std::uint32_t denominator,
                                   char const* note) {
                mulRatioCases.push_back(
                    {a, roundUp, numerator, denominator, note});
            };

            auto const tenOpt = api.float_set(0, 10);
            BEAST_EXPECT(tenOpt.has_value());
            if (!tenOpt)
                return;
            auto const ten = tenOpt.value();

            addMulRatio(ten, 0, 3, 2, "exact_positive");
            addMulRatio(ten, 0, 1, 3, "positive_toward_zero");
            addMulRatio(ten, 1, 1, 3, "positive_away_from_zero");
            addMulRatio(api.float_negate(ten), 0, 1, 3, "negative_toward_zero");
            addMulRatio(
                api.float_negate(ten), 1, 1, 3, "negative_away_from_zero");
            addMulRatio(one, 0, 1, 0, "division_by_zero");
            // HookAPI checks zero before denominator==0.
            // xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1030-1033
            addMulRatio(0, 1, 1, 0, "zero_precedes_division_by_zero");
            // Wrapper admission precedes the body denominator check.
            // xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3508-3511
            addMulRatio(
                static_cast<std::uint64_t>(-1),
                0,
                1,
                0,
                "invalid_float_precedes_division_by_zero");
            addMulRatio(one, 1, 0, 1, "zero_numerator");
            addMulRatio(
                one,
                1,
                std::numeric_limits<std::uint32_t>::max(),
                1,
                "uint32_max_numerator");

            // A final +1/-1 crosses maxMantissa. IOUAmount reconstruction must
            // carry a decimal place into the exponent instead of leaking an
            // oversized mantissa to HookAPI::make_float.
            // xahaud:src/libxrpl/protocol/IOUAmount.cpp:287-310
            auto const carryOpt = api.float_set(0, 7'499'999'999'999'999LL);
            BEAST_EXPECT(carryOpt.has_value());
            if (!carryOpt)
                return;
            auto const carry = carryOpt.value();
            addMulRatio(carry, 1, 4, 3, "rounding_carry_positive");
            addMulRatio(
                api.float_negate(carry), 1, 4, 3, "rounding_carry_negative");

            std::cerr << "phase: float_mulratio\n" << std::flush;
            Json::Value floatMulRatio{Json::arrayValue};
            for (auto const& c : mulRatioCases)
            {
                auto r =
                    [&]() -> Expected<std::uint64_t, hook::HookReturnCode> {
                    if (auto e = admitFloatError(c.a))
                        return Unexpected(*e);
                    return api.float_mulratio(
                        c.a, c.roundUp, c.numerator, c.denominator);
                }();
                Json::Value row{Json::objectValue};
                row["a"] = u64v(c.a);
                row["round_up"] = i32v(static_cast<std::int32_t>(c.roundUp));
                row["numerator"] = u64v(c.numerator);
                row["denominator"] = u64v(c.denominator);
                row["result"] = expectedU64(r);
                row["note"] = c.note;
                floatMulRatio.append(std::move(row));
                BEAST_EXPECT(true);
            }

            std::vector<BinaryArithmeticCase> divideCases = {
                {"divide.zero-numerator", "zero_order", zero, two},
                {"divide.denominator-zero", "division_by_zero", two, zero},
                {"divide.zero-zero", "precedence", zero, zero},
                {"divide.positive-positive", "sign", exactTen, two},
                {"divide.negative-positive",
                 "sign",
                 api.float_negate(exactTen),
                 two},
                {"divide.positive-negative",
                 "sign",
                 exactTen,
                 api.float_negate(two)},
                {"divide.negative-negative",
                 "sign",
                 api.float_negate(exactTen),
                 api.float_negate(two)},
                {"divide.min-mantissa-by-one",
                 "mantissa_extrema",
                 posMinMantissa,
                 one},
                {"divide.max-mantissa-by-one",
                 "mantissa_extrema",
                 posMaxMantissa,
                 one},
                {"divide.min-mantissa-numerator-generic",
                 "mantissa_extrema_generic",
                 posMinMantissa,
                 mantissaExtremaPeer},
                {"divide.max-mantissa-numerator-generic",
                 "mantissa_extrema_generic",
                 posMaxMantissa,
                 mantissaExtremaPeer},
                {"divide.min-mantissa-denominator-generic",
                 "mantissa_extrema_generic",
                 mantissaExtremaPeer,
                 posMinMantissa},
                {"divide.max-mantissa-denominator-generic",
                 "mantissa_extrema_generic",
                 mantissaExtremaPeer,
                 posMaxMantissa},
                {"divide.min-exponent-by-one",
                 "exponent_extrema",
                 posMinExponent,
                 one},
                {"divide.max-exponent-by-one",
                 "exponent_extrema",
                 posMaxExponent,
                 one},
                {"divide.max-exponent-numerator-zero",
                 "input_normalization_precedence",
                 posMaxExponent,
                 zero},
                {"divide.zero-max-exponent-denominator",
                 "input_normalization_precedence",
                 zero,
                 posMaxExponent},
                {"divide.max-exponent-numerator-generic",
                 "input_normalization",
                 posMaxExponent,
                 mantissaExtremaPeer},
                {"divide.max-exponent-denominator-generic",
                 "input_normalization",
                 mantissaExtremaPeer,
                 posMaxExponent},
                {"divide.order-15-below",
                 "decimal_order_transition",
                 divideOrder15BelowNumerator,
                 mantissaExtremaPeer},
                {"divide.order-15-above",
                 "decimal_order_transition",
                 divideOrder15AboveNumerator,
                 mantissaExtremaPeer},
                {"divide.order-15-inward-correction",
                 "decimal_order_transition",
                 divideOrder15InwardNumerator,
                 divideOrder15InwardDenominator},
                {"divide.restoring-digit-10",
                 "restoring_loop",
                 restoringDigit10Numerator,
                 restoringDigit10Denominator},
                {"divide.restoring-coefficient-18",
                 "restoring_loop",
                 restoringCoefficient18Numerator,
                 restoringCoefficient18Denominator},
                {"divide.min-exponent-numerator-generic",
                 "exponent_extrema_generic",
                 posMinExponent,
                 minExponentPeer},
                {"divide.min-exponent-denominator-generic",
                 "exponent_extrema_generic",
                 minExponentPeer,
                 posMinExponent},
                {"divide.smaller-denominator", "normalization", exactTen, two},
                {"divide.larger-denominator", "normalization", two, exactTen},
                {"divide.fixed-last-digit",
                 "fixed_divide",
                 fixedDivideGuardDividend,
                 fixedDivideGuardDivisor},
                {"divide.repeating-one-third", "last_digit", one, exactThree},
                {"divide.last-digit-neighbor",
                 "last_digit",
                 odd,
                 posMinMantissa},
                {"divide.underflow-to-zero",
                 "underflow_to_zero",
                 posMinExponent,
                 exactTen},
                {"divide.overflow",
                 "overflow",
                 maxExponentMinMantissa,
                 exactTenth},
                {"divide.invalid-numerator-zero",
                 "precedence",
                 invalidFloat,
                 zero},
                {"divide.zero-invalid-denominator",
                 "precedence",
                 zero,
                 invalidFloat},
                {"divide.valid-invalid-denominator",
                 "precedence",
                 one,
                 invalidFloat},
            };

            auto evaluateDivide = [&](hook::HookAPI const& divideApi,
                                      BinaryArithmeticCase const& c)
                -> Expected<std::uint64_t, hook::HookReturnCode> {
                if (auto e = admitFloatError(c.a))
                    return Unexpected(*e);
                if (auto e = admitFloatError(c.b))
                    return Unexpected(*e);
                return divideApi.float_divide(c.a, c.b);
            };

            std::cerr << "phase: float_divide n=" << divideCases.size() << "\n"
                      << std::flush;
            Json::Value floatDivide{Json::arrayValue};
            for (auto const& c : divideCases)
            {
                auto const r = evaluateDivide(api, c);
                Json::Value row{Json::objectValue};
                row["id"] = c.id;
                row["category"] = c.category;
                row["operation"] = "divide";
                row["a"] = u64v(c.a);
                row["b"] = u64v(c.b);
                row["result"] = expectedU64(r);
                floatDivide.append(std::move(row));
            }

            BinaryArithmeticCase const divideGuardCase{
                "guard.fix-float-divide-last-digit",
                "amendment_guard",
                fixedDivideGuardDividend,
                fixedDivideGuardDivisor};
            auto const fixedDivideGuard = evaluateDivide(api, divideGuardCase);
            auto const unfixedDivideGuard =
                evaluateDivide(unfixedDivideApi, divideGuardCase);
            BEAST_EXPECT(fixedDivideGuard.has_value());
            BEAST_EXPECT(unfixedDivideGuard.has_value());
            if (fixedDivideGuard.has_value() && unfixedDivideGuard.has_value())
            {
                BEAST_EXPECT(
                    *fixedDivideGuard ==
                    requireCanonicalFloat(-15, 1'316'301'218'888'766ULL));
                BEAST_EXPECT(*fixedDivideGuard != *unfixedDivideGuard);
            }

            auto validateCaseIds =
                [&](auto const& cases,
                    std::vector<std::string> const& required) {
                    std::set<std::string> ids;
                    for (auto const& c : cases)
                    {
                        BEAST_EXPECT(
                            c.id != nullptr && std::string(c.id).size() > 0);
                        BEAST_EXPECT(
                            c.category != nullptr &&
                            std::string(c.category).size() > 0);
                        BEAST_EXPECT(ids.emplace(c.id).second);
                    }
                    BEAST_EXPECT(ids.size() == cases.size());
                    for (auto const& id : required)
                        BEAST_EXPECT(ids.contains(id));
                    return ids;
                };

            auto const multiplyIds = validateCaseIds(
                multiplyCases,
                {"multiply.zero-left",
                 "multiply.negative-negative",
                 "multiply.min-mantissa-by-one",
                 "multiply.max-exponent-by-one",
                 "multiply.normalization-carry",
                 "multiply.truncation-tail",
                 "multiply.negative-truncation-tail",
                 "multiply.order-16-below",
                 "multiply.order-16-above",
                 "multiply.order-17-below",
                 "multiply.order-17-above",
                 "multiply.order-17-retained-digit",
                 "multiply.max-by-max",
                 "multiply.underflow-to-zero",
                 "multiply.overflow",
                 "multiply.invalid-left-zero"});
            auto const divideIds = validateCaseIds(
                divideCases,
                {"divide.zero-zero",
                 "divide.negative-negative",
                 "divide.min-mantissa-by-one",
                 "divide.min-mantissa-numerator-generic",
                 "divide.max-mantissa-numerator-generic",
                 "divide.min-mantissa-denominator-generic",
                 "divide.max-mantissa-denominator-generic",
                 "divide.max-exponent-by-one",
                 "divide.max-exponent-numerator-zero",
                 "divide.zero-max-exponent-denominator",
                 "divide.max-exponent-numerator-generic",
                 "divide.max-exponent-denominator-generic",
                 "divide.order-15-below",
                 "divide.order-15-above",
                 "divide.order-15-inward-correction",
                 "divide.restoring-digit-10",
                 "divide.restoring-coefficient-18",
                 "divide.min-exponent-numerator-generic",
                 "divide.min-exponent-denominator-generic",
                 "divide.fixed-last-digit",
                 "divide.repeating-one-third",
                 "divide.underflow-to-zero",
                 "divide.overflow",
                 "divide.invalid-numerator-zero"});
            for (auto const& id : multiplyIds)
                BEAST_EXPECT(!divideIds.contains(id));

            Json::Value multiplyDivideOracle{Json::objectValue};
            multiplyDivideOracle["schema"] =
                "xahau.xfl-multiply-divide-oracle.v1";
            multiplyDivideOracle["profile"] = "xahauFloatV1";
            multiplyDivideOracle["xahaud_source_commit"] =
                "bb244ef7729503a0317bcff0f8fdaa93ca5cb7d2";
            multiplyDivideOracle["fix_float_divide"] = true;
            {
                Json::Value build{Json::objectValue};
                build["commit"] = BuildInfo::getGitCommitString();
                build["commit_hash"] = BuildInfo::getGitCommitHash();
                build["branch"] = BuildInfo::getGitBranch();
                build["dirty"] = BuildInfo::isGitDirty();
                multiplyDivideOracle["build"] = std::move(build);
            }
            multiplyDivideOracle["multiply_cases"] = floatMultiply;
            multiplyDivideOracle["divide_cases"] = floatDivide;
            {
                Json::Value guard{Json::objectValue};
                guard["id"] = divideGuardCase.id;
                guard["a"] = u64v(divideGuardCase.a);
                guard["b"] = u64v(divideGuardCase.b);
                guard["fix_false"] = expectedU64(unfixedDivideGuard);
                guard["fix_true"] = expectedU64(fixedDivideGuard);
                multiplyDivideOracle["guard_fix_float_divide"] =
                    std::move(guard);
            }

            BEAST_EXPECT(
                multiplyDivideOracle["schema"].asString() ==
                "xahau.xfl-multiply-divide-oracle.v1");
            BEAST_EXPECT(multiplyDivideOracle["fix_float_divide"].asBool());
            BEAST_EXPECT(
                multiplyDivideOracle["multiply_cases"].size() ==
                multiplyCases.size());
            BEAST_EXPECT(
                multiplyDivideOracle["divide_cases"].size() ==
                divideCases.size());

            // ------------------------------------------------------------------
            // float_invert
            // Body: xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1356-1364
            //   zero → DIVISION_BY_ZERO (-25); one → one;
            //   else float_divide_internal(float_one_internal, x)
            // Wrapper: xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3694
            // DIVISION_BY_ZERO: xahaud:include/xrpl/hook/Enum.h:364
            // ------------------------------------------------------------------
            std::cerr << "phase: float_invert\n" << std::flush;
            Json::Value floatInvert{Json::arrayValue};
            for (std::uint64_t x : {one, two, static_cast<std::uint64_t>(0)})
            {
                auto r =
                    [&]() -> Expected<std::uint64_t, hook::HookReturnCode> {
                    if (auto e = admitFloatError(x))
                        return Unexpected(*e);
                    return api.float_invert(x);
                }();
                Json::Value row{Json::objectValue};
                row["a"] = u64v(x);
                row["result"] = expectedU64(r);
                floatInvert.append(std::move(row));
                BEAST_EXPECT(true);
            }

            // ------------------------------------------------------------------
            // document
            // ------------------------------------------------------------------
            Json::Value root{Json::objectValue};
            root["version"] = 1;
            root["suite"] = "ripple.app.HookzFloatVectors";
            root["purpose"] = "host oracle for hooks-testing float model";
            root["value_encoding"] =
                "typed {type,val} for u64|i64|i32|error — val is decimal "
                "string, "
                "no JSON number path (Json::UInt is 32-bit; avoids double "
                "loss)";
            // Host anchors for consumers — each value is a single full
            // xahaud:path:line cite (no shorthands, no comma-spliced ranges).
            {
                Json::Value cites{Json::objectValue};
                cites["limits"] = "xahaud:src/xrpld/app/hook/HookAPI.h:39-42";
                cites["normalize_xfl"] =
                    "xahaud:src/xrpld/app/hook/HookAPI.h:184-289";
                cites["canonical_xfl"] =
                    "xahaud:src/xrpld/app/hook/HookAPI.h:146-170";
                cites["float_one"] =
                    "xahaud:src/xrpld/app/hook/HookAPI.h:291-292";
                cites["float_set"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:986-1005";
                cites["float_sum"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1105-1145";
                cites["number_so_default"] =
                    "xahaud:src/libxrpl/protocol/IOUAmount.cpp:34-48";
                cites["legacy_iou_add"] =
                    "xahaud:src/libxrpl/protocol/IOUAmount.cpp:138-173";
                cites["transaction_number_so"] =
                    "xahaud:src/xrpld/app/tx/detail/apply.cpp:148";
                cites["float_compare"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1060-1099";
                cites["float_multiply_outer"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1008-1021";
                cites["float_multiply_inner"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2509-2537";
                cites["float_mulratio"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1023-1048";
                cites["float_mulratio_internal"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2540-2559";
                cites["iou_mulratio"] =
                    "xahaud:src/libxrpl/protocol/IOUAmount.cpp:183-315";
                cites["float_divide_outer"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1367-1369";
                cites["float_divide_inner"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2562-2636";
                cites["fix_float_divide_selector"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2565";
                cites["float_invert"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1356-1364";
                cites["float_negate"] =
                    "xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1052-1057";
                cites["invalid_float_gate"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392";
                cites["float_set_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3448-3456";
                cites["float_sum_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3552-3565";
                cites["float_compare_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3531-3549";
                cites["float_multiply_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3481-3494";
                cites["float_mulratio_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3497-3514";
                cites["float_divide_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3668-3681";
                cites["float_invert_wrapper"] =
                    "xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3689-3701";
                cites["INVALID_FLOAT"] = "xahaud:include/xrpl/hook/Enum.h:362";
                cites["DIVISION_BY_ZERO"] =
                    "xahaud:include/xrpl/hook/Enum.h:364";
                root["host_cites"] = std::move(cites);
            }
            root["float_one"] = u64v(one);
            root["float_set"] = std::move(floatSet);
            root["float_sum"] = std::move(floatSum);
            root["xfl_arithmetic"] = std::move(arithmeticOracle);
            root["float_compare"] = std::move(floatCompare);
            root["float_multiply"] = std::move(floatMultiply);
            root["xfl_multiply_divide"] = std::move(multiplyDivideOracle);
            root["float_mulratio"] = std::move(floatMulRatio);
            root["float_divide"] = std::move(floatDivide);
            root["float_invert"] = std::move(floatInvert);

            std::cerr << "phase: emit_json\n" << std::flush;
            auto const rendered = Json::pretty(root);
            BEAST_EXPECT(rendered == Json::pretty(root));
            // Emit before any further Expected::value() checks so a bad access
            // cannot swallow the oracle dump.
            std::cout << "---HOOKZ_FLOAT_VECTORS_BEGIN---\n"
                      << rendered << "---HOOKZ_FLOAT_VECTORS_END---\n"
                      << std::flush;

            // Sanity: float_one == make_float(1e15, -15)
            // xahaud:src/xrpld/app/hook/HookAPI.h:291-292
            auto oneFromSet = api.float_set(-15, 1'000'000'000'000'000LL);
            if (oneFromSet.has_value())
                BEAST_EXPECT(oneFromSet.value() == one);
            else
                fail("float_set(-15, 1e15) failed; cannot match float_one");
        }
        catch (std::exception const& e)
        {
            // Surface the step so a bad Expected::value() does not look like
            // a silent suite miss.
            std::cerr << "HookzFloatVectors unhandled: " << e.what() << "\n"
                      << std::flush;
            throw;
        }
    }

    void
    run() override
    {
        dumpVectors();
    }
};

BEAST_DEFINE_TESTSUITE(HookzFloatVectors, app, ripple);

}  // namespace test
}  // namespace ripple
