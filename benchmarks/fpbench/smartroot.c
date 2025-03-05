#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double c) {
	double a = 3.0;
	double b = 6.0;
	double discr = (b * b) - ((a * c) * 4.0);
	double tmp_1 = 0;
	if (((b * b) - (a * c)) > 10.0) {
		double tmp_2;
		if (b > 0.0) {
			tmp_2 = (c * 2.0) / (-b - sqrt(discr));
		} else if (b < 0.0) {
			tmp_2 = (-b + sqrt(discr)) / (a * 2.0);
		} else {
			tmp_2 = (-b + sqrt(discr)) / (a * 2.0);
		}
		tmp_1 = tmp_2;
	} else {
		tmp_1 = (-b + sqrt(discr)) / (a * 2.0);
	}
	return tmp_1;
}

int main() {

    double c1 = 2.0;  // 示例1

	int count = 1000;
	double result = 0;
	while (count--)
	{
		/* code */
		result = ex0(c1);
	}

    // 输出结果
    printf("%.13f", result);
    
    return 0;
}

