#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define TRUE 1
#define FALSE 0

double ex0(double sr_42_, double sl_42_) {
	double inv_l = 0.1f;
	double c = 12.34f;
	double delta_dl = 0.0f;
	double delta_dr = 0.0f;
	double delta_d = 0.0f;
	double delta_theta = 0.0f;
	double arg = 0.0f;
	double cosi = 0.0f;
	double x = 0.0f;
	double sini = 0.0f;
	double y = 0.0f;
	double theta = -0.985f;
	double t = 0.0f;
	double tmp = sl_42_;
	double sl = sl_42_;
	double sr = sr_42_;
	double j = 0.0f;
	int tmp_1 = t < 1000.0f;
	while (tmp_1) {
		delta_dl = c * sl;
		delta_dr = c * sr;
		delta_d = (delta_dl + delta_dr) * 0.5f;
		delta_theta = (delta_dr - delta_dl) * inv_l;
		arg = theta + (delta_theta * 0.5f);
		cosi = (1.0f - ((arg * arg) * 0.5f)) + ((((arg * arg) * arg) * arg) * 0.0416666666f);
		x = x + (delta_d * cosi);
		sini = (arg - (((arg * arg) * arg) * 0.1666666666f)) + (((((arg * arg) * arg) * arg) * arg) * 0.008333333f);
		y = y + (delta_d * sini);
		theta = theta + delta_theta;
		t = t + 1.0f;
		tmp = sl;
		double tmp_2;
		if (j == 50.0f) {
			tmp_2 = sr;
		} else {
			tmp_2 = sl;
		}
		sl = tmp_2;
		double tmp_3;
		if (j == 50.0f) {
			tmp_3 = tmp;
		} else {
			tmp_3 = sr;
		}
		sr = tmp_3;
		double tmp_4;
		if (j == 50.0f) {
			tmp_4 = 0.0f;
		} else {
			tmp_4 = j + 1.0f;
		}
		j = tmp_4;
		tmp_1 = t < 1000.0f;
	}
	return x;
}

int main() {
    // 设置初始的左右轮速度（例如，单位为 m/s）
    double sr_42 = 1.0;  // 右侧速度
    double sl_42 = 0.8;  // 左侧速度

    // 调用 ex0 函数并获取结果
    double result = ex0(sr_42, sl_42);

    // 输出结果
    printf("%.13f", result);

    return 0;
}

