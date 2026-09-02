// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* mandelbrot.c -- Average escape iterations over a Mandelbrot grid
 * Family : geometry/iterative
 * Hazards: iteration count branches on |z|^2>4
 *
 * Tunable variables (candidates for float<->double): double zx, zy, cx, cy
 *
 * Build:  make mandelbrot
 * Run:    ./mandelbrot
 * Oracle: oracles/mandelbrot.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int W, int H, int maxiter) {
    long total = 0;
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            double cx = -2.0 + 3.0*(double)i/(double)(W-1);
            double cy = -1.5 + 3.0*(double)j/(double)(H-1);
            double zx = 0.0, zy = 0.0;
            int it;
            for (it = 0; it < maxiter; it++) {
                double zx2 = zx*zx - zy*zy + cx;
                zy = 2.0*zx*zy + cy;
                zx = zx2;
                if (zx*zx + zy*zy > 4.0) break;
            }
            total += it;
        }
    return (double)total / (double)(W*H);
}

int main(void) {
    printf("%.17g\n", kernel(80, 60, 200));
    return 0;
}
