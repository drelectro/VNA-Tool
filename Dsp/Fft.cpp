#include "Fft.h"

#include <cmath>

namespace Fft {

size_t nextPow2(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

void transform(std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    if (n < 2)
        return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * M_PI / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wLen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wLen;
            }
        }
    }

    if (inverse) {
        const double scale = 1.0 / double(n);
        for (auto& v : a)
            v *= scale;
    }
}

} // namespace Fft
