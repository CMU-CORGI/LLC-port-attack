#!/usr/bin/python3

# This script generates the graph used in
# Jumanji: The Case for Dynamic NUCA in the Datacenter, MICRO 2020

from matplotlib.lines import Line2D
from matplotlib.patches import Ellipse
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

resultsDirectory = "../results/2020.micro.jumanji/"
figsDirectory = "figs/"

resultsFilename = resultsDirectory + "constant_access_times_"
graphFilename = figsDirectory + "portAttack.pdf"

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

# There are very few outliers, but we have to cut off the graph somewhere.
outlierLimit = 33


def GetResults():
    # Each entry is a list of results for a given number of victim threads.
    results = {}

    print("Outlier latency limit: %i cycles" % outlierLimit)

    for thread in threads:
        results[thread] = []
        filename = resultsFilename + str(thread) + "_threads.txt"

        with open(filename, "r") as resultsFile:
            accesses = int(resultsFile.readline())
            numOutliers = 0  # For debugging only

            for _ in range(accesses):
                result = float(resultsFile.readline()) / accessesPerDataPoint

                if result > outlierLimit:
                    numOutliers += 1

                    # Do not include outliers in the results vector because it
                    # messes with the contour resolution settings. Since the
                    # results with and without victim will certainly have
                    # different numbers of outliers, that means their data will
                    # be slightly out of sync, but by an unnoticeable amount.
                    # And results without the victim are practically constant
                    # anyways.
                    continue

                results[thread].append(result)

            print("Victim threads: %i, Outliers: %i/%i = %f%%" %
                  (thread, numOutliers, accesses, numOutliers / accesses * 100))

    return results


def GraphResults(results):
    labelFontSize = 15
    textFontSize = 13
    legendFontSize = 11
    alphaValue = 0.5
    contourLevels = 100

    fig, ax = plt.subplots(figsize=(7,4))

    ax.margins(x=0)

    # Granularity of grouping data along each axis for the contour plot.
    xSlices = 500
    ySlices = 25

    ax.set_ylim(bottom=0, top=ySlices)

    # Values are floating point, but we obviously need to use integer indices
    # into an array, so we multiply all values by the accesses per data point
    # (undoing the division in GetResults, but w/e) to operate on integer
    # values. We update tick labels later.
    yMin = 29.5 * accessesPerDataPoint
    yMax = outlierLimit * accessesPerDataPoint

    # Distance between consecutive contour points.
    xIncrement = int(len(results[victimThread]) / xSlices)
    yIncrement = int((yMax - yMin) / ySlices)

    # First graph the results with no victim threads. z represents a contour of
    # z values over a 2-d space.
    # Not sure why the x- and y-axis results need to be swapped for the contour,
    # but I'm too lazy to figure out why.
    z = np.zeros((ySlices + 1, xSlices + 1))

    for i in range(len(results[0])):
        xSlot = int(i / xIncrement)
        yVal = results[0][i] * accessesPerDataPoint
        ySlot = int((yVal - yMin) / yIncrement)
        z[ySlot][xSlot] += 1

    # Log2 of access count per contour location to more easily visualize peaks.
    z = np.log(z, out=np.zeros_like(z), where=(z!=0))

    # Ignore contour locations with no accesses.
    z = np.ma.array(z, mask=(z<1))

    ax.contourf(z, levels=contourLevels, cmap=plt.cm.Blues)

    # Then graph the results showing the port attack.
    z = np.zeros((ySlices + 1, xSlices + 1))

    for i in range(len(results[victimThread])):
        xSlot = int(i / xIncrement)
        yVal = results[victimThread][i] * accessesPerDataPoint
        ySlot = int((yVal - yMin) / yIncrement)
        z[ySlot][xSlot] += 1

    # Log2 of access count per contour location to more easily visualize peaks.
    z = np.log(z, out=np.zeros_like(z), where=(z!=0))

    # Ignore contour locations with no accesses.
    z = np.ma.array(z, mask = (z < 1))

    ax.contourf(z, levels=contourLevels, cmap=plt.cm.Oranges)

    ax.set_ylabel("Attacker LLC Access Time\n(CPU Cycles)",
                  fontsize=labelFontSize)
    ax.set_xlabel("Access Count", fontsize=labelFontSize)
    ax.tick_params(axis="both", labelsize=labelFontSize)

    # Update the tick labels back to per-access CPU cycles by dividing by
    # accessesPerDataPoint and by offsetting according to yMin and yMax.
    multiplier = int((yMax - yMin) / ySlices)
    ax.yaxis.set_major_formatter(
        ticker.FuncFormatter(lambda y,
                             pos: y * multiplier / accessesPerDataPoint + \
                             yMin / accessesPerDataPoint))

    # Ticks and gridlines
    ax.grid(b=True, which="major", axis="y", linestyle="-", color="gray")
    ax.minorticks_on()
    ax.grid(b=True, which="minor", axis="y", linestyle=":", color="gray")

    # Replace x-tick labels. We want a major tick every 100 (million) accesses.
    M = 1000000 # Present x-ticks as millions
    majorTickValueLimit = int(len(results[0]) * accessesPerDataPoint / M)
    xtickValues = [x for x in range(0, majorTickValueLimit, 100)]
    xtickPositions = [x / majorTickValueLimit * xSlices for x in xtickValues]
    labels = [str(x) + "M" if x != 0 else "0" for x in xtickValues]
    ax.set_xticks(xtickPositions)
    ax.set_xticklabels(labels)
    ax.xaxis.set_minor_locator(ticker.AutoMinorLocator(5))

    # We want major y-ticks every 0.5 cycles (and minor ticks  every 0.1 cycles)
    # according to the printed ticks, but we need to scale according to the
    # values provided to matplotlib.
    tickFrequency = ySlices / (((yMax - yMin) / accessesPerDataPoint) / 0.5)
    ax.yaxis.set_major_locator(ticker.MultipleLocator(tickFrequency))
    ax.yaxis.set_minor_locator(ticker.MultipleLocator(tickFrequency / 5))

    def AddCircle(x, y, width, height, color):
        circle = Ellipse(xy=(x,y), width=width, height=height, color=color,
                         fill=False, linewidth=2)
        ax.add_artist(circle)

    # Draw a circle around the results indicating a successful attack.
    AddCircle(x=445,
              y=(3230 - yMin) / (yMax - yMin) * ySlices,
              width=20,
              height=9.5,
              color="green")

    # # Add text next to the circle.
    ax.text(x=215,
            y=(3255 - yMin) / (yMax - yMin) * ySlices,
            s="Attacker detects victim\naccessing target bank!",
            fontsize=textFontSize,
            color="green",
            fontweight="bold")

    # # Add a checkmark next to circle to really emphasize it.
    ax.scatter(x=475,
               y=(3260 - yMin) / (yMax - yMin) * ySlices,
               marker="$\checkmark$",
               color="green",
               s=700)

    # # Draw a circle around all the other banks.
    AddCircle(x=227,
              y=(3170 - yMin) / (yMax - yMin) * ySlices,
              width=409,
              height=6,
              color="red")

    # # Add text indicating the banks not being attacked.
    ax.text(x=91,
            y=(3105 - yMin) / (yMax - yMin) * ySlices,
            s="Victim accesses each other bank",
            fontsize=textFontSize,
            color="red",
            fontweight="bold")

    # Custom legend. Plot 2 points outside the graph to get the legend handles.
    ax.plot([500, -1], label="W/O Victim", ls="none", marker=".",
            alpha=alphaValue)
    ax.plot([500, -1], label="With Victim", ls="none", marker=".",
            alpha=alphaValue)
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

    # Create graph.
    GraphResults(results)
