// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/mca/decoder.hpp"
#include "addc/product.hpp"

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
    return 0;
}
