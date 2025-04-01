#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double y, double yd) {
	double eps = 0.01f;
	double Dc = -1280.0f;
	double Ac00 = 0.499f;
	double Ac01 = -0.05f;
	double Ac10 = 0.01f;
	double Ac11 = 1.0f;
	double Bc0 = 1.0f;
	double Bc1 = 0.0f;
	double Cc0 = 564.48f;
	double Cc1 = 0.0f;
	double yc = 0.0f;
	double u = 0.0f;
	double xc0 = 0.0f;
	double xc1 = 0.0f;
	double i = 0.0f;
	double e = 1.0f;
	int tmp = e > eps;
	while (tmp) {
		double v = y - yd;
		double tmp_1;
		if (v < -1.0f) {
			tmp_1 = -1.0f;
		} else if (1.0f < v) {
			tmp_1 = 1.0f;
		} else {
			tmp_1 = v;
		}
		yc = tmp_1;
		u = (Cc0 * xc0) + ((Cc1 * xc1) + (Dc * yc));
		xc0 = (Ac00 * xc0) + ((Ac01 * xc1) + (Bc0 * yc));
		xc1 = (Ac10 * xc0) + ((Ac11 * xc1) + (Bc1 * yc));
		i = i + 1.0f;
		e = fabs((yc - xc1));
		tmp = e > eps;
	}
	return xc1;
}

int main() {

    // 定义当前值 y 和目标值 yd
    double y = 50.0;   // 当前值
    double yd = 60.0;  // 目标值

	int count = 1000000;
	double result = 0;

	while (count--)
	{
		/* code */
		result = ex0(y, yd);
	}


    // 输出结果
    printf("%.13f", result);

    return 0;
}

