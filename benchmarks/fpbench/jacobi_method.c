#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double a11, double a22, double a33, double a44, double b1, double b2, double b3, double b4) {
	double eps = 1e-17f;
	double x_n1 = 0.0f;
	double x_n2 = 0.0f;
	double x_n3 = 0.0f;
	double x_n4 = 0.0f;
	double i = 0.0f;
	double e = 1.0f;
	double x1 = 0.0f;
	double x2 = 0.0f;
	double x3 = 0.0f;
	double x4 = 0.0f;
	int tmp = e > eps;
	while (tmp) {
		x_n1 = (((b1 / a11) - ((0.1f / a11) * x2)) - ((0.2f / a11) * x3)) + ((0.3f / a11) * x4);
		x_n2 = (((b2 / a22) - ((0.3f / a22) * x1)) + ((0.1f / a22) * x3)) - ((0.2f / a22) * x4);
		x_n3 = (((b3 / a33) - ((0.2f / a33) * x1)) + ((0.3f / a33) * x2)) - ((0.1f / a33) * x4);
		x_n4 = (((b4 / a44) + ((0.1f / a44) * x1)) - ((0.2f / a44) * x2)) - ((0.3f / a44) * x3);
		i = i + 1.0f;
		e = fabs((x_n4 - x4));
		x1 = x_n1;
		x2 = x_n2;
		x3 = x_n3;
		x4 = x_n4;
		tmp = e > eps;
	}
	return x2;
}

int main() {

    // 定义矩阵的对角元素
    double a11 = 10.0, a22 = 20.0, a33 = 30.0, a44 = 40.0;
    
    // 定义常数向量 b
    double b1 = 1.0, b2 = 2.0, b3 = 3.0, b4 = 4.0;

    int count = 10000000;
    double result = 0;

    while (count--)
    {
        /* code */
        result = ex0(a11, a22, a33, a44, b1, b2, b3, b4);
    }
    
    
    // 输出结果
    printf("%.13f", result);

    return 0;
}