// RUN: rm -rf "%t" && mkdir -p "%t/answers" && cp "%S/mock_answers/1.txt" "%t/answers/1.txt" && env PERCI_OPENCODE_BIN="%S/mock_opencode.sh" MOCK_OPENCODE_DIR="%t" timeout -k 2 180 sh -c 'cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize -algorithm=strategy -agent=opencode -max-evals=4 "%s" -lm' > %t.out 2>&1
// RUN: grep -q "Strategy agent: analyzing program structure" %t.out
// RUN: grep -q ">>> Strategy: LLM generated -> strategy_generated.jsonl" %t.out
// RUN: grep -q ">>> Strategy finished (mock-agent-done)" %t.out
// RUN: grep -q ">>> LLM ▸" %t.out
// RUN: ! grep -E 'fatal error:|: error:|error generated|^>>> Perci-Tuner Error' %t.out
// RUN: test -f %t/strategy_generated.jsonl && test -f %t/run_history.csv

/* strategy_agent_generate.c -- M2 lit 测试：Agent 生成策略端到端搜索
 * 用 mock opencode 服务（PERCI_OPENCODE_BIN 指向同目录 mock_opencode.sh，
 * 回答来自 mock_answers/1.txt）模拟 LLM 返回策略，验证：无 -strategy 时
 * -agent=opencode 走“结构分析 -> 生成 strategy_generated.jsonl -> 执行”的
 * 完整链路，并且生成过程有 SSE 流式输出（">>> LLM ▸"）。不依赖真实 LLM。
 */
#include <stdio.h>
#include <math.h>

int main(void) {
    double acc = 0.0;
    double x = 0.125;
    double scale = 1e-4;
    for (int i = 0; i < 5000; i++) {
        acc += sin(x + (double)i * scale);
        x += 1e-3;
    }
    printf("%.17g\n", acc);
    return 0;
}
