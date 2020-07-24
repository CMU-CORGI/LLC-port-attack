/*
 * CPU Cache Stats
 *
 * Intel Xeon E5-2650 v4
 *
 * Size (https://en.wikichip.org/wiki/intel/xeon_e5/e5-2650_v4):
 * L1I$ 384 KiB	 12x32 KiB  8-way set associative (per core, write-back)
 * L1D$ 384 KiB	 12x32 KiB  8-way set associative (per core, write-back)
 * L2$    3 MiB	12x256 KiB  8-way set associative (per core, write-back)
 * L3$   30 MiB	12x2.5 MiB 20-way set associative (shared, per core, write-back)
 *
 * Latency (https://www.7-cpu.com/cpu/Broadwell.html):
 * L1D$ 4 cycles (for simple access via pointer)
 *      5 cycles (for access with complex address calculation:
 *                size_t n, *p; n = p[n])
 * L2$ 12 cycles
 * L3$ 40-70 cycles
 * RAM 100+ cycles
*/

#pragma once

#include <cstdint>

const uint64_t KiB = 1024;
const uint64_t MiB = KiB * KiB;
const uint64_t GiB = MiB * KiB;
const uint64_t CACHE_LINE_SIZE = 64; // bytes
const uint64_t LLC_BANKS = 12;
const uint64_t WAYS_PER_BANK = 20;
const uint64_t SETS_PER_BANK = 2048;

// Each LLC bank has 2048 sets. That means there are 11 index bits (6->16).
// The lower 6 bits (0->5) are the cache line offset.
// These constants allow us to check whether a given address maps to a specific
// set and whether it is aligned to a cache line.
const uint64_t SET_INDEX_BITS = 0b11111111111000000;
const uint64_t CACHE_LINE_BITS = 0b111111;
const uint64_t NUM_SET_INDEX_BITS = 11;
const uint64_t NUM_CACHE_LINE_BITS = 6;

// Need to allocate an array larger than twice the LLC size.
// LLC is 30 MiB (per socket), so we allocate a 64 MiB array.
const uint64_t ARRAY_SIZE = 64 * MiB;
const uint64_t ARRAY_ENTRIES = ARRAY_SIZE / CACHE_LINE_SIZE;

// Threshold for determining whether a cache access missed in the LLC.
// Based on profiling, an LLC hit is ~40 cycles, and a miss is ~170 cycles.
const uint64_t LLC_CYCLE_THRESHOLD = 100;

// The conflict set consists of the number of cache lines which fully fill a
// specific cache set across all LLC banks.
const uint64_t CONFLICT_SET_SIZE = LLC_BANKS * WAYS_PER_BANK;

// Cache line-sized struct.
struct __attribute__((packed, aligned(64))) Node {
    // "next" and "prev" are indices into the array for neighboring nodes to
    // visit.
    Node* next;
    Node* prev;
    uint64_t padding[6];
};
