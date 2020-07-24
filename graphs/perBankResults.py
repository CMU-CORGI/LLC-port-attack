#!/usr/bin/python3

# Graphs the results which are broken down per bank accessed by the victim
# thread(s). A separate plot is created for each number of victim threads. Can
# be used to verify that the bank which seems to be attacked according to the
# main graph actually is being attacked because as the victim threads' pressure
# increases with the number of threads, the attacker's access times for that
# specific bank becoming increasingly larger compared to the other banks.

import matplotlib.pyplot as plt
import numpy as np
import sys

from scipy.stats import zscore

NUM_BANKS = 12
maxNumberOfThreads = 10
BINS = 50

resultsFilename = "../results/2020.micro.jumanji/per_bank_access_times_"
graphFilename = "figs/perBank.pdf"


def GetResults():
    # Indexed by number of victim threads.
    results = []

    for threads in range(11):
        filename = resultsFilename + str(threads) + "_threads.txt"

        # List of results where each entry is a list of results for a given
        # bank.
        results.append([])

        with open(filename, "r") as resultsFile:
            for bank in range(NUM_BANKS):
                accessesForBank = int(resultsFile.readline())

                # List of results for a given bank.
                results[-1].append([])

                for _ in range(accessesForBank):
                    result = int(resultsFile.readline())
                    results[-1][-1].append(result)

                # Corner case for 0 victim threads. It only has one list of results.
                if threads == 0:
                    break

    return results


def GraphResults(results):
    fig, axes = plt.subplots(nrows=5, ncols=2, figsize=(15,8))

    for threads in range(1, maxNumberOfThreads + 1):
        axesX = int((threads - 1) / 2)
        axesY = int((threads - 1) % 2)
        ax = axes[axesX][axesY]

        numColors = NUM_BANKS
        colormap = plt.cm.nipy_spectral
        colors = [colormap(i) for i in np.linspace(0, 1, len(results[threads]))]
        ax.set_prop_cycle('color', colors)

        ax.hist(results[threads], bins=BINS, histtype="step", label=range(len(results[threads])))
        ax.set_xlabel("# Victim threads: " + str(threads))
        #ax.legend()

    plt.tight_layout()
    plt.savefig(graphFilename)


def RemoveOutliers(x):
    x = np.array(x)
    isOutlier = zscore(x) > 0.5
    x = x[~isOutlier]
    return x


if __name__ == "__main__":
    results = GetResults()

    # Remove outliers from results
    for thread in range(len(results)):
        for bank in range(len(results[thread])):
            results[thread][bank] = RemoveOutliers(results[thread][bank])


    GraphResults(results)
