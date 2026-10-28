// RUN: rm -rf "%t" && mkdir -p "%t/answers" && cp "%S/mock_answers_complex/answers/1.txt" "%t/answers/1.txt" && cp "%S/mock_answers_complex/answers/2.txt" "%t/answers/2.txt" && env PERCI_OPENCODE_BIN="%S/mock_opencode.sh" MOCK_OPENCODE_DIR="%t" timeout -k 2 300 sh -c 'cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize -algorithm=strategy -agent=opencode -max-evals=8 -telemetry-interval=3 "%s" -lm' > %t.out 2>&1
// RUN: grep -q "^>>> LLM 结构分析: " %t.out
// RUN: grep -q "^>>> LLM 策略: 3 个顶层步骤" %t.out
// RUN: grep -q "^>>> Strategy checkpoint (interval): 3 evals" %t.out
// RUN: grep -q "^>>> Strategy advice: " %t.out
// RUN: grep -q "^>>> Strategy finished (agent-stop)" %t.out
// RUN: grep -q "^>>> Strategy telemetry: " %t.out
// RUN: ! grep -E 'fatal error:|: error:|error generated|^>>> Perci-Tuner Error|Traceback' %t.out

/* strategy_agent_complex.c -- 复杂基准 + Agent 策略灵活性 lit 测试
 *
 * 16 个可调元素、覆盖全部四种 kind（global / local / struct / call），
 * 精度敏感度按结构分层（累加链敏感、前馈缓冲安全），用来验证 LLM 面对
 * “该降哪些、该放弃哪些”时能否给出分层策略，而不是一刀切 eval_uniform。
 *
 * mock 回答（mock_answers_complex/）给的是一个“先粗后细 + 只探测结构上
 * 安全的元素 + 用接受结果剪枝”的策略；检查点回答要求 stop。
 */
#include <stdio.h>
#include <math.h>

struct vec3 {
    double x;
    double y;
    double z;
};

double g_scale = 1e-4;
double g_bias = 0.5;

static double helper(struct vec3 *v, double w, int n) {
    double sensitive = 0.0;
    double buffer = 0.0;
    for (int i = 0; i < n; i++) {
        double t = (double)i * 1e-3;
        sensitive += sin(t + w) - cos(t * 0.5 + w);
        buffer += sin(t * 0.25);
    }
    v->x = sensitive;
    v->y = buffer;
    v->z = sensitive + buffer;
    return sensitive * g_scale + buffer * g_scale;
}

int main(void) {
    int n = 2000;
    struct vec3 v = {0.0, 0.0, 0.0};
    double total = 0.0;
    double carry = 0.0;
    for (int k = 0; k < 40; k++) {
        double step = helper(&v, 0.5, n);
        carry += v.y;
        total += step;
        total += carry * g_bias;
    }
    printf("%.17g\n", total);
    return 0;
}
