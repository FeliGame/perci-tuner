#include <time.h>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

long double pi;

long double fun(long double xarg)
{
  long double result;
  result = sinl(pi * xarg);
  return result;
}

int main(int argc, char **argv)
{

  const int n = 1000000;
  long double a;
  long double b;
  long double h;
  long double s1;
  long double x;

  a = 0.0;
  b = 1.0;
  pi = acosl(-1.0);
  h = (b - a) / (2.0 * n);
  s1 = 0.0;
  x = a;
  s1 = (fun(a) + fun(b));

  for (int l = 0; l < n; l++)
  {
    x += h;
    s1 = s1 + 4.0 * fun(x);
    x = x + h;
    s1 = s1 + 2.0 * fun(x);
  }
  s1 = s1 * h * pi / 3.0;

  printf("%.13e\n", s1);
  return 0;
}
