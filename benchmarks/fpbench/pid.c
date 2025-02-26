#include <stdio.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double m, double kp, double ki, double kd, double c) {
	double dt = 0.5;
	double invdt = 1.0 / dt;
	double e = 0.0;
	double p = 0.0;
	double i = 0.0;
	double d = 0.0;
	double r = 0.0;
	double m_1 = m;
	double eold = 0.0;
	double t = 0.0;
	double tmp = t < 100.0;
	while (tmp) {
		e = c - m_1;
		p = kp * e;
		i = i + ((ki * dt) * e);
		d = (kd * invdt) * (e - eold);
		r = (p + i) + d;
		m_1 = m_1 + (0.01 * r);
		eold = e;
		t = t + dt;
		tmp = t < 100.0;
	}
	return m_1;
}

int main() {

    // 设置初始值和增益
    double m = 0.0;  // 初始输出
    double kp = 1.0; // 比例增益
    double ki = 0.1; // 积分增益
    double kd = 0.5; // 微分增益
    double c = 10.0; // 目标值（期望值）

	int count = 500000;
	double result = 0;

    // 调用 ex0 函数进行计算
	while (count--)
	{
		/* code */
		result = ex0(m, kp, ki, kd, c);
	}

    // 输出结果
    printf("%.13f", result);

    return 0;
}