// RUN: rm -rf "%t" && mkdir -p "%t/answers" && cp "%S/mock_answers_checkpoint/answers/1.txt" "%t/answers/1.txt" && cp "%S/mock_answers_checkpoint/answers/2.txt" "%t/answers/2.txt" && env PERCI_OPENCODE_BIN="%S/mock_opencode.sh" MOCK_OPENCODE_DIR="%t" timeout -k 2 240 sh -c 'cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize -algorithm=strategy -agent=opencode -max-evals=8 -telemetry-interval=2 "%s" -lm' > %t.out 2>&1
// RUN: grep -q "^>>> Strategy checkpoint (interval): 2 evals, accepted " %t.out
// RUN: grep -q "^>>> Strategy advice: 前 2 次评估均被拒绝" %t.out
// RUN: grep -q "^>>> Strategy adjustment: stop requested by agent" %t.out
// RUN: grep -q "^>>> Strategy finished (agent-stop)" %t.out
// RUN: ! grep -E 'fatal error:|: error:|error generated|^>>> Perci-Tuner Error' %t.out

/* strategy_agent_checkpoint.c -- M2/M3 lit 测试：动态搜索期间的检查点闭环
 *
 * 验证遥测的第二类职责：把 run_history 的实际执行数据（时间/配置/数值）
 * 在固定间隔检查点交给 LLM 分析，并让 LLM 的 adjustment 真正生效。
 *
 * mock opencode 依次返回两份回答（mock_answers_checkpoint/）：
 *   1) 结构分析 + 策略（逐元素降级，产生多次不同配置的评估）
 *   2) 检查点分析 + adjustment {"stop": true}
 * 断言：检查点是按 interval 触发（不是靠异常触发）、日志含数据摘要字段
 * （accepted 2/2），且 adjustment 使搜索以 agent-stop 结束。
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
