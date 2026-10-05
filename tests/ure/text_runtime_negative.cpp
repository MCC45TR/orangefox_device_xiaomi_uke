// SPDX-License-Identifier: Apache-2.0
// Host-only sanitizer admission oracle; deliberately undefined and never shipped.
#include <climits>
#include <cstdio>
int main(int argc,char**) {
    volatile int maximum=INT_MAX;
    std::printf("%d\n",maximum+argc);
}
