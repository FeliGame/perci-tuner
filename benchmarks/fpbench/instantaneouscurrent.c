#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double t, double resistance, double frequency, double inductance, double maxVoltage)
{
    double pi = 3.14159265359;
    double impedance_re = resistance;
    double impedance_im = ((2.0 * pi) * frequency) * inductance;
    double denom = (impedance_re * impedance_re) + (impedance_im * impedance_im);
    double current_re = (maxVoltage * impedance_re) / denom;
    double current_im = -(maxVoltage * impedance_im) / denom;
    double maxCurrent = sqrt(((current_re * current_re) + (current_im * current_im)));
    double theta = atan((current_im / current_re));
    return maxCurrent * cos(((((2.0 * pi) * frequency) * t) + theta));
}

int main()
{
    // 定义输入参数
    double t = 0.01;           // 时间 t (单位秒)
    double resistance = 10.0;  // 电阻 R (单位欧姆)
    double frequency = 50.0;   // 频率 f (单位赫兹)
    double inductance = 0.1;   // 电感 L (单位亨利)
    double maxVoltage = 220.0; // 最大电压 (单位伏特)

    // 调用 ex0 函数计算瞬时电流
    int count = 10000000;
    double current = 0;
    while (count--)
    {
        /* code */
        current = ex0(t, resistance, frequency, inductance, maxVoltage);
    }

    // 输出结果
    printf("%.13f", current);

    return 0;
}
