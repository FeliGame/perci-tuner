#include <math.h>
#include <stdio.h>

double ex0(double n)
{
	double dppi = acos(-1.0);
	double h = dppi / n;
	double s1 = 0.0;
	double t1 = 0.0;
	double i = 1.0;
	int tmp = i <= n;
	while (tmp)
	{
		double x = i * h;
		float tmp_1 = 2.0f;
		float d0 = tmp_1;
		double t0 = x;
		double k = 1.0;
		int tmp_2 = k <= 5.0;
		while (tmp_2)
		{
			float tmp_3 = 2.0f * d0;
			float d0_4 = tmp_3;
			double t0_5 = t0 + (sin((((double)d0) * x)) / ((double)d0));
			double k_6 = k + 1.0;
			d0 = d0_4;
			t0 = t0_5;
			k = k_6;
			tmp_2 = k <= 5.0;
		}
		double t2 = t0;
		double s0 = sqrt(((h * h) + ((t2 - t1) * (t2 - t1))));
		double tmp_7 = ((double)s1) + ((double)s0);
		double s1_8 = tmp_7;
		double x_9 = i * h;
		float tmp_10 = 2.0f;
		float d0_11 = tmp_10;
		double t0_12 = x_9;
		double k_13 = 1.0;
		int tmp_14 = k_13 <= 5.0;
		while (tmp_14)
		{
			float tmp_15 = 2.0f * d0_11;
			float d0_16 = tmp_15;
			double t0_17 = t0_12 + (sin((((double)d0_11) * x_9)) / ((double)d0_11));
			double k_18 = k_13 + 1.0;
			d0_11 = d0_16;
			t0_12 = t0_17;
			k_13 = k_18;
			tmp_14 = k_13 <= 5.0;
		}
		double t2_19 = t0_12;
		double t1_20 = t2_19;
		double i_21 = i + 1.0;
		s1 = s1_8;
		t1 = t1_20;
		i = i_21;
		tmp = i <= n;
	}
	return s1;
}

int main()
{
	int n = 3000000; // 迭代次数 n
	double result = ex0(n);
	printf("%.13e\n", result);
	return 0;
}
