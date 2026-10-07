// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* predprey_rk4.c -- Lotka-Volterra predator-prey via RK4
 * Family : ODE
 * Hazards: periodic orbit; energy invariant slowly drifts
 *
 * Tunable variables (candidates for float<->double): double x, y
 *
 * Build:  make predprey_rk4
 * Run:    ./predprey_rk4
 * Oracle: oracles/predprey_rk4.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static const double A=1.5, B=1.0, C=1.0, D=3.0;
static void rhs(double x, double y, double *dx, double *dy) {
    *dx = A*x - B*x*y;
    *dy = C*x*y - D*y;
}

double kernel(int N, double dt) {
    double x=10.0, y=5.0;
    for (int i = 0; i < N; i++) {
        double a1,b1,a2,b2,a3,b3,a4,b4;
        rhs(x, y, &a1, &b1);
        rhs(x+0.5*dt*a1, y+0.5*dt*b1, &a2,&b2);
        rhs(x+0.5*dt*a2, y+0.5*dt*b2, &a3,&b3);
        rhs(x+    dt*a3, y+    dt*b3, &a4,&b4);
        x += dt*(a1+2.0*a2+2.0*a3+a4)/6.0;
        y += dt*(b1+2.0*b2+2.0*b3+b4)/6.0;
    }
    return x + y;
}

int main(void) {
    printf("%.17g\n", kernel(20000, 0.001));
    return 0;
}
