// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace ure {
// The caller retains the cooperative operation lease and lifecycle quarantine.
// Hold an identity descriptor throughout the tool call, but hand off the block
// claim: unmodified formatters/checkers independently open the same block view
// with O_EXCL. Keeping our claim would make their safety check fail with EBUSY.
// Verify exact map identity and own-only open counts at every handoff boundary.
// This is cooperative exclusion, not protection against privileged raw writers.
template<class Open, class Verify, class Effect, class Readback>
void dualboot_view_handoff(Open&& open, Verify&& verify, Effect&& effect, Readback&& readback) {
    auto claim=open(true);
    auto retained=open(false);
    verify(retained,2);
    claim=decltype(claim){};
    verify(retained,1);
    effect(retained);
    verify(retained,1);
    claim=open(true);
    verify(retained,2);
    // This callback must use the retained descriptor only. A tool that reopens
    // the device belongs in its own handoff, after this claim has been released.
    readback(retained);
}
}
