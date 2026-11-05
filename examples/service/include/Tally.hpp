// SPDX-License-Identifier: GPL-3.0-or-later
// The example.tally service in C++: its wrapper, and the traits Service<Tally> finds it by.

#pragma once

#include "TallyApi.h"

#include <wxl/Service.hpp>

/// A running total any extension can add to.
class Tally
{
public:
    Tally() = default;
    explicit Tally(const TallyApi* api) : m_api(api) {}

    void    Add(int32_t amount) const { m_api->Add(amount); }
    int32_t Total() const { return m_api->Total(); }

private:
    const TallyApi* m_api = nullptr;
};

template <>
struct wxl::ServiceTraits<Tally>
{
    using Table                           = TallyApi;
    static constexpr const char* kName    = TALLY_API_NAME;
    static constexpr uint32_t    kVersion = TALLY_API_VERSION;
};
