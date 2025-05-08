#include "check_result.h"

void printf_fp16(__fp16 a)
{
	printf("\n%04x\n", *(uint16_t *)(&a));
}

void printf_fp32(float a)
{
	printf("\n%08x\n", *(uint32_t *)(&a));
}

void printf_fp64(double a)
{
	printf("\n%016lx\n", *(uint64_t *)(&a));
}

#if defined(DD)
void printf_fp106(dd_real a)
{
	uint64_t *t = (uint64_t *)&a;
	printf("\n%016lx %016lx\n", t[0], t[1]);
}
#endif

void printf_fp128(long double a)
{
	uint64_t *t = (uint64_t *)&a;
	printf("\n%016lx%016lx\n", t[1], t[0]);
}

uint64_t convEnv(char *s)
{
	char *endptr;
	errno = 0;
	uint64_t r = strtoul(s, &endptr, 16);
	if (errno != 0 || endptr == s || *endptr != '\0')
	{
		fprintf(stderr, "无法解析 %s 的值为浮点数: %s\n", ORIGINAL_RESULT, s);
		return 0;
	}
	return r;
}

// 输出相对误差
void printf_RE_fp128(long double a)
{
	const char *env_value = getenv(ORIGINAL_RESULT);

	if (env_value == NULL)
	{ // 环境变量未定义
		fprintf(stderr, "环境变量 %s 未定义!\n", ORIGINAL_RESULT);
		return;
	}

	char *str = strdup(env_value);
	if (str == NULL)
	{
		fprintf(stderr, "内存分配失败!\n");
		return;
	}

	char *token = strtok(str, " "); // 以空格为分隔符
	if (token == NULL)
	{
		fprintf(stderr, "环境变量 %s 的值格式不正确!\n", ORIGINAL_RESULT);
		free(str);
		return;
	}

	long double b;
	uint64_t t;
	switch (strlen(token))
	{
	case 4:
		t = convEnv(token);
		b = (long double)(*(__fp16 *)(&t));
		break;
	case 8:
		t = convEnv(token);
		b = (long double)(*(float *)(&t));
		break;
	case 16:
	{
		t = convEnv(token);
		b = (long double)(*(double *)(&t));

		char *token2 = strtok(NULL, " ");
		if (token2 != NULL)
		{ // double-double
			t = convEnv(token2);
			b += (long double)(*(double *)(&t));
		}
	}
	break;
	case 32:
	{
		uint64_t t2[2];
		char first16[17] = {'\0'};
		char last16[17] = {'\0'};
		strncpy(first16, token, 16);
		strncpy(last16, token + 16, 16);
		t2[0] = convEnv(last16);
		t2[1] = convEnv(first16);
		b = *(long double *)(t2);
	}
	break;
	default:
		fprintf(stderr, "%s 的值格式不正确!\n", token);
		free(str);
		return;
	}
	if (b == 0.0l)  // 防止除零，此时输出绝对误差
	{
		printf("\n%.40Le\n", a);
	}
	else 
	{
		long double err = fabsl(a - b) / b;
		printf("\n%.40Le\n", err);	
	}
	free(str);
}

void printf_RE_fp16(__fp16 a)
{
	printf_RE_fp128((long double)a);
}

void printf_RE_fp32(float a)
{
	printf_RE_fp128((long double)a);
}

void printf_RE_fp64(double a)
{
	printf_RE_fp128((long double)a);
}

#if defined(DD)
void printf_RE_fp106(dd_real a)
{
	long double t = (long double)a.x[0];
	t += (long double)a.x[1];
	printf_RE_fp128(t);
}
#endif
