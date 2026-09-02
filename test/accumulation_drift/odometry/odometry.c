// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

#include <math.h>
#include <stdio.h>

double ex0(double sr_42_, double sl_42_)
{
	double inv_l = 0.1;
	double c = 12.34;
	double delta_dl = 0.0;
	double delta_dr = 0.0;
	double delta_d = 0.0;
	double delta_theta = 0.0;
	double arg = 0.0;
	double cosi = 0.0;
	double x = 0.0;
	double sini = 0.0;
	double y = 0.0;
	double theta = -0.985;
	double t = 0.0;
	double sl = sl_42_;
	double sr = sr_42_;
	double j = 0.0;

	while (t < 1000.0)
	{
		delta_dl = c * sl;
		delta_dr = c * sr;
		delta_d = 0.5 * (delta_dl + delta_dr);
		delta_theta = (delta_dr - delta_dl) * inv_l;

		arg = theta + 0.5 * delta_theta;
		cosi = 1.0 - 0.5 * arg * arg + (arg * arg * arg * arg) * 0.0416666666;
		x += delta_d * cosi;

		sini = arg - (arg * arg * arg) * 0.1666666666 + (arg * arg * arg * arg * arg) * 0.008333333;
		y += delta_d * sini;

		theta += delta_theta;
		t += 1.0;

		sl = (j == 50.0) ? sr : sl;
		sr = (j == 50.0) ? sl : sr;
		j = (j == 50.0) ? 0.0 : j + 1.0;
	}

	return x;
}

int main()
{
	double sr_42 = 1.0;
	double sl_42 = 0.8;
	double result = 0;
	int count = 10000;
	while (count--)
	{
		result = ex0(sr_42, sl_42);
	}
	printf("%.13e\n", result);
	return 0;
}
