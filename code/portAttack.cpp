#include <cassert>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>

#include "constants.h"
#include "constructingEvictionSet.h"

const uint64_t VICTIM_ITERATIONS = 5000000;
const uint64_t ATTACKER_WARMUP_ACCESSES = 50000000;
const uint64_t ATTACKER_TIMED_ITERATIONS = 5000000;
const uint64_t ATTACKER_ACCESSES_PER_ITERATION = 100;

// Run the attack once for every number of victim threads up to this value.
const uint64_t MAX_NUM_VICTIM_THREADS = 10;

// Cache sets can be arbitrary, as long as they are different.
const uint64_t CACHE_SET_ATTACKER = 27;
const uint64_t CACHE_SET_VICTIM = 1898;

// Needs to match the logical cores being used in the Makefile.
const uint64_t coreIDs[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
                            24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

// Allocating this inside main (a few separate times) caused immediate
// segfaults. I read this is possible from attempting to allocate data
// structures larger than the stack. This structure isn't that big, but moving
// it out and allocating it just this once did solve the problem.
uint64_t attackerTimesArray[ATTACKER_TIMED_ITERATIONS];

double AverageAttackerTimes(const uint64_t* times) {
    double total = 0;
    for (uint64_t i = 0; i < ATTACKER_TIMED_ITERATIONS; ++i) {
        total += times[i];
    }
    return total / (ATTACKER_TIMED_ITERATIONS * ATTACKER_ACCESSES_PER_ITERATION);
}

void CreateEvictionSets(Node** array, std::vector<Node*>* evictionSets,
                        uint64_t setIndex) {
    *evictionSets = GetEvictionSet(array, setIndex);
}

void GetAttackerClosestBank(std::vector<Node*> evictionSetsAttacker,
                            uint64_t* garbage, int coreID,
                            uint64_t* closestBank) {
    // Set core affinity.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreID, &cpuset);
    pthread_t currentThread = pthread_self();
    pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset);

    // Find the closest bank.
    *closestBank = -1;
    uint64_t shortestTime = -1;
    uint64_t time;

    // Enough iterations for a stable result.
    const uint64_t iterations = 10000000;

    for (uint64_t bank = 0; bank < LLC_BANKS; ++bank) {
        Node* node = evictionSetsAttacker[bank];

        _mm_lfence();
        time = __rdtsc();

        for (uint64_t i = 0; i < iterations; ++i) {
            node = node->next;
        }

        _mm_lfence();
        time = __rdtsc() - time;

        if (time < shortestTime) {
            shortestTime = time;
            *closestBank = bank;
        }

        *garbage += node->padding[0];
    }

    std::cout << "Found closest eviction set " << *closestBank
              << " for attacker. Average access time: "
              << static_cast<double>(shortestTime) / iterations << std::endl;
}

void IterateThroughSetAttacker(Node* node, uint64_t* times, uint64_t* garbage,
                               int coreID) {
    // Set core affinity.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreID, &cpuset);
    pthread_t currentThread = pthread_self();
    pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset);

    // std::stringstream ss;
    // ss << "My core: " << sched_getcpu() << ", should be: " << coreID
    //    << std::endl;
    // std::cout << ss.str();

    // Warmup iterations.
    for (uint64_t i = 0; i < ATTACKER_WARMUP_ACCESSES; ++i) {
        node = node->next;
    }

    // Timed iterations.
    for (uint64_t i = 0; i < ATTACKER_TIMED_ITERATIONS; ++i) {
        _mm_lfence();

        for (uint64_t j = 0; j < ATTACKER_ACCESSES_PER_ITERATION; ++j) {
            node = node->next;
        }

        _mm_lfence();
        times[i] = __rdtsc();
    }

    *garbage += node->padding[0];

    std::cout << "Attacker finished" << std::endl;
}

void IterateThroughSetVictim(Node* node, uint64_t* time, uint64_t* garbage) {
    // Perform the iterations.
    _mm_lfence();
    *time = __rdtsc();

    for (uint64_t i = 0; i < VICTIM_ITERATIONS; ++i) {
        node = node->next;
    }

    _mm_lfence();
    *time = __rdtsc() - *time;

    *garbage += node->padding[0];
}

// NOTE: this function will probably segfault if the attacker finishes before
// all the victims do. I should put a check for that.
std::vector<uint64_t> SplitResultsIntoBanks(uint64_t victimBankBoundaries[24]) {
    std::vector<uint64_t> boundaries;

    // Current index into the attacker's accesses.
    uint64_t access = 0;

    for (uint64_t bank = 0; bank < LLC_BANKS; ++bank) {
        while (attackerTimesArray[access] < victimBankBoundaries[bank * 2]) {
            ++access;
        }

        // We found the starting boundary.
        boundaries.push_back(access);

        // std::cout << "Bank " << bank << " starting boundary: "
        //           << boundaries.back() << std::endl;

        // Find the last attacker access which occurred while the victim
        // accessed the first bank.
        while (attackerTimesArray[access] < victimBankBoundaries[bank * 2 + 1]) {
            ++access;
        }

        // We found the ending boundary.
        boundaries.push_back(access - 1);

        // std::cout << "Bank " << bank << " ending boundary: "
        //           << boundaries.back() << std::endl;
    }

    return boundaries;
}

int main(int argc, char* argv[]) {
    Node* arrayAttacker = nullptr;
    Node* arrayVictim = nullptr;
    uint64_t garbage;

    std::vector<Node*> evictionSetsAttacker, evictionSetsVictim;

    // Create the two groups of eviction sets. We cannot do this in parallel
    // with two threads because they would impact each other's timing
    // measurements.
    CreateEvictionSets(&arrayAttacker, &evictionSetsAttacker,
                       CACHE_SET_ATTACKER);
    CreateEvictionSets(&arrayVictim, &evictionSetsVictim, CACHE_SET_VICTIM);

    std::cout << "Made two groups of eviction sets for different cache sets."
              << std::endl;

    // Although it probably doesn't make much of a difference, let's find the
    // eviction set with the shortest access time for the attacker (i.e., its
    // local LLC bank) so that bank contention shows the biggest impact.
    //
    // Run in a spawned thread in order to set its core affinity without
    // affecting the main thread.
    uint64_t closestBank;
    std::thread threadProfiler(GetAttackerClosestBank, evictionSetsAttacker,
                               &garbage, coreIDs[0], &closestBank);
    threadProfiler.join();

    // Needed to prevent compiler optimizations.
    std::vector<uint64_t> garbageVictim(MAX_NUM_VICTIM_THREADS);

    // Perform attack for varying number of victim threads.
    for(uint64_t numVictimThreads = 0;
        numVictimThreads <= MAX_NUM_VICTIM_THREADS; ++numVictimThreads) {

        // std::cout << "Number of victim threads: "
        //           << numVictimThreads << std::endl;

        uint64_t victimBankBoundaries[24];

        // Start the attacker.
        std::thread threadAttacker(IterateThroughSetAttacker,
                                   evictionSetsAttacker[closestBank],
                                   attackerTimesArray, &garbage,
                                   coreIDs[0]);

        // Give some time for the warmup requests.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Access each bank of one eviction set a certain number of times with a
        // pause in between each bank.
        if (numVictimThreads > 0) {
            for (uint64_t bank = 0; bank < LLC_BANKS; ++bank) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));

                std::vector<uint64_t> timesVictim(numVictimThreads);

                victimBankBoundaries[2 * bank] = __rdtsc();

                std::vector<std::thread> threadVictim;
                for (uint64_t i = 0; i < numVictimThreads; ++i) {
                    threadVictim.push_back(std::thread(IterateThroughSetVictim,
                                                       evictionSetsVictim[bank],
                                                       &timesVictim[i],
                                                       &garbageVictim[i]));
                }

                for (uint64_t i = 0; i < numVictimThreads; ++i) {
                    threadVictim[i].join();
                }

                victimBankBoundaries[2 * bank + 1] = __rdtsc();

                // if (numVictimThreads > 0) {
                //     std::cout << "Finished flooding bank " << bank << std::endl;
                // }
            }

            std::cout << "Victim(s) done" << std::endl;
        }

        threadAttacker.join();

        // Create the output files. One which splits results by bank and another
        // which outputs all times for the attacker.
        std::ofstream filePerBank, fileConstant;
        filePerBank.open("../results/per_bank_access_times_" +
                         std::to_string(numVictimThreads) + "_threads.txt");
        fileConstant.open("../results/constant_access_times_" +
                          std::to_string(numVictimThreads) + "_threads.txt");
        assert(filePerBank.is_open());
        assert(fileConstant.is_open());

        // Output each timing difference to file. Only output results that
        // occurred during a victim access for "filePerBank".
        std::cout << "Start writing to files" << std::endl;

        // First write all times to "fileConstant".
        fileConstant << ATTACKER_TIMED_ITERATIONS - 1 << std::endl;
        for (uint64_t i = 1; i < ATTACKER_TIMED_ITERATIONS; ++i) {
            const uint64_t accessTime =
                attackerTimesArray[i] - attackerTimesArray[i - 1];
            fileConstant << accessTime << std::endl;
        }
        fileConstant.close();

        // Now write the per-bank results to "filePerBank".
        //
        // Corner case for 0 victim threads. Just write all results to file.
        if (numVictimThreads == 0) {
            // First output the number of results.
            filePerBank << ATTACKER_TIMED_ITERATIONS - 1 << std::endl;

            // Now output the actual results.
            for (uint64_t i = 1; i < ATTACKER_TIMED_ITERATIONS; ++i) {
                const uint64_t accessTime =
                    attackerTimesArray[i] - attackerTimesArray[i - 1];
                filePerBank << accessTime << std::endl;
            }

            filePerBank.close();
            std::cout << "Finish writing to files" << std::endl;

            std::cout << "Finished experiment with " << numVictimThreads
                      << " victim threads." << std::endl;

            continue;
        }

        // At least one victim thread. Determine boundaries.
        std::vector<uint64_t> boundaries =
            SplitResultsIntoBanks(victimBankBoundaries);

        // Output results results per-bank.
        for (uint64_t bank = 0; bank < LLC_BANKS; ++bank) {
            // First output the number of attacker accesses which occurred while
            // the victim accessed this bank.
            const uint64_t accesses =
                boundaries[2 * bank + 1] - boundaries[2 * bank] + 1;
            filePerBank << accesses << std::endl;

            // Then output values.
            for (uint64_t i = boundaries[2 * bank];
                 i <= boundaries[2 * bank + 1]; ++i) {
                filePerBank << attackerTimesArray[i] - attackerTimesArray[i - 1]
                            << std::endl;
            }
        }

        filePerBank.close();
        std::cout << "Finish writing to files" << std::endl;

        std::cout << "Finished experiment with " << numVictimThreads
                  << " victim threads." << std::endl;
    }

    delete [] arrayAttacker;
    delete [] arrayVictim;

    uint64_t finalGarbage = garbage;
    for (uint64_t i = 0; i < garbageVictim.size(); ++i) {
        finalGarbage += garbageVictim[i];
    }
    std::cout << "All done! (Garbage:" << finalGarbage << ")" << std::endl;

    return 0;
}
