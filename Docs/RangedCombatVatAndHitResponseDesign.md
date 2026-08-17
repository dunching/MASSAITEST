# Demo 远程战斗、VAT 与受击表现设计

## 1. 文档职责

本文定义 `MassAICrowdDemo` 如何使用生产插件验证 T7 / T8 的战斗业务、受击状态和 VAT 表现。

它属于 Demo / Host 层，不是 Projectile 插件机制事实源。

职责边界：

```text
MassProjectileHitFrameworkDesign.md
= Projectile simulation / Broadphase / Sweep / ImpactFact / HitFact

本文
= Demo Attack business / HitResponse / Reactive state / VAT / HitFlash / visual acceptance
```

当前是否已经通过 T7 / T8、双端、性能或 runner，以 `FeatureChecklist.md` 和 `TestScenarioMatrix.md` 为准。

---

## 2. 四类状态必须分离

Demo 不允许用一个枚举同时表达业务、攻击阶段、受击运动和视觉。

```text
BusinessState
├── Idle
├── Move
├── AcquireTarget
├── Attack
├── Recover
└── Dead

AttackPhase
├── None
├── Windup
├── Fired
└── Recovery

ReactiveMotionMode
├── None
├── Knockback
├── KnockUp
└── LandingRecovery

VisualState
├── Idle
├── Move
├── Attack
├── HitReact
└── Death
```

各层职责：

- `BusinessState`：当前业务意图。
- `AttackPhase`：一次攻击内部阶段，确保一次 Windup 只发射一次。
- `ReactiveMotionMode`：受击产生的临时运动覆盖。
- `VisualState`：客户端应该播放哪个视觉 Clip。

这些状态之间可以相关，但不能合并成同一权威字段。

---

## 3. Visual Resolver

VisualState 只从已经确定的业务 / Combat / Reactive facts 推导。

推荐优先级：

```text
Death
> HitReact
> Attack
> Move
> Idle
```

视觉层不能反向修改 BusinessState、AttackPhase、Health 或 Movement。

HitFlash 是独立材质反馈，不需要切换 VisualState。

例如：

```text
当前正在播放 Attack
+ 收到轻量 HitFlash
```

可以继续保持 Attack Clip，同时叠加闪色，而不是强制把动画切到 HitReact。

---

## 4. VAT Profile

每个 Presentation Profile 共享版本化 VAT 描述：

```text
VisualState
→ ClipIndex
→ Frame Range
→ Loop
→ Duration
→ PlayRate
→ optional FireNormalizedTime
```

单个实体只保存必要的表现事实：

```text
VisualState
VisualRevision
StateStartServerTime
PlayRate
Stable PhaseSeed
HitFlash state
```

服务器不需要每 fixed-step 复制当前 VAT Frame。

客户端根据同步 ServerTime 推导动画进度。

---

## 5. VAT Custom Data

主体 ISM / VAT 的实例 Custom Data 固定使用明确槽位，例如：

```text
slot 0 = Current Frame
slot 1 = Previous Frame
slot 2 = HitFlashIntensity
```

HitFlashColor、Emissive Strength 等属于 Presentation Profile / Material 参数。

Visual Processor 只消费已经解析的视觉事实并提交 ISM Custom Data，不计算：

```text
Attack legality
Damage
Hit
Knockback
Death
```

---

## 6. ServerTime 与视觉连续性

视觉状态变化由：

```text
VisualState
VisualRevision
StateStartServerTime
```

确定。

客户端使用 ServerTime 计算当前 clip phase，避免网络更新频率与渲染帧率绑定。

Correction / Checkpoint 恢复时必须恢复：

```text
VisualRevision
StateStartServerTime
```

否则会出现：

```text
Attack Clip 重启
Death Clip 重播
HitReaction 重复
发射视觉重复触发
```

---

## 7. Demo 远程攻击业务链

远程攻击高层链路：

```text
Target selection / validation
        ↓
Range / Facing eligibility
        ↓
AttackPhase = Windup
        ↓
Fire eligibility
        ↓
Projectile Spawn Request
        ↓
MassCrowdProjectiles Worker Domain
        ↓
ImpactFact / HitFact
        ↓
Demo Combat Resolve
        ↓
Health / Reactive / Visual facts
```

Projectile 轨迹、Broadphase 和 Sweep 不属于 Demo 重新实现范围。

Demo 只负责：

```text
目标选择
攻击合法性
攻击阶段
Damage / Health
Reactive profile
Visual response
验收与诊断
```

---

## 8. Attack Phase

一次攻击至少使用：

```text
None
→ Windup
→ Fired
→ Recovery
→ None
```

Windup 开始时冻结本次攻击需要的稳定事实，例如：

```text
Source StableEntityRef
Target StableEntityRef + Lifecycle
Attack Sequence
Profile Revision
Simulation Tick
```

同一个 Attack / FireSequence 在 replay、重复调度或网络重试后只能生成一次 Projectile Spawn Request。

不能通过“当前正在 Attack Clip”来反推是否已经发射。

---

## 9. Attack 与 Target Region 的边界

Target Region 的 Melee / MidRange / Ranged terminal band 只回答：

> Agent 在目标周围什么距离范围内属于合法宏观终端区域。

它不回答：

```text
当前有没有合法攻击目标
是否满足 Facing
Cooldown 是否完成
Windup 是否结束
是否应该发射 Projectile
```

这些属于 Combat / Host Business。

因此不能把：

```text
AttackPhase
Projectile owner
HitFact
Damage
HitFlash
```

写进 Polar Cell / Region / Edge Quota。

---

## 10. HitFact 消费

所有 Demo 战斗命中都通过稳定 HitFact 进入统一受击处理。

HitFact 至少关联：

```text
StableHitEventId
SourceRef
Causer / ProjectileRef
TargetRef + Lifecycle
SimulationTick
HitPosition
HitDirection
EffectProfileKey
```

Demo 解析 `EffectProfileKey` 得到：

```text
Damage
HorizontalImpulse
VerticalImpulse
HitFlash profile
Status / Death effect
```

同一个 StableHitEventId 只能消费一次。

目标 Lifecycle 不匹配或已经被 Despawn 后，旧 HitFact 必须被拒绝。

---

## 11. Hit Response

Demo 的 HitResponse 将 HitFact 转换成业务和表现事实：

```text
HitFact
   ↓
Damage / Health update
   ↓
Alive / Death decision
   ↓
Reactive motion request
   ↓
VisualState / HitFlash
   ↓
Ordered gameplay / presentation event
```

业务 Apply 必须处于 Worker Result / Host Commit 的原子提交边界内。

不能先改 Health，再因为 Presentation 或 Mass validation 失败回滚一半。

---

## 12. Knockback

Knockback 是水平 Reactive Motion。

它不能在 Particle Finalize 之后直接修改 Transform。

正确路径：

```text
HitFact
→ Reactive Movement Fact / TimedImpulse
→ Movement Resolve
→ Movement Predict
→ Particle Safety
→ Final Apply
```

这样即使受到击退，实体仍不能穿过其他 Agent、墙体或 Bounds。

需要记录：

```text
Requested impulse
Realized impulse
Safety-limited reason
```

以便区分“业务请求很大”和“安全层只允许移动一部分”。

---

## 13. KnockUp

第一阶段 KnockUp 使用保守 2.5D 模型：

```text
Z
→ deterministic vertical velocity
→ gravity
→ landing
→ LandingRecovery

XY
→ 继续参加统一 Movement / Particle Safety
```

这不代表实体可以真正从其他 Agent 上方穿越。

真正的 3D 空中碰撞 / 分层穿越需要独立能力设计。

Landing 只在 fixed-step boundary 发生。

Correction / Checkpoint 需要恢复：

```text
ReactiveMotionMode
Horizontal reactive velocity
Vertical velocity
Elapsed ticks
Landing revision
Recovery state
```

---

## 14. Death

Death 是业务事实，不只是一个动画。

死亡流程至少区分：

```text
Health reaches terminal state
→ Dead business fact
→ Movement disabled / Despawn policy
→ VisualState = Death
→ Presentation lifecycle
→ eventual remove / recycle
```

VisualState = Death 不能反向决定实体已经死亡。

实体何时真正 Despawn / recycle 属于 Lifecycle / Host policy。

---

## 15. HitFlash

HitFlash 是可叠加在主体 ISM 上的短期材质反馈。

它应由稳定的：

```text
HitFlashRevision
StartServerTime
Duration
Intensity profile
```

驱动。

不需要创建第二个红色 Overlay ISM 作为长期设计。

客户端根据同步时间计算当前强度；服务端不逐帧复制强度。

---

## 16. Projectile Visual

Projectile Gameplay Simulation 在 Worker / Projectiles 插件中进行。

Demo / Presentation 只消费：

```text
Spawn
Update / Correction when needed
Impact
Expire
```

视觉可以使用：

```text
ISM
Niagara
Trail
Impact VFX
```

但不能为每颗大规模 Projectile 建立持续复制 Actor 作为主生产路径。

Projectile visual 被裁剪、隐藏或回收不能改变服务端 Projectile lifecycle。

---

## 17. T7 验收职责

T7 主要验证受击与 VAT 表现链：

```text
Move / Idle visual
HitReact
Knockback
KnockUp
LandingRecovery
Death
HitFlash
ServerTime visual continuity
```

验收必须同时使用：

```text
权威日志 / sidecar
+ 连续视频 / contact sheet
+ 人工审片
```

视频不能反推 Damage / HitEvent 是否正确；业务正确性以权威事实为准。

---

## 18. T8 验收职责

T8 主要验证：

```text
Target selection
Attack eligibility
Windup
Single Fire per attack
Projectile Spawn
Impact / HitFact
Damage
HitResponse
Projectile visual lifecycle
Golden / ordered event consistency
```

Projectile 内部算法由 `MassProjectileHitFrameworkDesign.md` 定义，T8 只验证 Demo 是否正确使用生产插件路径。

---

## 19. 性能与视觉验收分开

性能基线和录像验收不能使用同一次高开销运行互相替代。

推荐：

```text
Run A
→ 关闭录屏与高成本调试
→ 测 fixed-step / Worker / client frame / realtime

Run B
→ 开启可视化标签与录制
→ 看 clip continuity / hit response / black frame / freeze
```

不能用录屏状态下的帧率冒充生产性能，也不能用性能日志证明视觉正确。

---

## 20. Fail-Closed

以下情况不得继续提交部分结果：

```text
重复 HitEvent
stale Lifecycle
重复 FireSequence
非法 Attack phase transition
缺失 Projectile spawn fact
Damage / Reactive / Visual plan 不一致
错误 Profile Revision
VisualRevision 倒退
```

同一个命中不能出现：

```text
Damage 2 次
Impulse 1 次
HitFlash 3 次
```

Damage、Reactive、Visual 和 Event 必须围绕同一个稳定 HitEvent 原子关联。

---

## 21. 当前状态不属于本文

本文只定义 Demo 战斗 / VAT / HitResponse 的设计边界。

以下信息不再保存在正文：

```text
历史外部参考仓库审计
旧 32 槽 Projectile
旧 PreparedProjectiles
具体测试 PID / 端口
阶段迁移记录
旧 p95 数值流水账
```

当前 T7 / T8 是否正式通过、双端 runner 是否成功、当前 Golden 和性能证据统一查看：

```text
FeatureChecklist.md
TestScenarioMatrix.md
```

需要追溯旧实现时使用 Git 历史。
