#include "gl_resolution_curate.h"

#include <stddef.h> // NULL

int gl_curate_resolutions(const int *raw_w, const int *raw_h, int rawCount,
                          int *out_w, int *out_h, int maxOut)
{
    int count = 0;
    int i, j;

    if (raw_w == NULL || raw_h == NULL || out_w == NULL || out_h == NULL ||
        maxOut <= 0) {
        return 0;
    }

    for (i = 0; i < rawCount; i++) {
        int w = raw_w[i];
        int h = raw_h[i];
        int dup = 0;

        // 1. Drop modes below the 640x480 floor.
        if (w < 640 || h < 480) {
            continue;
        }

        // 2. Deduplicate by exact (w,h).
        for (j = 0; j < count; j++) {
            if (out_w[j] == w && out_h[j] == h) { dup = 1; break; }
        }
        if (dup) {
            continue;
        }

        // 3. Keep at most maxOut, preferring the largest. When full, replace
        //    the current smallest only if this candidate is larger.
        if (count < maxOut) {
            out_w[count] = w;
            out_h[count] = h;
            count++;
        } else {
            int min = 0;
            for (j = 1; j < count; j++) {
                if (out_w[j] < out_w[min] ||
                    (out_w[j] == out_w[min] && out_h[j] < out_h[min])) {
                    min = j;
                }
            }
            if (w > out_w[min] || (w == out_w[min] && h > out_h[min])) {
                out_w[min] = w;
                out_h[min] = h;
            }
        }
    }

    // 4. Sort ascending (width, then height) — insertion sort.
    for (i = 1; i < count; i++) {
        int w = out_w[i];
        int h = out_h[i];
        j = i - 1;
        while (j >= 0 && (out_w[j] > w || (out_w[j] == w && out_h[j] > h))) {
            out_w[j + 1] = out_w[j];
            out_h[j + 1] = out_h[j];
            j--;
        }
        out_w[j + 1] = w;
        out_h[j + 1] = h;
    }

    return count;
}
