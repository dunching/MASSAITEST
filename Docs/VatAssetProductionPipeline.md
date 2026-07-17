# Crowd Demo VAT 资源生产管线

## 1. 文档职责

[INFERRED][HIGH] 本文件是 Demo 自制静态网格、骨骼网格、动画序列和 AnimToTexture VAT 的生产合同；它不定义战斗业务、Particle 运动或 T7 关卡验收。

[COMPUTED][HIGH] 本管线不读取、迁移、复制或重命名 `E:\Projects\SuperInvincibleTank_BugFix` 的 `.uasset`、源模型或动画。原工程只用于确认需要五种可辨识状态、30fps采样和批量 ISM/VAT 表现这一需求边界。

## 2. 权威生成链

```text
Scripts/BuildCrowdDemoVatSource.py
→ Blender 5.0 headless
→ 新建低模虫体、12骨骼骨架、五个Action
→ Intermediate/CrowdDemoVatSource/*.fbx + manifest + .blend

Scripts/BuildCrowdDemoVatAssets.py
→ 完整 UE 5.7 Editor 隐藏会话
→ 导入 SkeletalMesh / 5×AnimSequence / StaticMesh
→ 创建3张纹理与AnimToTexture DataAsset
→ Bone VAT bake
→ 创建5个状态MaterialInstance和2个运行时手动播放MaterialInstance
→ /Game/CrowdDemo/VAT/T7

Scripts/ValidateCrowdDemoVatAssets.py
→ 只读检查类型、引用、帧范围、UV、纹理尺寸和材质Parent
```

[COMPUTED][HIGH] `Intermediate/CrowdDemoVatSource` 是可删除的生成中间物，不是事实源；生成脚本和最终 `/Game/CrowdDemo/VAT/T7` Unreal资产才属于工程交付物。

## 3. 五状态帧合同

| VisualState | AnimSequence | VAT范围 | Loop |
|---|---|---:|---|
| [COMPUTED][HIGH] Idle | `A_CrowdDemoBug_Idle` | `0–24` | 是 |
| [COMPUTED][HIGH] Move | `A_CrowdDemoBug_Move` | `25–49` | 是 |
| [COMPUTED][HIGH] Attack | `A_CrowdDemoBug_Attack` | `50–74` | 否 |
| [COMPUTED][HIGH] HitReact | `A_CrowdDemoBug_HitReact` | `75–99` | 否 |
| [COMPUTED][HIGH] Death | `A_CrowdDemoBug_Death` | `100–124` | 否 |

[COMPUTED][HIGH] 每个源序列固定25个采样帧，DataAsset固定30fps、Bone mode、16-bit、单骨骼影响、总帧数125。Death是实际烘焙的第五段，不使用越界帧、末帧钳制或其他状态替代。

[COMPUTED][HIGH] VAT静态网格关闭自动生成Lightmap UV，保留UV0作为普通网格坐标，UV1由AnimToTexture写入骨骼权重查找数据。当前验证结果为UV通道数2。

## 4. 当前生成结果（2026-07-16）

[COMPUTED][HIGH] 已生成并保存：1个SkeletalMesh、1个Skeleton、1个StaticMesh、5个AnimSequence、1个AnimToTexture DataAsset、3张Bone VAT纹理、5个状态MaterialInstance和2个运行时手动播放MaterialInstance。

[COMPUTED][HIGH] 只读验证结果：`ranges=[0–24,25–49,50–74,75–99,100–124]`、`uv_count=2`、Bone position/rotation纹理=`12×126`、bone weight纹理=`230×2`、材质实例=`7`、静态网格原始bounds=`2.402×2.040×2.496cm`。

[COMPUTED][HIGH] 运行时材质关闭 AutoPlay；客户端 `CrowdInstances` 通过 per-instance custom data 写入 Frame、PreviousFrame 与 HitFlashIntensity，从同一 VAT mesh/material 按实体播放五种状态。8436 已证明五种状态均被提交。

[COMPUTED][HIGH] HitFlashIntensity 同时驱动红色同帧VAT overlay的启用与强度缩放；8442逐帧检查已证明可见改色。运行时统一scale=`34`使视觉footprint约为`81.7×69.4×84.9cm`，不改变Particle碰撞事实。

## 5. 生产约束与禁止项

- [INFERRED][HIGH] 不允许把原工程资产路径写入生成脚本或DataAsset。
- [INFERRED][HIGH] 不允许通过文件系统复制`.uasset`，也不允许用旧帧号硬编码覆盖生成清单。
- [INFERRED][HIGH] Blender输出必须可从空目录重复生成；五个动画FBX必须来自五个不同Action。
- [INFERRED][HIGH] Unreal导入和烘焙必须在完整Editor会话执行。UE5.7的AssetTools在Python commandlet保存路径会触发Slate断言，不能把commandlet作为受支持的写入入口。
- [INFERRED][HIGH] 运行时clip映射必须消费DataAsset/生成描述符中的实际范围；客户端仍只负责视觉播放，不计算伤害或运动。
- [INFERRED][HIGH] T7只有在真实关卡中完成五状态、命中闪色、击退/击飞、双端同步和录像门后才能标记通过；资源存在不等于能力通过。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
