#include <time.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define INPUTS 10

long double fun( long double x )
{
  int k, n = 5;
  long double t1, d1 = 1.0L;

  t1 = x;

  for( k = 1; k <= n; k++ )
  {
    d1 = 2.0 * d1;
    t1 = t1 + sinl (d1 * x) / d1;
  }

  //if(x<t1) printf("%Lf\n",x);
  return t1;
}

int main()
{
  int l;
  long int diff = 0;

  int i, j, k; 
  int arr[] = {100,
              200,
              1000,
              2000,
              10000,
              20000,
              100000,
              200000,
              1000000,
              2000000
              };
  long double h, t1, t2, dppi;
  // , ans = 5.795776322412856L;
  long double s1; 

  // variables for logging/checking
  long double result;


  // dummy calls
  sqrtf(0);
  acosf(0);
  sinf(0);

  for (l = 0; l < INPUTS; l++)
  {
    //printf("r1=%d\n",l);
    int n = arr[l];
    t1 = -1.0;
    dppi = acosl(t1);
    s1 = 0.0;
    t1 = 0.0;
    h = dppi / n;

    for( i = 1; i <= n; i++ )
    {
      //printf("r2=%d\n", i);
      t2 = fun (i * h);
      s1 = s1 + sqrtl (h*h + (t2 - t1)*(t2 - t1));
      t1 = t2;
    	//printf("%Lf\n", s1);
    }

    result += s1;
  }


//  if (sizeof(typeof(result)) == sizeof(long double))
    printf("%.30Lf\n", result);
    //uint64_t *xx = (uint64_t *)(&result);
    //printf("%lx %lx\n", xx[0], xx[1]);
//  else if (sizeof(typeof(result)) == sizeof(double))
//    printf("%lf", result);
//  else if (sizeof(typeof(result)) == sizeof(float))
//    printf("%f", result);

  // print score (diff) to a file
  // FILE* file;
  // file = fopen("score.cov", "w");
  // fprintf(file, "%ld\n", diff);
  // fclose(file);

  return 0;

}


