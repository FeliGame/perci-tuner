#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double u) {
	double a = 0.25;
	double b = 5000.0;
	double n = 25.0;
	double h = (b - a) / n;
	double xb = 0.0;
	double r = 0.0;
	double xa = 0.25;
	int tmp = xa < 5000.0;
	while (tmp) {
		double v = xa + h;
		double tmp_1;
		if (v > 5000.0) {
			tmp_1 = 5000.0;
		} else {
			tmp_1 = v;
		}
		xb = tmp_1;
		double gxa = u / ((((((0.7 * xa) * xa) * xa) - ((0.6 * xa) * xa)) + (0.9 * xa)) - 0.2);
		double gxb = u / ((((((0.7 * xb) * xb) * xb) - ((0.6 * xb) * xb)) + (0.9 * xb)) - 0.2);
		r = r + (((gxa + gxb) * 0.5) * h);
		xa = xa + h;
		tmp = xa < 5000.0;
	}
	return r;
}

int main() {

    double u = 1.0; // 设置输入参数 u
	int count = 1000000;
	double result = 0;
	while (count--)
	{
		/* code */
		result = ex0(u);
	}

    // 输出结果
    printf("%.13f", result);

    return 0;
}

