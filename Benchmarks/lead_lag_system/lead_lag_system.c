#include <math.h>
#include <stdio.h>

double ex0(double y, double yd)
{
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
	double xc0 = 0.0f;
	double xc1 = 0.0f;
	double e = 1.0f;
	int tmp = e > eps;
	while (tmp)
	{
		double v = y - yd;
		double tmp_1;
		if (v < -1.0f)
		{
			tmp_1 = -1.0f;
		}
		else if (1.0f < v)
		{
			tmp_1 = 1.0f;
		}
		else
		{
			tmp_1 = v;
		}
		yc = tmp_1;
		xc0 = (Ac00 * xc0) + ((Ac01 * xc1) + (Bc0 * yc));
		xc1 = (Ac10 * xc0) + ((Ac11 * xc1) + (Bc1 * yc));
		e = fabs((yc - xc1));
		tmp = e > eps;
	}
	return xc1;
}

int main()
{
	double y = 50.0;
	double yd = 60.0;
	double result = 0;
	int count = 500000;
	while (count--)
	{
		result = ex0(y, yd);
	}
	printf("%.13e\n", result);
	return 0;
}