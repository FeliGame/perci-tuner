#include <stdio.h>
#include <stdlib.h>
#include "arm_bf16.h"

void main() {
	__bf16 a = 1;
	a += 1;

	printf("%f\n", a);
}
