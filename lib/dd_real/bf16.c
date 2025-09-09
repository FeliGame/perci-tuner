#include <stdio.h>
#include <stdint.h>
#include "bf16.h"

typedef union {
	float f;
	__bf16 bf[2];	
	unsigned short us[2];
} bf16;

typedef union {
	__bf16 bf;
	unsigned short us;
} ubf16;

// BF16
inline __bf16 FP32ToBF16(float a) {
	bf16 t;
	t.f = a;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	//printf("in FP32ToBF16 : %x %x -> %x\n", t.us[0], t.us[1], t.us[0]);
	return t.bf[0];
#else
	//printf("in FP32ToBF16 : %x %x -> %x\n", t.us[0], t.us[1], t.us[1]);
	return t.bf[1];
#endif
}

inline float BF16ToFP32(__bf16 a) {
	bf16 t;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	//printf("in BF16ToFP32 : %x -> %x %x\n", t.us[0], t.us[0], t.us[1]);
	t.bf[0] = a;
	t.bf[1] = 0;
#else 
	//printf("in BF16ToFP32 : %x -> %x %x\n", t.us[1], t.us[0], t.us[1]);
	t.bf[0] = 0;
	t.bf[1] = a;
#endif
	return t.f;
}

inline __bf16 FP64ToBF16(double a) {
	float aa = (float)a;
	return FP32ToBF16(aa);
}

inline double BF16ToFP64(__bf16 a) {
	return (double)BF16ToFP32(a);
}

inline __bf16 FP128ToBF16(fp128 a) {
	float aa = (float)a;
	return FP32ToBF16(aa);
}

inline fp128 BF16ToFP128(__bf16 a) {
	return (fp128)BF16ToFP32(a);
}

inline __bf16 bf16_add(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);

	float fc = fa + fb;
	return FP32ToBF16(fc);
}

inline __bf16 bf16_sub(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);

	float fc = fa - fb;
	return FP32ToBF16(fc);
}

inline __bf16 bf16_mul(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);

	float fc = fa * fb;
	return FP32ToBF16(fc);
}

inline __bf16 bf16_div(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);

	float fc = fa / fb;
	return FP32ToBF16(fc);
}

inline bool bf16_eq(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa == fb);
}

inline bool bf16_le(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa <= fb);
}

inline bool bf16_ge(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa >= fb);
}

inline bool bf16_lt(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa < fb);
}

inline bool bf16_gt(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa > fb);
}

inline bool bf16_ne(__bf16 a, __bf16 b) {
	float fa = BF16ToFP32(a);
	float fb = BF16ToFP32(b);
	return (fa != fb);
}

//inline bool bf16_and(__bf16 a, __bf16 b) {
//	return (BF16ToFP32(a) && BF16ToFP32(b));
//}
//
//inline bool bf16_or(__bf16 a, __bf16 b) {
//	return (BF16ToFP32(a) || BF16ToFP32(b));
//}
//
//inline bool bf16_not(__bf16 a) {
//	return !(BF16ToFP32(a));
//}

