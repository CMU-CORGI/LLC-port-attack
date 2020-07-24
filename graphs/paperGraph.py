#!/usr/bin/python3

# This scripts generates the graph used in
# Jumanji: The Case for Dynamic NUCA in the Datacenter, MICRO 2020

from matplotlib.patches import Ellipse
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

resultsDirectory = "../results/2020.micro.jumanji/"
figsDirectory = "figs/"

resultsFilename = resultsDirectory + "constant_access_times_"
graphFilename = figsDirectory + "portAttack-sampled.pdf"

# This needs to be set according to the attack code which generated the results.
# For the attack demonstrated in the paper, we recorded the time for every 100
# LLC accesses.
accessesPerDataPoint = 100

# The number of accesses from the beginning and end of the results trace to not
# graph. This is just for visualization purposes. Tune it for the specific
# results being graphed.
accessesToDropFromBeginning = None
accessesToDropFromEnd = int(60000000 / accessesPerDataPoint)

# Number of victim threads to compare against.
victimThread = 3
threads = [0, victimThread]

# There are too many data points to graph, so we only graph one of every
# "sampleRate" points.
sampleRate = 1000

# For debugging purposes only. Doesn't affect the graph.
outlierLimit = 33


def GetResults():
    # Each entry is a list of results for a given number of victim threads.
    results = {}

    print("Outlier latency limit (for debugging only): %i cycles" %
          outlierLimit)

    for thread in threads:
        results[thread] = []
        filename = resultsFilename + str(thread) + "_threads.txt"

        with open(filename, "r") as resultsFile:
            accesses = int(resultsFile.readline())
            numOutliers = 0  # For debugging only

            for _ in range(accesses):
                result = float(resultsFile.readline()) / accessesPerDataPoint
                results[thread].append(result)

                if result > outlierLimit:
                    numOutliers += 1

            print("Victim threads: %i, Outliers: %i/%i = %f%%" %
                  (thread, numOutliers, accesses, numOutliers / accesses * 100))

    return results


def GraphResults(results):
    labelFontSize = 15
    textFontSize = 13
    legendFontSize = 11
    alphaValue = 0.5

    fig, ax = plt.subplots(figsize=(7,4))

    # First graph the results with no victim threads.
    ax.plot(results[0], label="W/O Victim", ls="none", marker=".",
            alpha=alphaValue)

    # Then graph the results showing the port attack.
    ax.plot(results[victimThread], label="With Victim", ls="none", marker=".",
            alpha=alphaValue)

    ax.margins(x=0)
    ax.set_ylim(bottom=29.5, top=33)

    ax.set_ylabel("Attacker LLC Access Time\n(CPU Cycles)",
                  fontsize=labelFontSize)
    ax.set_xlabel("Access Count", fontsize=labelFontSize)
    ax.tick_params(axis="both", labelsize=labelFontSize)

    # Replace tick labels.
    M = 1000000 # Present x-ticks as millions
    labels = [int(x * accessesPerDataPoint * sampleRate / M)
              for x in ax.get_xticks().tolist()]
    labels = [str(x) + "M" if x != 0 else "0" for x in labels]
    ax.set_xticks(ax.get_xticks())
    ax.set_xticklabels(labels)

    # Ticks and gridlines
    ax.grid(b=True, which="major", axis="y", linestyle="-", color="gray")
    ax.minorticks_on()
    ax.grid(b=True, which="minor", axis="y", linestyle=":", color="gray")
    #ax.yaxis.set_major_locator(ticker.MultipleLocator(0.5))

    def AddCircle(x, y, width, height, color):
        circle = Ellipse(xy=(x,y), width=width, height=height, color=color,
                         fill=False, linewidth=2)
        ax.add_artist(circle)

    # Draw a circle around the results indicating a successful attack.
    AddCircle(x=3930, y=32.4, width=200, height=1.2, color="green")

    # Add text next to the circle.
    ax.text(x=1900, y=32.55,
            s="Attacker detects victim\naccessing target bank!",
            fontsize=textFontSize, color="green", fontweight="bold")

    # Add a checkmark next to circle to really emphasize it.
    ax.scatter(x=4180, y=32.6, marker="$\checkmark$", color="green",
               s=700)

    # Draw a circle around all the other banks.
    AddCircle(x=2000, y=31.7, width=3600, height=0.8, color="red")

    # Add text indicating the banks not being attacked.
    ax.text(x=800, y=31.1, s="Victim accesses each other bank",
            fontsize=textFontSize, color="red", fontweight="bold")

    plt.legend(fontsize=legendFontSize, loc="upper left", markerscale=3)
    fig.tight_layout()
    plt.savefig(graphFilename)


if __name__ == "__main__":
    results = GetResults()

    # Drop excess accesses from the beginning.
    if accessesToDropFromBeginning:
        for i in results:
            results[i] = results[i][accessesToDropFromBeginning:]

    # Drop excess accesses from the end.
    if accessesToDropFromEnd:
        for i in results:
            results[i] = results[i][:(len(results[i]) - accessesToDropFromEnd)]

    # Reduce the number of data points to a graphable amount.
    for i in threads:
        results[i] = results[i][::sampleRate]

    # Create graph.
    GraphResults(results)
