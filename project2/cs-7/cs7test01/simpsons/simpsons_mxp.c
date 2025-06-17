#include <math.h>
#include <stdio.h>

#define ITERS 1

long double fun(long double x)
{
  float pi = acos(-1.0);
  double result;
  result = sinf(pi * x);
  return result;
}

int main(int argc, char **argv)
{
  int l;
  int i, j, k;
  const int n = 1000000;
  float a;
  float b;
  float h;
  double s1;
  double x;
  double fuzz = 1e-26;

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
