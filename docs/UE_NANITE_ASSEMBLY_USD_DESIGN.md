# SlowTree → UE Nanite Assembly (USD) 对接方案 v2

> **确定方向**（用户 2026-07-20 拍板）：
> - 枝干 mesh + 骨骼来自 **SpeedTree 导出**（连骨骼一起，走 FBX/XML，因为 SpeedTree 的 USD 导出**不带**Nanite Assembly 结构）；
> - SlowTree 负责【读带骨骼枝干】+【撒叶程序集】+【导出 USD Nanite Assembly】；
> - **完全绕开 PVE 节点图**（PVE 的 MeshBuilder 会重建树皮毁造型，见下），直接产出引擎能吃的 USD。

---

## 一、为什么绕开 PVE（结论，勿再走回头路）

`PVExportHelper.cpp:1368` 导出时 `DynamicMesh = CollectionToDynamicMesh(ExportedCollection)`，
`CollectionToDynamicMesh`(:380) 把 Collection 的 FGeometryCollection 顶点转成 base 树皮 mesh，
这些顶点由 **MeshBuilder 节点**用朴素扫管算法生成。
→ **只要进 PVE 图，MeshBuilder 必重建树皮，美术造型必被毁，无解。**

**但 Nanite Assembly 机制本身与 PVE 无关**：它由引擎核心 + USDCore 插件实现，
可用 **USD (NaniteAssembly* schema)** 直接喂给引擎的 USD 导入器，base mesh 用 SpeedTree 原始枝干，一个顶点不动。
这正是社区已验证的 "SpeedTree→(Houdini)→USD NaniteAssemblyRootAPI→UE" 路线，我们用 SlowTree 取代 Houdini 那一步。

---

## 二、目标 USD 结构（权威，来自引擎 schema + utils 源码）

来源：
- schema 定义：`Engine\Plugins\Runtime\USDCore\Resources\...\unreal\schema.usda:305-371`
- 读取逻辑：`Engine\Plugins\Runtime\USDCore\Source\USDUtilities\Public\USDNaniteAssemblyUtils.h`
- 导入器：`USDImporter\...\USDNaniteAssemblyTranslator.cpp`、`Interchange\...\NaniteAssemblySchemaHandler.cpp`

### 2.1 层级骨架
```
/Root                         (Xform)  ← 应用 NaniteAssemblyRootAPI
  · meshType = "skeletalMesh"
  · rel skeleton = </Root/TrunkSkelRoot/Skel>   (必须指向下面的 Skeleton prim)
  ├─ TrunkSkelRoot            (SkelRoot)         ← base 树干（SpeedTree 原始 mesh）
  │    ├─ Skel                (Skeleton)         ← 树干骨骼（SpeedTree 导出的 bones）
  │    │    · joints          = [关节路径数组]
  │    │    · bindTransforms / restTransforms
  │    └─ TrunkMesh           (Mesh)             ← 原始树皮几何 + UsdSkelBindingAPI 蒙皮到 Skel
  │
  ├─ LeafInstancer            (PointInstancer)   ← 叶实例；应用 NaniteAssemblySkelBindingAPI
  │    · prototypes = [</Root/LeafInstancer/Protos/Leaf_A>, ...Leaf_B...]
  │    · protoIndices = [每实例选哪个原型]
  │    · positions / orientations / scales  = [每实例 transform]
  │    · primvars:unreal:naniteAssembly:bindJoints      = [每实例绑定的关节名]  (uniform token[])
  │    · primvars:unreal:naniteAssembly:bindJointWeights = [权重]              (uniform float[], 可选)
  │    · primvars:...:bindJoints 的 elementSize metadata = 每实例关节数(通常 1)
  │    └─ Protos               (Scope)
  │         ├─ Leaf_A          (Xform/Mesh)  叶预制体 A（几何内嵌 或 用 ExternalRefAPI 引用 UE 资产）
  │         └─ Leaf_B          (Xform/Mesh)  叶预制体 B
```

### 2.2 确切属性名（照抄，别拼错）
| Schema | 应用到 | 属性 | 类型/值 |
|---|---|---|---|
| `NaniteAssemblyRootAPI` | Xform | `unreal:naniteAssembly:meshType` | `uniform token` = `"skeletalMesh"` |
| | | `unreal:naniteAssembly:skeleton` | `rel` → Skeleton prim（必须是后代） |
| `NaniteAssemblySkelBindingAPI` | Xform/Mesh/SkelRoot/**PointInstancer** | `primvars:unreal:naniteAssembly:bindJoints` | `uniform token[]` 关节名/路径；PointInstancer 上需 `elementSize`=每实例关节数 |
| | | `primvars:unreal:naniteAssembly:bindJointWeights` | `uniform float[]`（可选，缺省等权） |
| `NaniteAssemblyExternalRefAPI` | Xform | `unreal:naniteAssembly:meshAssetPath` | `uniform token` = UE 包路径 `/Game/.../Leaf.Leaf`（用于引用已在 UE 里的叶资产，不内嵌几何） |

### 2.3 硬性约束（Epic 官方论坛 + 源码）
1. **base mesh 必须有几何**（哪怕一个隐藏三角形），否则导入失败。我们有真实树干，满足。
2. Assembly **不能嵌套**：一个 mesh 不能既是 base 又是 part。
3. **叶 Part 只做刚性动画**（不弯曲）——风效靠骨骼刚性摆动，可接受。
4. skeleton 必须是 root 的**后代 prim**。
5. 叶实例绑骨：`bindJoints` 用关节名，`elementSize` 指定每实例几根关节（我们用 1=单骨，权重 1）。

### 2.4 叶预制体二选一
- **内嵌**：叶 mesh 几何直接写进 USD 的 Protos 下（自包含，单文件可分发）。
- **引用**（`NaniteAssemblyExternalRefAPI` + `meshAssetPath`）：指向 UE 里已有的叶 StaticMesh 资产（USD 更小，但依赖 UE 内资产先就位）。
→ 建议先做**内嵌**（不依赖 UE 侧预置资产，最易验证）。

---

## 三、SpeedTree 侧准备（用户负责，需验证）

- SpeedTree 导 **FBX，勾选 Bones/Skeleton**（"Bones" 或 "Wind+Bones"），得到带骨骼+蒙皮的枝干。
  - ⚠️ SpeedTree 的 **USD 导出不带 Nanite Assembly 结构**（Epic 明确说其 USD 是私有约定），所以骨骼走 FBX。
  - ⚠️ "Bones + Hierarchy grouping" 会把每组原点移到世界原点，导出前留意。
- 叶片：可以让 SpeedTree 把**单片叶/一簇叶**单独导出成一个小 mesh 作为"程序集原型"。

**SlowTree 要能读 FBX（含 skeleton + skin weights）**——这是新增能力，见下节风险。

---

## 四、SlowTree 侧实现分解

### 4.1 需要的新能力
| 模块 | 作用 | 现状 |
|---|---|---|
| **FBX 导入(带骨骼)** | 读 SpeedTree 枝干 mesh + skeleton + skin weights | ❌ 需新增（评估用 UE 无关的轻量 FBX 库，如 ufbx） |
| **叶预制体来源** | 一/多个叶 mesh 作原型 | 可从 FBX 读，或用 SlowTree 现有 LeafCluster 单叶 |
| **撒叶(分布)** | 在枝干表面/末端按密度/朝向撒实例，输出 (protoIndex, transform, 绑定关节) | 部分可复用现有 buildLeafCluster 的锚点/朝向逻辑 |
| **关节绑定** | 每叶实例找最近骨骼→关节名 | 需要 skeleton 层级 + 每叶位置最近关节查询 |
| **USD 写出** | 生成 §2 结构 | ❌ 需新增（评估内嵌 tinyusdz / 或直接写 .usda 文本） |

### 4.2 撒叶数据 = 复用现有 rings/anchor
SlowTree 现有 `buildLeafCluster` 已算出每片叶卡的 anchor/normal/朝向（leaf 顶点含 anchor3）。
撒叶实例只需：
- `position` = 叶锚点（映射到 SpeedTree 枝干表面坐标系）
- `orientation` = 由 up/normal 构造四元数
- `scale`
- `protoIndex` = 选哪个叶原型
- `bindJoint` = 该位置最近的 SpeedTree 骨骼关节名

### 4.3 坐标系
- SlowTree/OpenGL：Y-up 米制。USD 默认 Y-up。UE 导入器负责转 Z-up。
- 写 USD 时设 `upAxis = "Y"`，用米。让 UE USD 导入器统一处理换算（比手动 ×100 + 轴交换更稳）。
- 与 §"路线丙"里 Houdini JSON 的 `(x,z,y)*100` **不同**——那是 PVE 私有约定，此处走标准 USD 不需要。

---

## 五、分阶段计划（MVP 优先验证机制）

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0 机制验证**（不写 SlowTree 代码） | 在 UE 里**手工**造一个最小 USD：1 树干(带骨骼)+1 PointInstancer(几片叶,绑骨)，用引擎 USD 导入器导入 | 确认生成 Skeletal Mesh Nanite Assembly、叶正确绑骨、内存/实例正常。**这一步定生死** |
| **P1 USD 写出器** | SlowTree 加 `UsdAssemblyExport`：给定树干mesh+骨骼+叶实例列表，写出 §2 的 .usda | 生成的 USD 能被 P0 同样流程导入成功 |
| **P2 FBX 读入** | 读 SpeedTree FBX 的 mesh+skeleton+skin | SlowTree 视口能显示带骨骼枝干 |
| **P3 撒叶+绑骨** | 在枝干上撒叶实例，每叶绑最近关节 | 导出 USD 进 UE，叶分布=SlowTree 所见 |
| **P4 打磨** | 多叶原型、密度/朝向 UI、内嵌vs引用切换、批量 | 可用工具 |

**MVP = P0 + P1**（先证明"SlowTree 写的 USD 能变成正确的 Nanite Assembly"），再补 FBX 读入和撒叶。

---

## 六、风险 / 待确认

1. **P0 必须先做**：整个方案成立的前提是"手工 USD 能导成 Nanite Assembly"。建议**先在 UE 里手搓一个最小 USD 验证**，再投入写 SlowTree 代码。
2. **FBX 带骨骼读取**：SlowTree 目前无 FBX 导入。需引入库（ufbx 轻量、纯C、MIT，推荐）。或退一步：先用 SlowTree 自己生成的枝干+rings 骨架做 base（跳过 FBX），把 USD 管线打通，之后再接 SpeedTree FBX。
3. **USD 写出库**：MSVC 下引入完整 OpenUSD 较重；`.usda` 是文本格式，可**直接字符串写出**（最轻，先用这个验证），或引入 tinyusdz。
4. **叶原型的 UE 材质**：内嵌几何的 UV/材质如何对到 UE？P0 手工验证时一并确认。
5. **风**：Nanite Assembly 叶只有刚性动画。若要叶自身摆动，需靠骨骼层级更细——先不追求。

---

## 七、给用户的问题

1. **P0 谁来做**：你在 UE 里手工搭最小 USD 验证机制，还是我先写一个"生成最小验证 USD"的独立小脚本给你导？
2. **FBX 还是先用 SlowTree 自生成枝干**打通 USD 管线？（建议先自生成，把 USD 写出跑通，再接 SpeedTree FBX，降风险）
3. **USD 写出**：先用最轻的"直接写 .usda 文本"验证，还是一步到位引入 USD 库？（建议先文本）
4. 叶预制体先走**内嵌几何**还是 **ExternalRef 引用 UE 资产**？（建议内嵌）
