#pragma once
#include <libraw/libraw.h>
#include <algorithm>

// OpenMP limits are local to the calling worker. Avoid saturating a many-core
// machine during adjacent-image preloading; honor a smaller OMP_NUM_THREADS.
inline void configureRawProcessingThreads()
{
#ifdef LIBRAW_USE_OPENMP
    omp_set_num_threads(std::min(8, omp_get_max_threads()));
#endif
}
