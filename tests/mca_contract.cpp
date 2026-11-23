// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/mca/decoder.hpp"
#include "addc/mca/mi400.hpp"
#include "addc/product.hpp"

namespace
{

constexpr uint64_t kUc = uint64_t{1} << 61;
constexpr uint64_t kPcc = uint64_t{1} << 57;

// STATUS.ErrorCodeExt is bits [21:16]. IPID.HardwareID is bits [43:32].
constexpr addc::mca::McaRegisters make_regs(uint64_t hw_id, uint64_t ext,
                                            uint64_t flags)
{
    return addc::mca::McaRegisters{flags | (ext << 16), hw_id << 32, 0, 0};
}

} // namespace

int main()
{
    const auto event = addc::mca::decode_common(
        {addc::mca::McaRegisters{}});
    if (event.decoded.bank.has_value())
    {
        return 1;
    }
    if (event.decoded.error_type.has_value())
    {
        return 2;
    }
    if (!event.ppr.is_null())
    {
        return 3;
    }
    for (const auto& product : addc::product::compiled_products())
    {
        if (product.capabilities.mca &&
            !addc::mca::try_decode_project_event(
                product.identity.canonical_name,
                addc::mca::BankCapture{addc::mca::McaRegisters{}}))
        {
            return 4;
        }
    }
#ifdef ADDC_HAS_VENICE
    if (!addc::mca::try_decode_project_event(
            "venice",
            addc::mca::BankCapture{addc::mca::McaRegisters{}}))
    {
        return 5;
    }
#endif
    // cpuid_eax is optional: empty by default, and carried when supplied.
    const addc::mca::BankCapture no_cpuid{addc::mca::McaRegisters{}};
    if (no_cpuid.cpuid_eax.has_value())
    {
        return 6;
    }
    const addc::mca::BankCapture with_cpuid{addc::mca::McaRegisters{}, 0,
                                            0x00B90F00U};
    if (!with_cpuid.cpuid_eax.has_value() ||
        *with_cpuid.cpuid_eax != 0x00B90F00U)
    {
        return 7;
    }
    // AFID bases follow the AFID CSV: the base is the AFID of ErrorCodeExt 0.
    namespace mi400 = addc::mca::base::mi400;
    if (mi400::decode_afid(make_regs(0x80, 0x01, 0)) != 43842U ||
        mi400::decode_afid(make_regs(0x80, 0x3F, kPcc)) != 44032U)
    {
        return 8;
    }
    if (mi400::decode_afid(make_regs(0x27, 0x04, 0)) != 44613U ||
        mi400::decode_afid(make_regs(0x27, 0x3F, kPcc)) != 44800U)
    {
        return 9;
    }
    // Venice is optional at build time: skip when it is not compiled in.
    const auto venice_afid = [](uint64_t flags) -> int {
        const auto r = addc::mca::try_decode_project_event(
            "venice", addc::mca::BankCapture{make_regs(0xf9, 0x00, flags)});
        return r ? r->decoded.afid.at(0) : -1;
    };
    if (venice_afid(0) != -1 &&
        (venice_afid(0) != 20641 || venice_afid(kUc) != 22370 ||
         venice_afid(kPcc) != 24099))
    {
        return 10;
    }
    return 0;
}
