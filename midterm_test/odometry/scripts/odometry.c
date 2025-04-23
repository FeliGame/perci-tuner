#include <math.h>
#include <stdio.h>

double ex0(double sr_42_, double sl_42_)
{
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
	double sl = sl_42_;
	double sr = sr_42_;
	double j = 0.0f;

	while (t < 1000.0f)
	{
		delta_dl = c * sl;
		delta_dr = c * sr;
		delta_d = 0.5f * (delta_dl + delta_dr);
		delta_theta = (delta_dr - delta_dl) * inv_l;

		arg = theta + 0.5f * delta_theta;
		cosi = 1.0f - 0.5f * arg * arg + (arg * arg * arg * arg) * 0.0416666666f;
		x += delta_d * cosi;

		sini = arg - (arg * arg * arg) * 0.1666666666f + (arg * arg * arg * arg * arg) * 0.008333333f;
		y += delta_d * sini;

		theta += delta_theta;
		t += 1.0f;

		sl = (j == 50.0f) ? sr : sl;
		sr = (j == 50.0f) ? sl : sr;
		j = (j == 50.0f) ? 0.0f : j + 1.0f;
	}

	return x;
}

int main()
{
	double sr_42 = 1.0;
	double sl_42 = 0.8;
	double result = ex0(sr_42, sl_42);
	printf("%.13e\n", result);
	return 0;
}
