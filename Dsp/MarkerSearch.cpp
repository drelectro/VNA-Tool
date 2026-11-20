#include "MarkerSearch.h"

#include <algorithm>
#include <cmath>

namespace MarkerSearch {

double interpolate(const std::vector<double>& x, const std::vector<double>& y, double xq)
{
    if (x.empty() || x.size() != y.size())
        return 0.0;
    if (xq <= x.front())
        return y.front();
    if (xq >= x.back())
        return y.back();
    const auto it = std::upper_bound(x.begin(), x.end(), xq);
    const size_t hi = size_t(it - x.begin());
    const size_t lo = hi - 1;
    const double span = x[hi] - x[lo];
    const double t = span != 0.0 ? (xq - x[lo]) / span : 0.0;
    return y[lo] + (y[hi] - y[lo]) * t;
}

int nearestIndex(const std::vector<double>& x, double xq)
{
    if (x.empty())
        return -1;
    const auto it = std::lower_bound(x.begin(), x.end(), xq);
    if (it == x.begin())
        return 0;
    if (it == x.end())
        return int(x.size()) - 1;
    const int hi = int(it - x.begin());
    return (xq - x[size_t(hi) - 1]) <= (x[size_t(hi)] - xq) ? hi - 1 : hi;
}

int maxIndex(const std::vector<double>& y)
{
    return y.empty() ? -1 : int(std::max_element(y.begin(), y.end()) - y.begin());
}

int minIndex(const std::vector<double>& y)
{
    return y.empty() ? -1 : int(std::min_element(y.begin(), y.end()) - y.begin());
}

std::vector<int> findPeaks(const std::vector<double>& yIn, double prominence, bool valleys)
{
    std::vector<int> out;
    const int n = int(yIn.size());
    if (n < 3)
        return out;

    // Valleys are peaks of the negated trace.
    std::vector<double> y(yIn);
    if (valleys)
        for (auto& v : y)
            v = -v;

    for (int i = 1; i < n - 1; ++i) {
        if (!(y[size_t(i)] > y[size_t(i) - 1] && y[size_t(i)] >= y[size_t(i) + 1]))
            continue;
        // Walk outwards until something higher is met, tracking the lowest
        // point passed on each side.
        double leftMin = y[size_t(i)], rightMin = y[size_t(i)];
        for (int j = i - 1; j >= 0 && y[size_t(j)] <= y[size_t(i)]; --j)
            leftMin = std::min(leftMin, y[size_t(j)]);
        for (int j = i + 1; j < n && y[size_t(j)] <= y[size_t(i)]; ++j)
            rightMin = std::min(rightMin, y[size_t(j)]);
        if (y[size_t(i)] - std::max(leftMin, rightMin) >= prominence)
            out.push_back(i);
    }
    return out;
}

int nextPeak(const std::vector<double>& y, int fromIndex, int direction, double prominence, bool valleys)
{
    const std::vector<int> peaks = findPeaks(y, prominence, valleys);
    if (direction < 0) {
        for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
            if (*it < fromIndex)
                return *it;
    } else {
        for (const int p : peaks)
            if (p > fromIndex)
                return p;
    }
    return -1;
}

std::vector<double> crossings(const std::vector<double>& x, const std::vector<double>& y, double level)
{
    std::vector<double> out;
    for (size_t i = 1; i < y.size() && i < x.size(); ++i) {
        const double a = y[i - 1] - level;
        const double b = y[i] - level;
        if (a == 0.0)
            out.push_back(x[i - 1]);
        else if ((a < 0.0) != (b < 0.0) && b != 0.0)
            out.push_back(x[i - 1] + (x[i] - x[i - 1]) * (a / (a - b)));
    }
    if (!y.empty() && y.size() == x.size() && y.back() == level)
        out.push_back(x.back());
    return out;
}

Bandwidth bandwidth(const std::vector<double>& x, const std::vector<double>& y,
                    int refIndex, double level, bool notch)
{
    Bandwidth r;
    const int n = int(y.size());
    if (refIndex < 0 || refIndex >= n || x.size() != y.size())
        return r;

    r.refX = x[size_t(refIndex)];
    r.refY = y[size_t(refIndex)];
    r.loss = r.refY;
    const double target = notch ? r.refY + std::abs(level) : r.refY + level;

    // In a pass-band the trace falls THROUGH the target going outwards; in a
    // notch it rises through it. Either way: the first sign change.
    auto edge = [&](int step, double& out) {
        for (int i = refIndex; i + step >= 0 && i + step < n; i += step) {
            const double a = y[size_t(i)] - target;
            const double b = y[size_t(i + step)] - target;
            if ((a < 0.0) != (b < 0.0) || b == 0.0) {
                const double t = (a - b) != 0.0 ? a / (a - b) : 0.0;
                out = x[size_t(i)] + (x[size_t(i + step)] - x[size_t(i)]) * t;
                return true;
            }
        }
        return false;
    };

    if (!edge(-1, r.low) || !edge(+1, r.high))
        return r;

    r.bandwidth = r.high - r.low;
    r.center = 0.5 * (r.low + r.high);
    r.q = r.bandwidth > 0.0 ? r.center / r.bandwidth : 0.0;
    r.valid = r.bandwidth > 0.0;
    return r;
}

} // namespace MarkerSearch
