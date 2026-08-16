// Minimal C ABI over Xahau's real decimal float machinery for differential
// tests in hooks-testing. Keep this boundary fixed-width and exception-safe;
// never expose C++ objects to ctypes.

#include <xrpld/app/hook/HookAPI.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/IOUAmount.h>

#include <cstdint>
#include <stdexcept>

#if defined(_WIN32)
#define HOOKZ_ORACLE_EXPORT __declspec(dllexport)
#else
#define HOOKZ_ORACLE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

// Mirrors RETURN_IF_INVALID_FLOAT.
// xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3375-3392
std::int64_t
admitFloat(std::uint64_t raw) noexcept
{
    using enum hook_api::hook_return_code;
    using namespace hook::hook_float;

    auto const value = static_cast<std::int64_t>(raw);
    if (value < 0)
        return static_cast<std::int64_t>(INVALID_FLOAT);
    if (value == 0)
        return 0;

    auto const man = get_mantissa(value);
    auto const exp = get_exponent(value);
    if (!man || !exp || man.value() < minMantissa ||
        man.value() > maxMantissa || exp.value() < minExponent ||
        exp.value() > maxExponent)
        return static_cast<std::int64_t>(INVALID_FLOAT);
    return 0;
}

}  // namespace

extern "C" {

HOOKZ_ORACLE_EXPORT std::uint32_t
hookz_xahaud_oracle_abi_version() noexcept
{
    return 1;
}

HOOKZ_ORACLE_EXPORT char const*
hookz_xahaud_oracle_git_commit() noexcept
{
    return ripple::BuildInfo::getGitCommitString().c_str();
}

HOOKZ_ORACLE_EXPORT std::int64_t
hookz_xahaud_float_mulratio(
    std::uint64_t raw,
    std::uint32_t roundUp,
    std::uint32_t numerator,
    std::uint32_t denominator) noexcept
{
    using enum hook_api::hook_return_code;
    using namespace hook::hook_float;

    // Wrapper admission precedes HookAPI's zero/denominator body checks.
    // xahaud:src/xrpld/app/hook/detail/applyHook.cpp:3497-3514
    if (auto const error = admitFloat(raw); error != 0)
        return error;

    // xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:1030-1033
    if (raw == 0)
        return 0;
    if (denominator == 0)
        return static_cast<std::int64_t>(DIVISION_BY_ZERO);

    try
    {
        auto const value = static_cast<std::int64_t>(raw);
        auto man = static_cast<std::int64_t>(get_mantissa(value).value());
        auto exp = get_exponent(value).value();

        // Same primitives as HookAPI::mulratio_internal.
        // xahaud:src/xrpld/app/hook/detail/HookAPI.cpp:2540-2559
        // xahaud:src/libxrpl/protocol/IOUAmount.cpp:183-315
        ripple::IOUAmount amount{man, exp};
        auto const out =
            ripple::mulRatio(amount, numerator, denominator, roundUp != 0);
        man = out.mantissa();
        exp = out.exponent();

        if (man < 0)
            man = -man;
        auto const result = make_float(
            static_cast<std::uint64_t>(man), exp, is_negative(value));
        if (!result)
            return static_cast<std::int64_t>(result.error());
        return static_cast<std::int64_t>(result.value());
    }
    catch (std::overflow_error const&)
    {
        return static_cast<std::int64_t>(XFL_OVERFLOW);
    }
    catch (...)
    {
        // No C++ exception may cross the ctypes boundary.
        return static_cast<std::int64_t>(INTERNAL_ERROR);
    }
}

}  // extern "C"
