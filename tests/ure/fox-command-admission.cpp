// SPDX-License-Identifier: Apache-2.0
// Compile the production Fox_Remote_State against controlled external state.
#include <cstdio>
#include "fox_fifo/fox_remote_state.hpp"
#include "data.hpp"

bool fox_active = false;
int action_running = 0;

int main()
{
    for (int legacy = 0; legacy < 2; ++legacy) {
        for (int active = 0; active < 2; ++active) {
            for (int worker = 0; worker < 2; ++worker) {
                fox_active = active != 0;
                action_running = worker;
                const bool occupied = legacy != 0 || active != 0 || worker != 0;
                if (Fox_Remote_State::CommandActive(legacy != 0) != occupied ||
                    Fox_Remote_State::CanAcceptCommand(legacy != 0) != !occupied) {
                    std::fputs("FAIL: action ownership admission lost\n", stderr);
                    return 1;
                }
            }
        }
    }
    // Reproduce the observed window: RPC output closed, GUI batch still owned.
    fox_active = false;
    action_running = 1;
    if (Fox_Remote_State::CanAcceptCommand(false)) return 1;
    action_running = 0;
    if (!Fox_Remote_State::CanAcceptCommand(false)) return 1;
}
