// SPDX-License-Identifier: GPL-3.0-or-later
// The side that uses example.tally, as another extension would; this one also publishes it.

#include "Tally.hpp"

#include <wxl/wxl.hpp>

using namespace wxl;

bool PublishTally();

class TallyUser : public Extension
{
    Service<Tally> tally;

    void OnStart() override
    {
        // Not published yet: the callback runs once it is, here when every extension has loaded.
        tally.OnReady([](Tally& t)
        {
            t.Add(2);
            t.Add(3);
            Log::Info("total {}", t.Total());
        });
        PublishTally();
    }
};

Export<TallyUser> extension;
