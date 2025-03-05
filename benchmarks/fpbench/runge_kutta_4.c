#include <stdio.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double h, double y_n_42_, double c) {
	double sixieme = 1.0f / 6.0f;
	double eps = 0.005f;
	double k = 1.2f;
	double y_n = y_n_42_;
	double i = 0.0f;
	double e = 1.0f;
	int tmp = e > eps;
	while (tmp) {
		double v = c - y_n;
		double k1 = (k * v) * v;
		double v_1 = c - (y_n + ((0.5f * h) * k1));
		double k2 = (k * v_1) * v_1;
		double v_2 = c - (y_n + ((0.5f * h) * k2));
		double k3 = (k * v_2) * v_2;
		double v_3 = c - (y_n + (h * k3));
		double k4 = (k * v_3) * v_3;
		double y_n_4 = y_n + ((sixieme * h) * (((k1 + (2.0f * k2)) + (2.0f * k3)) + k4));
		double i_5 = i + 1.0f;
		double e_6 = e - eps;
		y_n = y_n_4;
		i = i_5;
		e = e_6;
		tmp = e > eps;
	}
	return fabs(e);
}

int main() {

    // 设置初始值
    double h = 0.1; // 步长
    double y_n_42_ = 1.3; // 初始值 y_n
    double c = 2.5; // 常数 c

	int count = 1;
	double result = 0;

	while (count--)
	{
		/* code */
		result = ex0(h, y_n_42_, c);
	}

    // 输出结果
    printf("%.13E", result);

    return 0;
}

