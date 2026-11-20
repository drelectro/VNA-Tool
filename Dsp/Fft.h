#pragma once

#include <complex>
#include <vector>

namespace Fft {

// In-place radix-2 FFT. `data.size()` must be a power of two.
// The inverse includes the 1/N factor.
void transform(std::vector<std::complex<double>>& data, bool inverse);

// Smallest power of two >= n.
size_t nextPow2(size_t n);

} // namespace Fft
