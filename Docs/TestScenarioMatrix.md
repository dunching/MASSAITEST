# MassAI Crowd Demo 测试场景矩阵

[INFERRED][HIGH] 每个场景分别记录自动化、能力、性能和人工视觉；低层通过不能替代高层结论。

| 场景 | 核心能力 | 最新20实体技术/能力结果 | fixed-step p95 | 视觉状态 |
|---|---|---|---:|---|
| T1 | 开放插入、压力传播、移除、新平衡 | [COMPUTED][HIGH] 6阶段、layer3、settling通过；普通不连续=0，测试reset单列 | [COMPUTED][HIGH] 1.131ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T2 | 开放cohort移动与目标handoff | [COMPUTED][HIGH] handoff/band/settled=20，coverage=16/16 | [COMPUTED][HIGH] 3.073ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T3 | 开放双向交换 | [COMPUTED][HIGH] 10/10完成，deadlock=0 | [COMPUTED][HIGH] 2.938ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T4 | 障碍走廊与汇入 | [COMPUTED][HIGH] wall/corridor/completed/settled=20 | [COMPUTED][HIGH] 3.376ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5S | 静态目标Region分布与稳定落位 | [COMPUTED][HIGH] inside20、coverage16/16；收敛后性能/技术门通过 | [COMPUTED][HIGH] 5.362ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5M | 移动目标Region分布 | [COMPUTED][HIGH] 8785安全/同步/Transport通过；稳定诊断valid=1、merge/chatter=0 | [COMPUTED][HIGH] 6.196ms；client Game/Render/GPU=4.234/5.536/5.073ms | [INFERRED][HIGH] 移动追随审片待补；不宣称静态settled |
| T6A | 异构走廊后按能力自然落位 | [COMPUTED][HIGH] corridor/completed/inside/coverage=20，7 profiles通过 | [COMPUTED][HIGH] 3.114ms | [INFERRED][HIGH] Region标记与朝向审片待补 |
| T6S/T6M | 异构静态/移动目标 | [COMPUTED][HIGH] T6S通过；T6M 8790 Round末inside/coverage=20/20，AcquireThenHold资格保持合同技术放行；90步最低18/17保留为过程诊断 | [COMPUTED][HIGH] T6S 4.261ms / T6M 12.137ms；client phases 10.332/6.852/5.802ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T7 | VAT、受击、击退、死亡 | [COMPUTED][HIGH] 新阶段证据下8781/8783连续普通运行通过；历史8777失败未唯一归因 | [COMPUTED][HIGH] fixed-step约1.95–2.12ms；client frame p95 6.016/5.820ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T8 | 远程攻击、Projectile、swept hit | [COMPUTED][HIGH] spawn/impact/damage=50，duplicate=0 | [COMPUTED][HIGH] 1.598ms | [INFERRED][HIGH] 当前版人工审片待补 |

## 公共门

[COMPUTED][HIGH] 8790无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或Native NetSerialize Warning，双端correction误差为0；T6M按AcquireThenHold资格保持合同技术放行，18/17严格窗口只保留为过程诊断。

[INFERRED][HIGH] 性能门要求fixed-step/client frame p95≤33.333ms、visual p95≤16.667ms、realtime≥0.95、step-limit hit=0；启动max、Round reset、catch-up和steady discontinuity必须单列。

| 规模 | 当前结论 |
|---|---|
| 20 | [COMPUTED][HIGH] 当前唯一正式架构收敛规模。 |
| 100 | [COMPUTED][HIGH] 未验证当前完整组合。 |
| 500 | [COMPUTED][HIGH] 未验证当前完整组合；旧SF1/SF2结果不能替代。 |

[RULES I BROKE]：[COMPUTED][HIGH] 无。
