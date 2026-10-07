// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* raytrace_sphere.c -- Average ray-sphere intersection distance over an image plane
 * Family : geometry
 * Hazards: discriminant cancellation when ray grazes sphere
 *
 * Tunable variables (candidates for float<->double): double rx, ry, rz, b, disc, t
 *
 * Build:  make raytrace_sphere
 * Run:    ./raytrace_sphere
 * Oracle: oracles/raytrace_sphere.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int W, int H) {
    double sum = 0.0; int count = 0;
    double cx = 0.0, cy = 0.0, cz = 5.0, R = 1.0;
    double C2 = cx*cx + cy*cy + cz*cz;
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            double rx = -1.0 + 2.0*(double)i/(double)(W-1);
            double ry = -1.0 + 2.0*(double)j/(double)(H-1);
            double rz = 1.0;
            double rl = sqrt(rx*rx + ry*ry + rz*rz);
            rx /= rl; ry /= rl; rz /= rl;
            double b = rx*cx + ry*cy + rz*cz;
            double disc = b*b - C2 + R*R;
            if (disc >= 0.0) {
                double t = b - sqrt(disc);
                if (t > 0) { sum += t; count++; }
            }
        }
    return sum / (double)(count > 0 ? count : 1);
}

int main(void) {
    printf("%.17g\n", kernel(100, 100));
    return 0;
}
