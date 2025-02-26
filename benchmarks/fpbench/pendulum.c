#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double t0, double w0, double N) {
	double h = 0.01;
	double L = 2.0;
	double m = 1.5;
	double g = 9.80665;
	double t = t0;
	double w = w0;
	double n = 0.0;
	int tmp = n < N;
	while (tmp) {
		double k1w = (-g / L) * sin(t);
		double k2t = w + ((h / 2.0) * k1w);
		double t_1 = t + (h * k2t);
		double k2w = (-g / L) * sin((t + ((h / 2.0) * w)));
		double w_2 = w + (h * k2w);
		double n_3 = n + 1.0;
		t = t_1;
		w = w_2;
		n = n_3;
		tmp = n < N;
	}
	return t;
}

int main() {
	
    // 初始化参数
    double t0 = 0.0;  // 初始时间
    double w0 = 1.0;  // 初始角速度
    double N = 1000;  // 积分步数

	int count = 20000;
	double result = 0;
	while (count--)
	{
		/* code */
		result = ex0(t0, w0, N);
	}

    // 输出结果
    printf("%.13e", result);

    return 0;
}

