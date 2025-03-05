#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double Mf, double A)
{
	double R = 6400000.0f;
	double G = 6.67428e-11f;
	double Mt = 5.9736e+24f;
	double dt = 0.1f;
	double T = 24.0f * 3600.0f;
	double nombrepas = T / dt;
	double r0 = (400.0f * 10000.0f) + R;
	double vr0 = 0.0f;
	double teta0 = 0.0f;
	double viss = sqrt(((G * Mt) / r0));
	double vteta0 = viss / r0;
	double rf = R;
	double vrf = 0.0f;
	double tetaf = 0.0f;
	double vl = sqrt(((G * Mt) / R));
	double vlrad = vl / r0;
	double vtetaf = 1.1f * vlrad;
	double t_i = 0.0f;
	double mf_i = 0.0f;
	double u1_i = 0.0f;
	double u3_i = 0.0f;
	double w1_i = 0.0f;
	double w3_i = 0.0f;
	double u2_i = 0.0f;
	double u4_i = 0.0f;
	double w2_i = 0.0f;
	double w4_i = 0.0f;
	double x = 0.0f;
	double y = 0.0f;
	double i = 1.0f;
	double u1_im1 = r0;
	double u2_im1 = vr0;
	double u3_im1 = teta0;
	double u4_im1 = vteta0;
	double w1_im1 = rf;
	double w2_im1 = vrf;
	double w3_im1 = tetaf;
	double w4_im1 = vtetaf;
	double t_im1 = 0.0f;
	double mf_im1 = Mf;
	int tmp = i < 2000000.0f;
	while (tmp)
	{
		t_i = t_im1 + dt;
		mf_i = mf_im1 - (A * t_im1);
		u1_i = (u2_im1 * dt) + u1_im1;
		u3_i = (u4_im1 * dt) + u3_im1;
		w1_i = (w2_im1 * dt) + w1_im1;
		w3_i = (w4_im1 * dt) + w3_im1;
		u2_i = ((-G * (Mt / (u1_im1 * u1_im1))) * dt) + ((u1_im1 * u4_im1) * (u4_im1 * dt));
		u4_i = ((-2.0f * (u2_im1 * (u4_im1 / u1_im1))) * dt) + u4_im1;
		double tmp_1;
		if (mf_im1 > 0.0f)
		{
			tmp_1 = ((A * w2_im1) / (Mf - (A * t_im1))) * dt;
		}
		else
		{
			tmp_1 = 0.0f;
		}
		w2_i = (((-G * (Mt / (w1_im1 * w1_im1))) * dt) + ((w1_im1 * w4_im1) * (w4_im1 * dt))) + (tmp_1 + w2_im1);
		double tmp_2;
		if (mf_im1 > 0.0f)
		{
			tmp_2 = A * ((w4_im1 / (Mf - (A * t_im1))) * dt);
		}
		else
		{
			tmp_2 = 0.0f;
		}
		w4_i = ((-2.0f * (w2_im1 * (w4_im1 / w1_im1))) * dt) + (tmp_2 + w4_im1);
		x = u1_i * cos(u3_i);
		y = u1_i * sin(u3_i);
		i = i + 1.0f;
		u1_im1 = u1_i;
		u2_im1 = u2_i;
		u3_im1 = u3_i;
		u4_im1 = u4_i;
		w1_im1 = w1_i;
		w2_im1 = w2_i;
		w3_im1 = w3_i;
		w4_im1 = w4_i;
		t_im1 = t_i;
		mf_im1 = mf_i;
		tmp = i < 2000000.0f;
	}
	return x;
}

int main()
{

	double Mf = 5000; // 初始质量 (单位千克)
	double A = 10;		// 每单位时间的质量损失（单位千克/秒）

	int count = 1;
	double result = 0;
	while (count--)
	{
		/* code */
		result = ex0(Mf, A);
	}

	// 输出结果
	printf("%.13e", result);

	return 0;
}
