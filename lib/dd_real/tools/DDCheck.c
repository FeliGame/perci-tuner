#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "dd_real.h"
#include "ldouble.h"

#define DDNAME "DD"

typedef union {
	long double ld;
	dd_real dd;
	uint64_t ul[2];
} ltd;


int getEnv(double *value1, double *value2) {
    // 获取环境变量 DD 的值
    const char *env_value = getenv(DDNAME);
    if (env_value == NULL) {
        //fprintf(stderr, "环境变量 %s 未定义!\n", DDNAME);
	*value1=0;
        *value2=0;
        return 0;
    }

    // 检查环境变量值是否为空
    if (strlen(env_value) == 0) {
        //fprintf(stderr, "环境变量 %s 的值为空!\n", DDNAME);
	*value1=0;
        *value2=0;
        return 0;
    }

    //printf("env=%s\n",env_value);
    // 使用 strtok 分割字符串
    char *str = strdup(env_value); // 创建环境变量值的副本
    if (str == NULL) {
        //fprintf(stderr, "内存分配失败!\n");
	*value1=0;
        *value2=0;
        return 0;
    }

    //printf("str=%s\n",str);
    char *token = strtok(str, " "); // 以空格为分隔符
    if (token == NULL) {
        //fprintf(stderr, "环境变量 %s 的值格式不正确!\n", DDNAME);
	*value1=0;
	*value2=0;
        free(str);
        return 0;
    }

    //double value1, value2;
    char *endptr;
    ltd t;

    //printf("token1=%s\n", token);
    // 转换第一个十六进制浮点数
    errno = 0;
    t.ul[0] = strtoul(token, &endptr, 16);
    //*value1 = strtod(token, &endptr);
    if (errno != 0 || endptr == token || *endptr != '\0') {
        //fprintf(stderr, "无法解析 %s 第一个值为浮点数: %s\n", DDNAME, token);
	*value1=0;
        free(str);
        return 0;
    }
    *value1 = t.dd.x[0];
    //printf("t1=%le\n", t.dd.x[0]);

    // 获取第二个十六进制浮点数
    token = strtok(NULL, " ");
    if (token == NULL) {
        //fprintf(stderr, "环境变量 %s 的值格式不正确，缺少第二个值!\n", DDNAME);
        *value2=0;
	free(str);
        return (*value1 == 0) ? 0 : 1;
    }

    // 转换第二个十六进制浮点数
    errno = 0;
    //*value2 = strtod(token, &endptr);
    t.ul[1] = strtoul(token, &endptr, 16);
    if (errno != 0 || endptr == token || *endptr != '\0') {
        //fprintf(stderr, "无法解析 %s 第二个值为浮点数: %s\n", DDNAME, token);
        *value2=0;
	free(str);
        return (*value1 == 0) ? 0 : 1;
    }
    *value2 = t.dd.x[1];

    // 打印结果
    //printf("第一个浮点值: %.30lf\n", *value1);
    //printf("第二个浮点值: %.30lf\n", *value2);

    // 释放内存
    free(str);

    return 2;
}
int DCheck(double a) {
	ltd t1, t2;
	if (getEnv(&t1.dd.x[0], &t1.dd.x[1]) == 0) {
		fprintf(stderr, "get env error!\n");
		return -1;
	}
	//fprintf(stderr, "Env : %lx %lx\n", t1.ul[0], t2.ul[1]);

	t2.dd = dd_init(a);

	dd_real result = div_dd_dd(abs_dd(sub_dd_dd(t2.dd, t1.dd)), t1.dd);
	
	printf("%.5le\n", result.x[0]);
	return 0;
}

int DDCheck(dd_real a) {
	ltd t1, t2;
	//getEnv(&t1.dd.x[0], &t1.dd.x[1]);
	if (getEnv(&t1.dd.x[0], &t1.dd.x[1]) == 0) {
		fprintf(stderr, "get env error!\n");
		return -1;
	}
	fprintf(stderr, "Env : %lx %lx\n", t1.ul[0], t1.ul[1]);

	t2.dd = a;
	//fp128_printdd(t1.ld);
	//fp128_printdd(t2.ld);
	//printf("t1(%le %le), t2(%le %le)\n", t1.dd.x[0], t1.dd.x[1], t2.dd.x[0], t2.dd.x[1]);

	dd_real result = div_dd_dd(abs_dd(sub_dd_dd(t2.dd, t1.dd)), t1.dd);
	
	printf("%.5le\n", result.x[0]);
	return 0;
}

int main(int argc, char **argv) {
	switch(argc) {
		case 2 :
			{
				size_t size = strlen(argv[1]);
				char *endptr;
				if (size == 8) { //float
					uint32_t t = (uint32_t)strtoul(argv[1], &endptr, 16);
					if (*endptr != '\0') {
                        	                fprintf(stderr, "Error: Invalid hexadecimal number '%s'\n", argv[1]);
                        	                return 1;
                        	        }
					double tt = (double)(*(float *)(&t));
					return DCheck(tt);
				} else if (size == 16) { //double
					uint64_t t = strtoul(argv[1], &endptr, 16);
					if (*endptr != '\0') {
                        		        fprintf(stderr, "Error: Invalid hexadecimal number '%s'\n", argv[1]);
                        		        return 1;
                        		}
					double tt = *(double *)(&t);
					return DCheck(tt);		
				} 
			}
			break;
		case 3 :
			{
				//ltd ex,et;
				//ex.ld = 1.2345678987654321L;
				//FP128ToDD(ex.ld, &et.ld);
				//printf("%Lf : %lx %lx\n", et.ld, et.ul[0], et.ul[1]);
				//printf("%Lf : %le %le\n", et.ld, et.dd.x[0], et.dd.x[1]);
				//ex.ld = ;
				//printf("%Lf : %lx %lx", ex.ld, ex.ul[0], ex.ul[1])

				ltd newDD;
				char *endptr;
				newDD.ul[0] = strtoul(argv[1], &endptr, 16);

				if (*endptr != '\0') {
					fprintf(stderr, "Error: Invalid hexadecimal number '%s'\n", argv[1]);
					return 1;
				}

				newDD.ul[1] = strtoul(argv[2], &endptr, 16);

				if (*endptr != '\0') {
					fprintf(stderr, "Error: Invalid hexadecimal number '%s'\n", argv[2]);
					return 1;
				}

				return DDCheck(newDD.dd);

			}
			break;
		default :
			printf("./~ d1 [d2]\n");
			return 1;
	}

	return 0;
}
