// This file is heavily based on
//   F. Liu, Y. Yarom, Q. Ge, G. Heiser and R. B. Lee,
//   "Last-Level Cache Side-Channel Attacks are Practical,"
//   2015 IEEE Symposium on Security and Privacy, San Jose, CA, 2015,
//   pp. 605-622, doi: 10.1109/SP.2015.43.
// section IV.A

// To run (with huge pages):
// $ LD_PRELOAD=libhugetlbfs.so HUGETLB_MORECORE=yes "binary"
//
// Very rarely the probe function may not terminate due to never recording a
// plausible load time. This should happen infrequently based on outside factors
// (e.g., frequent thread context switching). Just rerun the program.

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <x86intrin.h> // For rdtsc()

#include "constants.h" // Contains CPU-specific properties and "Node" definition

// Returns the number of entries in the linked list.
// Assumes the linked list is closed (wraps around).
uint64_t SizeOfLinkedList(const Node* node) {
    if (node == node->next) {
        return 1;
    }

    const Node* const head = node;
    node = node->next;
    uint64_t size = 1;

    while (node != head) {
        node = node->next;
        ++size;
    }

    return size;
}

// Determine the average access latency for all the elements in the provided
// linked list. Sanity check that the accesses are missing to DRAM.
void SanityCheckCandidates(Node* candidateSetNode, uint64_t& garbage) {
    const uint64_t iterations = 100000 * LLC_BANKS * WAYS_PER_BANK;

    register Node* currentNode = candidateSetNode;
    register uint64_t i = 0;
    uint64_t time;

    unsigned int unused;

    _mm_lfence();
    time = __rdtsc();

    while (i++ < iterations) {
        currentNode = currentNode->next;
    }

    _mm_lfence();
    time = __rdtsc() - time;

    time /= iterations;

    std::cout << "Average candidate access time: " << time << std::endl;

    // DRAM access time usually ~175-180 for Intel Xeon E5-2650 v4.
    // May need to adjust for other processors.
    assert(time >= 165);
    assert(time <= 190);

    std::cout << "Validated candidates miss to DRAM" << std::endl;

    garbage += currentNode->padding[0];
}

// Determine the average access latency for all the elements in the provided
// linked list. Sanity check that the accesses are all hitting in the LLC.
void SanityCheckConflictSet(Node* conflictSet, uint64_t& garbage) {
    const uint64_t iterations = 10000 * LLC_BANKS * WAYS_PER_BANK;

    register Node* currentNode = conflictSet;
    register uint64_t i = 0;
    uint64_t time;

    _mm_lfence();
    time = __rdtsc();

    for (uint64_t i = 0; i < iterations; ++i) {
        currentNode = currentNode->next;
    }

    _mm_lfence();
    time = __rdtsc() - time;

    time /= iterations;

    std::cout << "Average access time for conflict set: " << time << std::endl;

    // Average LLC access time is ~40 cycles for Intel Xeon E5-2650 v4.
    // May need to adjust for other processors.
    assert(time > 30 && time < 50);

    std::cout << "Validated conflict set access time" << std::endl;

    garbage += currentNode->padding[0];
}

// Multiple sanity checks on the eviction sets.
// - All sets are disjoint
// - All sets are the correct size
// - All sets' accesses hit in the LLC.
void SanityCheckEvictionSets(std::vector<Node*> evictionSetHeads,
                             uint64_t& garbage) {
    // Check that all sets are disjoint and the correct size.
    std::set<Node*> allNodes;
    for (uint64_t i = 0; i < evictionSetHeads.size(); ++i) {
        Node* node = evictionSetHeads[i];
        allNodes.insert(node);
        uint64_t evictionSetSize = 1;

        node = node->next;
        while (node != evictionSetHeads[i]) {
            allNodes.insert(node);
            ++evictionSetSize;
            node = node->next;
        }

        // std::cout << "Eviction set " << i << " size: " << evictionSetSize
        //           << std::endl;
        assert(evictionSetSize == WAYS_PER_BANK);
    }

    assert(allNodes.size() == CONFLICT_SET_SIZE);

    std::cout << "Validated size of each eviction set" << std::endl;
    std::cout << "Validated eviction sets are disjoint" << std::endl;

    // Now check access time for each full eviction set.
    const uint64_t iterations = 10000 * LLC_BANKS * WAYS_PER_BANK;

    for (uint64_t j = 0; j < evictionSetHeads.size(); ++j) {
        register Node* currentNode = evictionSetHeads[j];
        register uint64_t i = 0;
        uint64_t time;

        _mm_lfence();
        time = __rdtsc();

        for (uint64_t i = 0; i < iterations; ++i) {
            currentNode = currentNode->next;
        }

        _mm_lfence();
        time = __rdtsc() - time;

        time /= iterations;

        std::cout << "Average access time for eviction set " << j + 1 << ": "
                  << time << std::endl;

        // LLC access time averages about 40 cycles, but it strongly depends on
        // bank location. Now that each eviction set contains nodes in a
        // specific bank, the range in access times across eviction sets will
        // vary noticably. I generally see times ranging from ~28-48.
        assert(time > 25 && time < 55);

        garbage += currentNode->padding[0];
    }

    std::cout << "Validated access time for each full eviction set"
              << std::endl;
}

// Determine the indexes into the array (on a cache line boundary) whose
// addresses indicate they map into the given cache set.
void FindCandidates(Node* array, std::set<Node*>& candidates,
                    uint64_t setIndex) {
    for (uint64_t i = 0; i < ARRAY_ENTRIES; ++i) {
        Node* nodeAddress = &(array[i]);
        // std::cout << nodeAddress << std::endl;

        const uintptr_t address = reinterpret_cast<uintptr_t>(nodeAddress);

        // Sanity check that nodes are cache-line aligned.
        assert((address & CACHE_LINE_BITS) == 0);

        // Add the node as a candidate if it maps to the provided cache set.
        if ((address & SET_INDEX_BITS) == (setIndex << NUM_CACHE_LINE_BITS)) {
            candidates.insert(nodeAddress);
        }
    }
}

void RandomizeLinkedList(const std::set<Node*>& candidates) {
    // Put the candidates into a list.
    std::vector<Node*> candidateList;
    for (auto it = candidates.begin(); it != candidates.end(); ++it) {
        candidateList.push_back(*it);
    }
    //std::cout << "List size: " << candidateList.size() << std::endl;

    // Create a list from 0 -> candidates.size() - 1. Randomly permute the list.
    std::vector<uint64_t> permutation(candidateList.size());
    for (uint64_t i = 0; i < permutation.size(); ++i) {
        permutation[i] = i;
    }
    std::random_shuffle(permutation.begin(), permutation.end());

    // Use the permutation to create the permuted linked list which loops among
    // all the "candidates".
    //
    // We achieve this by taking the path of values in numerical order of the
    // shuffled permutation.
    //
    // We start by creating a dictionary from value -> index in the shuffled
    // permutation where it occurs.
    std::map<uint64_t, uint64_t> dictionary;
    for (uint64_t i = 0; i < permutation.size(); ++i) {
        dictionary[permutation[i]] = i;
    }

    // Now apply this permutation to create a linked list through all of the
    // "candidates".
    for (uint64_t i = 0; i < permutation.size(); ++i) {
        // Get the position in permutation of the values "i" and "i+1".
        const uint64_t startIndex = dictionary[i];
        const uint64_t endIndex = dictionary[(i + 1) % permutation.size()];

        // Use the permutation indices to extract their respective candidate
        // nodes.
        Node* startNode = candidateList[startIndex];
        Node* endNode = candidateList[endIndex];

        // "startNode" needs to point to "endNode".
        startNode->next = endNode;
        endNode->prev = startNode;
    }
}

// This function is heavily based on Algorithm 1 in the paper mentioned at the
// top.
//
// This version of "Probe()" iterates over the set many times because iterating
// over the set only once often does not cause the candidate to be evicted by
// the replacement policy, even if the set contains WAYS_PER_BANK nodes in the
// candidate's bank. This would be because the replacement policy evicts a
// member of the set, whereas we want all set nodes to reside in the LLC when we
// re-probe the candidate.
//
// "garbage" is solely for preventing the compiler from optimizing out parts of
// this function.
bool Probe(register Node* setStartNode, const Node* candidate,
           uint64_t& garbage, const bool printOutput) {
    register Node dummy1, dummy2;
    register Node* currentNode = setStartNode;
    uint64_t time = 0;

    // To deal with weird occasional timing results, repeat until we get a
    // number in a believable range.
    uint64_t attempt = 0;

    while (time < 20 || time > 200) {
        // First iterate over the linked list many times to make sure any old
        // values not in the linked list are evicted from the LLC banks.
        const uint64_t iterations = 100 * WAYS_PER_BANK * LLC_BANKS;
        for (uint64_t i = 0; i < iterations; ++i) {
            currentNode = currentNode->next;
        }
        _mm_lfence();

        // Then read the candidate to insert it into the LLC.
        dummy1 = *candidate;
        _mm_lfence();

        // Once again iterate over the linked list many times to make sure that
        // the linked list's nodes evict the candidate (if there are
        // WAYS_PER_BANK nodes in the bank which contains the candidate).
        //
        // Note: I tried iterating over the list forwards and backwards, but it
        // did not provide any better results and actually greatly increased
        // total runtime (maybe due to context switching between the separate
        // iterations? I'm not sure).
        for (uint64_t i = 0; i < iterations; ++i) {
            currentNode = currentNode->next;
        }

        // Measure the time to reread the candidate to determine whether it is
        // still cached (in the LLC or lower).
        _mm_lfence();
        time = __rdtsc();

        dummy2 = *candidate;

        _mm_lfence();
        time = __rdtsc() - time;

        if (printOutput) {
            if (attempt > 0) {
                std::cout << std::endl;
            }
            std::cout << "Attempt: " << attempt++ << ", time: " << time;
        }

        // Need to use the dummy and index variables in order to not be
        // optimized out by the compiler.
        garbage += dummy1.padding[0] + dummy2.padding[0] +
            currentNode->padding[0];
    }

    return time > LLC_CYCLE_THRESHOLD;
}

std::vector<Node*> GetEvictionSet(Node** array, const uint64_t setIndex) {
    srand(0);
    assert(*array == nullptr);

    // Ensure that each node occupies exactly one cache line.
    assert(sizeof(Node) == CACHE_LINE_SIZE);

    // Ensure that a valid set index is provided.
    assert(setIndex < SETS_PER_BANK);

    // Only needed to prevent compiler optimizations.
    uint64_t garbage = 0;

    // Allocate our full buffer which is at least twice the size of the LLC.
    // Need to use the heap to be mapped into huge pages.
    // Array needs to be aligned on a cache line so that each node occupies a
    // distinct and full cache line.
    assert(*array == nullptr);
    *array = static_cast<Node*>(aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE));

    // Determine the nodes in the array (on a cache line boundary) whose
    // addresses indicate they map into a given set of an LLC bank.
    // This set is called "lines" in Algorithm 1 in the paper mentioned above.
    std::set<Node*> candidates;
    FindCandidates(*array, candidates, setIndex);
    std::cout << "Number of candidates: " << candidates.size() << std::endl;

    // Make sure we have enough candidates.
    assert(candidates.size() >= 2 * LLC_BANKS * WAYS_PER_BANK);

    // for (auto it = candidates.begin(); it != candidates.end(); ++it) {
    //     std::cout << *it << " " << std::bitset<8*sizeof(int*)>
    //         (reinterpret_cast<long>(*it)) << std::endl;
    // }

    // Need to create a randomized linked list among the "candidates" in "array"
    // so that accessing nodes in order does not trigger prefetching.
    RandomizeLinkedList(candidates);

    // Verify there is a linked list through all of the "candidates" in "array".
    uint64_t count = SizeOfLinkedList(*candidates.begin());
    assert(count == candidates.size());
    std::cout << "Entries in linked list: " << count << std::endl;

    // Sanity check that the candidates are all in the same cache set. An
    // empirical method is iterating through all candidates and ensuring that
    // they do miss in the LLC.
    SanityCheckCandidates(*candidates.begin(), garbage);

    // Determine a conflict set from the candidates in "array". A conflict set
    // contains LLC_BANKS * WAYS_PER_BANK nodes which consists of LLC_BANKS
    // groups of nodes (each of size WAYS_PER_BANK), each of which maps to a
    // distinct LLC bank.
    //
    // We will later separate the conflict set into disjoint eviction sets which
    // each map to a different LLC bank.
    //
    // In order to enable traversing through the conflict set without pollution
    // from other accesses, the conflict set will be managed by a separate
    // linked list within "array". Moving a node from the candidate set to the
    // conflict set just involves changing node (next & prev) pointers. The only
    // required metadata is a head node pointer for each of the lists.
    // (Note that "head" is a meaningless term here. We just need a pointer to
    // any node in each of the disjoint lists.)
    Node *candidateSetHead, *conflictSetHead;

    // Arbitrarily pick the first WAYS_PER_BANK candidates to move to the
    // conflict set because you need at least WAYS_PER_BANK + 1 nodes in order
    // to overfill a set in a single LLC bank.
    conflictSetHead = *candidates.begin();

    candidateSetHead = conflictSetHead;
    for (uint64_t i = 0; i < WAYS_PER_BANK; ++i) {
        candidateSetHead = candidateSetHead->next;
    }

    // Move the nodes from the candidate set to the conflict set.
    Node* conflictSetTail = candidateSetHead->prev;
    Node* candidateSetTail = conflictSetHead->prev;
    candidateSetHead->prev = candidateSetTail;
    candidateSetTail->next = candidateSetHead;
    conflictSetHead->prev = conflictSetTail;
    conflictSetTail->next = conflictSetHead;

    // Verification that the conflict set is the correct size.
    count = SizeOfLinkedList(conflictSetHead);
    assert(count == WAYS_PER_BANK);

    // Verification that the candidate set is the correct size.
    count = SizeOfLinkedList(candidateSetHead);
    assert(count == candidates.size() - WAYS_PER_BANK);

    // Probe every candidate to determine whether to add them to the conflict
    // set.
    count = WAYS_PER_BANK;
    Node* candidate = candidateSetHead;

    // Call Probe() a few times to warmup the caches.
    for (uint64_t i = 0; i < 10; ++i) {
        Probe(conflictSetHead, candidate, garbage, /*printOutput=*/false);
        // std::cout << " (warmup)" << std::endl;
    }

    // Now perform the true probes until we fill the conflict set.
    while (count < CONFLICT_SET_SIZE) {
        const bool missToDRAM =
            Probe(conflictSetHead, candidate, garbage, /*printOutput=*/false);
        if (!missToDRAM) {
            ++count;
            // std::cout << ", Added to set: " << candidate << ", size: "
            //           << count << std::endl;

            Node* nextCandidate = candidate->next;

            // Remove "candidate" from the candidate set.
            Node* next = candidate->next;
            Node* prev = candidate->prev;
            next->prev = prev;
            prev->next = next;
            if (candidate == candidateSetHead) {
                candidateSetHead = next;
            }

            // Add "candidate" to the conflict set.
            next = conflictSetHead;
            prev = conflictSetHead->prev;
            candidate->next = next;
            candidate->prev = prev;
            next->prev = candidate;
            prev->next = candidate;

            candidate = nextCandidate;
        } else {
            // std::cout << ", Skipped candidate: " << candidate << std::endl;
            candidate = candidate->next;
        }
    }

    // Verify the size of the conflict set.
    count = SizeOfLinkedList(conflictSetHead);
    std::cout << "Conflict set size: " << count << ", should be "
              << CONFLICT_SET_SIZE << std::endl;
    assert(count == CONFLICT_SET_SIZE);

    // Verify that accessing nodes in the conflict set always hits in the LLC.
    SanityCheckConflictSet(conflictSetHead, garbage);

    // Verify the size of the candidate set.
    count = SizeOfLinkedList(candidateSetHead);
    std::cout << "Remaining candidate set size: " << count << ", should be "
              << candidates.size() - CONFLICT_SET_SIZE << std::endl;
    assert(count == candidates.size() - CONFLICT_SET_SIZE);


    // Now we need to separate the conflict set into separate eviction sets for
    // each LLC bank.
    // std::cout << "Starting eviction set construction" << std::endl;

    // This will contain a node pointer into each of the final eviction sets.
    std::vector<Node*> evictionSetHeads;

    // Pick an arbitrary candidate.
    candidate = candidateSetHead;

    // This code is a sanity check that every candidate will miss to DRAM
    // when probed with the conflict set.
    // std::cout << std::endl << "Probing every candidate against conflict set:"
    //           << std::endl;
    // do {
    //     bool missToDRAM = Probe(conflictSetHead, candidate, garbage,
    //                             /*printOutput=*/true);
    //     std::cout << std::endl;
    //     candidate = candidate->next;
    // }
    // while (candidate != candidateSetHead);
    // std::cout << "Done probing candidates" << std::endl << std::endl;

    // Once we have determined all the eviction sets except the last one, all
    // the remaining nodes in the conflict set are implicitly the final eviction
    // set.
    while(evictionSetHeads.size() < LLC_BANKS - 1) {
        // First find a node which was not inserted into the conflict set
        // (i.e., still in the candidate set) which maps to the same cache set
        // as nodes still in the conflict set. We do this by probing candidate
        // nodes one at a time with the conflict set.
        bool foundCandidate = false;

        while (!foundCandidate) {
            bool missToDRAM = Probe(conflictSetHead, candidate, garbage,
                                    /*printOutput=*/false);
            // std::cout << ", " << candidate << std::endl;
            while (!missToDRAM) {
                // The candidate hit in the LLC, so we no longer want to
                // consider it. Remove it from the candidate set.
                //
                // And make sure there are still other candidates to check.
                assert(candidate->next != candidate);

                candidate->prev->next = candidate->next;
                candidate->next->prev = candidate->prev;
                candidate = candidate->next;

                // Test the next candidate;
                missToDRAM = Probe(conflictSetHead, candidate, garbage,
                                   /*printOutput=*/false);
                // std::cout << ", " << candidate << std::endl;
            }

            // std::cout << "Found possible candidate" << std::endl;

            // We possibly found a candidate to use to find an eviction set.
            // However, we sometimes get artificially long probe times due to
            // factors besides an LLC miss (e.g., thread context switch). Probe
            // the possible candidate many more times to make sure it really is
            // causing an LLC miss.
            foundCandidate = true;

            for (uint64_t i = 0; i < 100; ++i) {
                missToDRAM = Probe(conflictSetHead, candidate, garbage,
                                   /*printOutput=*/false);
                if (!missToDRAM) {
                    // We got a cache hit. Remove the previous candidate from
                    // the candidate set and move on to the next candidate.
                    //
                    // And make sure there are still other candidates to check.
                    assert(candidate->next != candidate);

                    candidate->prev->next = candidate->next;
                    candidate->next->prev = candidate->prev;
                    candidate = candidate->next;
                    foundCandidate = false;
                    break;
                }
            }
        }

        // std::cout << " - Confirmed candidate for eviction set probing"
        //           << std::endl;

        // Now we will use this candidate to find an eviction set. Remove test
        // nodes one at a time from the conflict set and retry the probe. If the
        // probe does not miss to DRAM, then we know that the test node maps to
        // the same set as the candidate. Keep probing test nodes from the
        // conflict set until we find all WAYS_PER_BANK conflict set nodes which
        // map to the same set as the candidate node. This forms an eviction
        // set. Although we cannot tell which specific bank this eviction set
        // maps to, we do know all the nodes in the set do map to the same bank.
        std::set<Node*> evictionSet;

        // Start by testing the conflict set head.
        Node* testNode = conflictSetHead;

        while (evictionSet.size() < WAYS_PER_BANK) {
            // Go to the next node if the test node has already been added to
            // the eviction set (this can happen if we loop around the entire
            // conflict set without finding the full eviction set yet).
            if (evictionSet.find(testNode) != evictionSet.end()) {
                // Test the next conflict set node.
                testNode = testNode->next;
                continue;
            }

            // Temporarily remove the test node from the conflict set.
            testNode->prev->next = testNode->next;
            testNode->next->prev = testNode->prev;

            const bool missToDRAM =
                Probe(testNode->next, // The node after the test node will still
                                      // be in the conflict set (whereas the
                                      // conflict set head might not be, if it
                                      // is currently the test node).
                      candidate, garbage, /*printOutput=*/false);

            // Insert the test node back into the conflict set.
            testNode->prev->next = testNode;
            testNode->next->prev = testNode;

            if (!missToDRAM) {
                // Add the node to the eviction set.
                evictionSet.insert(testNode);
                // std::cout << "Eviction set " << evictionSetHeads.size() + 1
                //           << " size: " << evictionSet.size() << std::endl;
            }

            // Test the next conflict set node.
            testNode = testNode->next;
        }

        // We have found an entire eviction set. Move the nodes out of the
        // conflict set and connect them into their own linked list.
        Node* evictionSetHead = *evictionSet.begin();
        if (evictionSetHead == conflictSetHead) {
            conflictSetHead = conflictSetHead->next;
        }

        evictionSetHead->next->prev = evictionSetHead->prev;
        evictionSetHead->prev->next = evictionSetHead->next;
        evictionSetHead->next = evictionSetHead;
        evictionSetHead->prev = evictionSetHead;

        for (auto it = ++evictionSet.begin(); it != evictionSet.end(); ++it) {
            // Remove the node from the conflict set and add it to the back of
            // the eviction set.
            Node* node = *it;

            if (node == conflictSetHead) {
                conflictSetHead = conflictSetHead->next;
            }

            node->next->prev = node->prev;
            node->prev->next = node->next;
            node->next = evictionSetHead;
            node->prev = evictionSetHead->prev;
            node->next->prev = node;
            node->prev->next = node;
        }

        evictionSetHeads.push_back(evictionSetHead);

        std::cout << "Found eviction set: " << evictionSetHeads.size()
                  << std::endl;

        // We can now remove this candidate from its set.
        assert(candidate->next != candidate);

        candidate->prev->next = candidate->next;
        candidate->next->prev = candidate->prev;
        candidate = candidate->next;
    }

    // The remaining nodes in the conflict set now compose the final eviction
    // set.
    evictionSetHeads.push_back(conflictSetHead);
    std::cout << "Remaining nodes form eviction set: "
              << evictionSetHeads.size() << std::endl;

    // Perform sanity checks on the eviction sets.
    SanityCheckEvictionSets(evictionSetHeads, garbage);

    // Need to use "garbage" to prevent compiler optimizing it out.
    std::cout << "(Garbage: " << garbage << ")" << std::endl;

    return evictionSetHeads;
}
