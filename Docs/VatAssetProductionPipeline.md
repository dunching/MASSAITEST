# Crowd Demo VAT 资源生产管线

## 1. 文档职责

本文只定义 Demo 自制 VAT 资产的**可重复生产合同**：源资产如何生成、UE5.7 如何导入与烘焙、运行时需要哪些稳定帧范围和 Per-Instance Custom Data。

本文不证明 T7/T8 已通过，也不定义战斗、HitResponse 或 Particle 逻辑。能力状态与验收证据分别查看 `FeatureChecklist.md`、`TestScenarioMatrix.md` 和 `RangedCombatVatAndHitResponseDesign.md`。

---

## 2. 资产来源原则

本管线不复制、迁移或重命名原工程的 `.uasset`、源模型或动画二进制。

原工程只提供产品需求参考：

```text
可辨识的 Idle / Move / Attack / HitReact / Death
30 fps 统一时间基线
适合 Mass ISM / VAT 的批量表现
```

最终交付资产必须能够从当前仓库脚本和明确的外部工具环境重新生成。

---

## 3. 权威生成链

```text
Scripts/BuildCrowdDemoVatSource.py
        ↓
Blender headless
        ↓
低模虫体 + 骨架 + 五个 Action
        ↓
Intermediate/CrowdDemoVatSource/
  FBX / manifest / blend
        ↓
Scripts/BuildCrowdDemoVatAssets.py
        ↓
UE 5.7 Editor
        ↓
SkeletalMesh / AnimSequence / StaticMesh
AnimToTexture DataAsset / VAT textures / materials
        ↓
/Game/CrowdDemo/VAT/T7
        ↓
Scripts/ValidateCrowdDemoVatAssets.py
```

`Intermediate/CrowdDemoVatSource` 是可删除中间物，不是最终事实源。

生成脚本、生成清单和 `/Game/CrowdDemo/VAT/T7` 下的最终 Unreal 资产才构成交付管线。

---

## 4. 五状态帧合同

| VisualState | AnimSequence | VAT Frame Range | Loop |
|---|---|---:|---|
| Idle | `A_CrowdDemoBug_Idle` | `0–24` | 是 |
| Move | `A_CrowdDemoBug_Move` | `25–49` | 是 |
| Attack | `A_CrowdDemoBug_Attack` | `50–74` | 否 |
| HitReact | `A_CrowdDemoBug_HitReact` | `75–99` | 否 |
| Death | `A_CrowdDemoBug_Death` | `100–124` | 否 |

固定合同：

```text
Sample Rate = 30 fps
Frames Per Clip = 25
Total VAT Frames = 125
Mode = Bone VAT
```

Death 必须拥有自己的真实烘焙区间，不能依赖越界帧、末帧钳制或复用其他状态。

运行时代码必须消费 DataAsset / descriptor 中的实际范围，不能重新维护另一套历史帧号。

---

## 5. Mesh / UV / Texture 合同

VAT StaticMesh：

- UV0 保留普通网格坐标；
- UV1 由 AnimToTexture 保存骨骼查找数据；
- 不为了 VAT 自动重排已有运行时 UV 语义；
- VAT Position / Rotation / Weight texture 尺寸由 bake 结果决定，运行时代码不硬编码纹理分辨率。

资产验证至少检查：

```text
Asset type / reference
五段 frame range
UV channel
VAT DataAsset
Bone position / rotation / weight texture
Material parent
Per-instance custom data index
WPO connection
旧 overlay 资产不存在
```

---

## 6. 运行时材质合同

主体 ISM 使用同一 VAT mesh / material，不通过创建第二套“闪色副本”表现 HitFlash。

Per-Instance Custom Data 固定：

```text
slot 0 = Frame
slot 1 = PreviousFrame
slot 2 = HitFlashIntensity
```

HitFlash 在主体材质内部完成，例如：

```text
BaseColor = lerp(BaseColor, HitFlashColor, intensity)
Emissive  = BaseEmissive + HitFlashColor * intensity * strength
```

`HitFlashIntensity = 0` 时必须恢复普通材质。

视觉缩放只影响 Presentation footprint，不改变 Worker / Particle 的物理半径、HardGap 或安全事实。

---

## 7. Runtime 数据边界

Server / Worker 只发布稳定视觉事实，例如：

```text
VisualState
VisualRevision
StateStartServerTime
PlayRate
HitFlash state / revision
```

Client Presentation 根据同步 ServerTime 和 VAT descriptor 计算当前 frame 并提交 ISM custom data。

客户端 VAT 播放不得：

- 决定 Attack 合法性；
- 生成 Damage；
- 修改 Worker Movement；
- 修改生命周期；
- 用动画帧反推 gameplay authority。

---

## 8. 可重复生成要求

- Blender 源输出必须能从空的 `Intermediate/CrowdDemoVatSource` 重新生成。
- 五个动画 FBX 必须来自五个明确 Action，而不是一个文件通过运行时猜区间。
- Unreal 导入和 AnimToTexture bake 使用受支持的完整 Editor 会话。
- 脚本不得包含原工程绝对资产路径作为生产输入。
- 不通过文件系统直接复制 `.uasset`。
- 生成失败必须保留明确错误，不静默复用旧资产冒充成功。

---

## 9. 资产存在不等于能力通过

VAT 管线完成只证明：

```text
资产能够稳定生成
+
运行时拥有可消费的五状态描述
```

它不自动证明：

```text
攻击状态正确
HitResponse 正确
Knockback / KnockUp 正确
双端同步正确
视觉连续性正确
```

这些必须由 Demo 场景和 `TestScenarioMatrix.md` 单独证明。
