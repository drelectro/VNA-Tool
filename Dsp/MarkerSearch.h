#pragma once

#include <vector>

// Searches on a formatted trace (x ascending, one y per x).

namespace MarkerSearch {

double interpolate(const std::vector<double>& x, const std::vector<double>& y, double xq);
int    nearestIndex(const std::vector<double>& x, double xq);
int    maxIndex(const std::vector<double>& y);
int    minIndex(const std::vector<double>& y);

// Indices of local maxima (or minima, with `valleys`) that stand at least
// `prominence` above (below) the higher of the two saddles around them.
std::vector<int> findPeaks(const std::vector<double>& y, double prominence, bool valleys = false);

// The nearest peak strictly left (-1) or right (+1) of `fromIndex`, or -1.
int nextPeak(const std::vector<double>& y, int fromIndex, int direction, double prominence,
             bool valleys = false);

// Every x at which the trace crosses `level`, linearly interpolated.
std::vector<double> crossings(const std::vector<double>& x, const std::vector<double>& y, double level);

struct Bandwidth
{
    bool   valid = false;
    double refX = 0, refY = 0;      // the reference point (peak, or the marker)
    double low = 0, high = 0;       // edges
    double center = 0;              // midpoint of the edges
    double bandwidth = 0;
    double q = 0;                   // center / bandwidth
    double loss = 0;                // refY
};

// Edges are the first crossings of (refY + level) either side of refIndex.
// For a band-pass response `level` is negative (e.g. -3). With `notch` the
// reference is a minimum and the edges are where the trace rises to
// refY + |level|.
Bandwidth bandwidth(const std::vector<double>& x, const std::vector<double>& y,
                    int refIndex, double level, bool notch);

} // namespace MarkerSearch
