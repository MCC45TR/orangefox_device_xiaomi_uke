// SPDX-License-Identifier: Apache-2.0
// Host-only tests execute the shipping worker with a controlled storage boundary.
#include "ui-preference-writer.hpp"
#include <atomic>
#include <cassert>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;
int main() {
    std::atomic<unsigned> saves{0};
    {
        ure::PreferenceWriter writer([&] { ++saves; });
        for(unsigned i=0;i<30;++i) { writer.request(); std::this_thread::sleep_for(5ms); }
        assert(saves==0); // Dragging does not start a save for each GUI event.
    }
    assert(saves==1); // Normal shutdown drains the one final snapshot.
    std::promise<void> entered,release;
    const auto ready=entered.get_future(); auto unblock=release.get_future().share();
    {
        ure::PreferenceWriter writer([&] { if(++saves==2) { entered.set_value(); unblock.wait(); } });
        writer.request();
        assert(ready.wait_for(3s)==std::future_status::ready);
        const auto before=std::chrono::steady_clock::now();
        for(unsigned i=0;i<100;++i)writer.request();
        assert(std::chrono::steady_clock::now()-before<500ms); // Busy storage cannot block scheduling.
        release.set_value();
    }
    assert(saves==3); // Requests during a busy save coalesce into one final save.
    {
        ure::PreferenceWriter writer([&] { ++saves; throw std::runtime_error("fixture storage failure"); });
        writer.request();
    }
    assert(saves==4); // A failed backend cannot leave an unjoined worker or terminate the GUI.
}
