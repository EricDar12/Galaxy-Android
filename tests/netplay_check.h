// Shared assertion for the netplay suite. Both of its translation units stop
// the binary on the first failure, so a rejected packet or an incoherent peer
// address cannot be hidden by a later test.
#pragma once

#include <cstdio>
#include <cstdlib>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

// Defined in test_netplay_packets.cpp and called from the suite's main().
void runNetplayPacketChecks();
