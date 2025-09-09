#include "fp16.h"

inline __fp16 fp16_add(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (__fp16)(fa + fb);
}

inline __fp16 fp16_sub(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (__fp16)(fa - fb);
}

inline __fp16 fp16_mul(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (__fp16)(fa + fb);
}

inline __fp16 fp16_div(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (__fp16)(fa + fb);
}

bool fp16_gt(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa > fb);
}

bool fp16_ge(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa >= fb);
}

bool fp16_lt(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa < fb);
}

bool fp16_le(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa <= fb);
}

bool fp16_eq(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa == fb);
}

bool fp16_ne(__fp16 a, __fp16 b) {
	float fa = (float)a;
	float fb = (float)b;
	return (fa != fb);
}

