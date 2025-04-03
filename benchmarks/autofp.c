#include <stdio.h>

__autofp ga = 1;
static __autofp gb;
__autofp gc[3];
static __autofp gd[3];

int main() {
  __autofp a, b;
  static __autofp c = 0;
  __autofp d[3];
  static __autofp e[4];
  int x = gb + gd[1];
  return x;
}
