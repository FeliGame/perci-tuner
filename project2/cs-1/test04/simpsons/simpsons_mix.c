#include <math.h>
#include <stdio.h>

#define ITERS 1

double fun(double x)
{
  double pi = acos(-1.0);
  double result;
  result = sin(pi * x);
  return result;
}

int main(int argc, char **argv)
{
  int l;
  int i, j, k;
  const int n = 1000000;
  float a, b;
  double h, x;
  long double s1;
  const long double fuzz = 1e-26;

  for (l = 0; l < ITERS; l++)
  {
    a = 0.0;
    b = 1.0;
    h = (b - a) / (2.0 * n);
    s1 = 0.0;
    x = a;
    s1 = fun(x);

  L100:
    x = x + h;
    s1 = s1 + 4.0 * fun(x);
    x = x + h;
    if (x + fuzz >= b)
      goto L110;
    s1 = s1 + 2.0 * fun(x);
    goto L100;

  L110:
    s1 = s1 + fun(x);
  }

  printf("%.16Le\n", (long double)h * s1 / 3.0);

  return 0;
}
