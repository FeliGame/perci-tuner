#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

#define M_PI 3.14159265358979323846

double ex0(double lat1, double lat2, double lon1, double lon2)
{
    // printf("flag\n");
    double dLon = lon2 - lon1;
    double s_lat1 = sin(lat1);
    double c_lat1 = cos(lat1);
    double s_lat2 = sin(lat2);
    double c_lat2 = cos(lat2);
    double s_dLon = sin(dLon);
    double c_dLon = cos(dLon);
    return atan(((c_lat2 * s_dLon) / ((c_lat1 * s_lat2) - ((s_lat1 * c_lat2) * c_dLon))));
}

int main()
{
    // 输入两个点的经纬度（以弧度表示）
    double lat1 = 40.7128 * M_PI / 180.0;   // 例如纽约的纬度（单位：弧度）
    double lon1 = -74.0060 * M_PI / 180.0;  // 例如纽约的经度（单位：弧度）
    double lat2 = 34.0522 * M_PI / 180.0;   // 例如洛杉矶的纬度（单位：弧度）
    double lon2 = -118.2437 * M_PI / 180.0; // 例如洛杉矶的经度（单位：弧度）

    // 调用 ex0 函数，计算两点之间的角度
    int count = 5000000;
    double result = 0;
    while (count--)
    {
        /* code */
        result = ex0(lat1, lat2, lon1, lon2);
    }

    // 输出结果（以弧度和度为单位）
    printf("%.13f", result);

    return 0;
}
