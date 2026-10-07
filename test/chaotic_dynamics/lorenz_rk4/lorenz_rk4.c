// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* lorenz_rk4.c -- Lorenz system, classical RK4
 * Family : ODE/chaos
 * Hazards: higher-order method, but chaos still amplifies
 *
 * Tunable variables (candidates for float<->double): double x, y, z, k1*, k2*, k3*, k4*
 *
 * Build:  make lorenz_rk4
 * Run:    ./lorenz_rk4
 * Oracle: oracles/lorenz_rk4.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static const double SIG = 10.0, RHO = 28.0, BETA = 8.0/3.0;

static void rhs(double x, double y, double z,
                double *dx, double *dy, double *dz) {
    *dx = SIG*(y-x); *dy = x*(RHO-z) - y; *dz = x*y - BETA*z;
}

double kernel(int N, double dt) {
    double x=1.0, y=1.0, z=1.0;
    for (int i = 0; i < N; i++) {
        double a1,b1,c1,a2,b2,c2,a3,b3,c3,a4,b4,c4;
        rhs(x, y, z, &a1, &b1, &c1);
        rhs(x+0.5*dt*a1, y+0.5*dt*b1, z+0.5*dt*c1, &a2,&b2,&c2);
        rhs(x+0.5*dt*a2, y+0.5*dt*b2, z+0.5*dt*c2, &a3,&b3,&c3);
        rhs(x+    dt*a3, y+    dt*b3, z+    dt*c3, &a4,&b4,&c4);
        x += dt*(a1+2.0*a2+2.0*a3+a4)/6.0;
        y += dt*(b1+2.0*b2+2.0*b3+b4)/6.0;
        z += dt*(c1+2.0*c2+2.0*c3+c4)/6.0;
    }
    return x + y + z;
}

int main(void) {
    printf("%.17g\n", kernel(2000, 0.001));
    return 0;
}
