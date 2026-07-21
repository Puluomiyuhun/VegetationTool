# SlowTree → UE PVE 对接方案（枝干骨架 + PCG 撒程序集）

> 目标：在 SlowTree 中完成【生成/编辑枝干骨架】+【撒枝叶程序集(foliage instancer)】，
> 直接产出 UE PVE 能吃的数据，跳过 PVE 编辑器"把导入模型重构成难看造型"这一步。

---

## ★★★ 痛点修正（2026-07-20，替代原"一、背景与痛点"的理解）

**原先理解错了**：以为痛点是"PVE 从 mesh 反推骨架难看"。
**真实痛点**：有了骨架后，**PVE 的 `MeshBuilder` 节点会拿骨架重新扫出树皮 mesh**，这个重建又朴素又**毁掉美术在 SpeedTree 里精心做的枝干造型**。

### 源码验证（决定性）
`PVExportHelper.cpp:1368` 导出 SkeletalMesh 时：
```cpp
DynamicMesh = CollectionToDynamicMesh(ExportedCollection);  // ← base 树皮 mesh
```
`CollectionToDynamicMesh`(:380) 直接把 Collection 里的 **FGeometryCollection 顶点/面**转成 base mesh，**不在导出时现算**。
→ 那些顶点是谁塞进 Collection 的？是 **`MeshBuilder` 节点**（把骨架点序列扫成管状树皮）。
→ **结论：只要走 PVE 节点图，MeshBuilder 就一定会用它自己的朴素算法重建树皮，绕不开。** 叶子(Foliage)是之后 `BuildNaniteAssemblyData` 作为 Nanite Assembly Part 挂上去的，与树皮 mesh 分离。

### 因此真正的目标改为
**保留 SlowTree/SpeedTree 原始枝干 mesh 一个顶点都不动，只借用 PVE/引擎的"叶子=可复用 Nanite Assembly Part、省内存"机制。**

### Nanite Assembly 的本质（关键认知）
一个 **Skeletal Mesh Nanite Assembly** = `base mesh(树干) + skeleton` ＋ 一组 **Part(叶/枝预制网格)**，每个 Part 通过 `(transform, boneIndex, weight)` 被实例化挂到骨骼节点上。**base mesh 可以是任意网格**，不必是 MeshBuilder 生成的。构建器是引擎核心的 `FNaniteAssemblyDataBuilder`（不在 PVE 插件里），PVE 只是它的一个调用方。

### 三条候选路线（新）
| 路线 | 做法 | 是否保留原 mesh | 是否碰 MeshBuilder |
|---|---|---|---|
| **甲：绕开 PVE 图，直连 Assembly** ★首选 | SlowTree 导出【原枝干 mesh】+【叶实例(transform+bone)】→ 用引擎 `FNaniteAssemblyDataBuilder` 或 **USD NaniteAssemblyRootAPI** 直接组装 Skeletal Mesh Nanite Assembly | ✅完全保留 | ✅完全不碰 |
| **乙：喂 Collection 带顶点，跳过 MeshBuilder** | 想办法让 PVE Collection 直接携带外部 mesh 顶点(FGeometryCollection 几何组)，PVE 图里删掉 MeshBuilder，只用 Foliage+Export | ✅保留(若顶点能塞进去) | ✅跳过 |
| **丙：走 JSON 骨架 + MeshBuilder** | 原方案 | ❌树皮被重建 | ❌ |

- **路线甲**最干净：**根本不用 PVE 编辑器**，SlowTree 直接产出引擎能用的 Nanite Assembly（走 USD 或写个极小的 UE 编辑器脚本/commandlet 调 `FNaniteAssemblyDataBuilder`）。这也符合社区已验证的 "SpeedTree→Houdini→USD NaniteAssemblyRootAPI→UE" 工作流。
- **路线乙**需要验证 Collection 能否被外部顶点填充且 Export 不再触发 MeshBuilder——技术上可行但要改/绕 PVE 图。
- 下面第四节起的"JSON 骨架"内容属于**路线丙**，仅当仍想用 PVE 撒叶规则时才参考；**若目标是保留原 mesh，应转向路线甲**。

### USD 路线的硬性要求（来自 UE 论坛 Epic 官方回复）
Skeletal Mesh Nanite Assembly 的 USD 结构：
- Root Xform：`NaniteAssemblyRootAPI`，`meshType="skeletalMesh"`，引用树干的 skeleton
- SkelRoot + 树干 mesh 几何（**base mesh 必须有几何**，否则导入失败）
- Point instancer（叶实例）：`NaniteAssemblySkelBindingAPI`，设 `bindJoints` + `bindJointWeights`
- Prototypes 组：每个叶预制体（可 reference 外部 USD）+ 其 skeleton
- 注意：当前 Nanite Assembly 的 Part **只做刚性动画**（不弯曲），风效靠骨骼刚性摆动。

---

## 一、背景与痛点

UE 5.7/5.8 的 **Procedural Vegetation Editor (PVE)** 理念：
- 导入一个**带骨骼的枝干(trunk/branch skeleton)**作为 base；
- 在骨架上用规则分布**撒枝叶**（叶/花是可复用的 **Nanite Assembly Part / 程序集 prototype**）；
- 最终产出 **Skeletal Mesh + Nanite Assembly**：每片叶子只在内存里存一份网格，成千上万个实例只是"骨骼节点 + transform"，内存极省，还支持骨骼驱动风。

痛点：PVE 若走 **StaticMesh 导入**路径，会用 `PVImporter_StaticMesh` 从网格几何**反推骨架**（射线找中心线→折叠→BFS 连接），这个反推经常把枝干重构成难看造型。

**破局点（本方案核心）**：PVE 内部另有一条 **JSON → 骨架** 的官方通道，完全绕开网格反推。

---

## 二、关键源码发现（PVE 侧）

插件路径：`F:\Z2Project\Engine\Plugins\Experimental\ProceduralVegetationEditor`

### 2.1 两条数据入口
| 入口节点 | 实现 | 输入 | 是否反推骨架 |
|---|---|---|---|
| `PVExtractFromMeshSettings` | `PVImporter_StaticMesh.cpp` | 外部 StaticMesh | **是**（造型变难看的元凶） |
| **`PVGrowthDataJsonImporter`** | `PVJSONHelper.cpp::LoadGrowthDataJsonToCollection` | **JSON 文件** | **否，直接读骨架点** |

> `PVGrowthDataJsonImporter` 节点默认 `bOnlyExposeInDebugMode=true`（开发节点），但功能完整可用。
> 参数只有一个：`FFilePath GrowthDataFile`。

### 2.2 内部数据模型（ManagedArrayCollection + Facade）
- **Points 组**：`Position`(FVector3f)、`Scale`(=半径)、`LengthFromRoot`、`budDirection` …
- **Branch/Primitives 组**：`points`(该枝包含的 point 索引数组)、`parents`、`children`、`branchNumber` …
- **Bones 组**：不需要我们提供——PVE 的 `BoneReduction` 节点会从骨架 Point 序列自动生成骨骼树。
- **Foliage 组**：每个叶实例 = `NameId + BranchId + PivotPoint + UpVector + NormalVector + Scale + LengthFromRoot`。
  - 绑骨发生在**导出阶段**（`PVExportHelper::AssignBoneIDsToFoliage`）：按 `BranchId` + `LengthFromRoot` 落在骨骼区间自动**单骨绑定**，我们**不用**自己算骨骼权重。

### 2.3 JSON Schema（`LoadGrowthDataJsonToCollection` 硬性要求）
必需字段（缺一即报错 `Failed to find required path`）：
```
points.positions                          // [[x,y,z], ...] 骨架点世界坐标
points.attributes.budDirection.values     // [[x,y,z], ...] 每点芽/生长方向
primitives.points                         // [[p0,p1,...], ...] 每根枝含哪些 point 索引
primitives.attributes.parents.values      // [父枝索引, ...]  根枝= -1
primitives.attributes.children.values     // [[子枝索引...], ...]
primitives.attributes.branchNumber.values // [枝编号, ...]
```
属性块通用格式（`FillAttributes` 解析）：
```jsonc
"attributeName": {
  "type": "float" | "int" | "string",
  "size": 1 | 3,          // 1=标量, 3=向量
  "isArray": false | true,// true=每元素是数组(如 primitives.points/children)
  "values": [ ... ]       // 长度 = 该组元素数
}
```

### 2.4 坐标系换算（务必照做，来自 cpp 硬编码）
- **轴序交换 + 缩放**：读取时 `FVector3f(v[0], v[2], v[1]) * 100.0f`。
  → 源 JSON 用 **Houdini/DCC 约定：Y-up、米制**；X 不变，**Y↔Z 交换**，×100 转厘米进 UE(Z-up)。
- SlowTree 内部是 Y-up 米制（OpenGL 约定），**恰好匹配**：导出时直接写 `[x, y, z]`（我方 y=高度），PVE 读成 UE 的 `(x, z, y)*100`。需在导出时验证一次朝向。
- `Scale`(半径)：Growth 路径里 PVE 从 `BudLateralMeristem.LateralMeristem*100` 取半径；
  MegaPlants 路径用 `pscale*100`。→ 我们应额外提供 `pscale` 属性带半径（见下文可选字段）。

### 2.5 叶实例 JSON（`FillFoliageData`，可选但推荐）
挂在 `primitives.attributes` 下，**按枝(branch)分组**：
```
instancer_name   // [[叶资产名,...], ...]  每根枝上各叶实例引用的程序集名
instancer_pivot  // [[x,y,z,...], ...]     实例位置(米, 会*100)
instancer_UP     // [[x,y,z,...], ...]     实例 up 向量
instancer_N      // [[x,y,z,...], ...]     实例 normal 向量
instancer_scale  // [[s,...], ...]         实例缩放
instancer_LFR    // [[lfr,...], ...]       沿枝距离(定绑骨)
```
> `instancer_name` 会被拼成包路径 `PackageFolder/Name.Name`，指向 UE 内的叶网格资产。
> 也可以**不写**叶实例，改在 PVE 图里用 `FoliageDistributor` 节点撒——见路线选择。

---

## 三、总体对接策略（三条路线）

### 路线 A：SlowTree 出「骨架 JSON」，PVE 内撒叶 ★推荐先做
- SlowTree 只导出 **枝干骨架**（points + primitives + 半径 + budDirection）。
- 进 UE 走 `GrowthDataJsonImporter` → `BoneReduction` → `FoliageDistributor`(在 PVE 里选叶网格、调密度) → `Export`。
- **优点**：JSON 最简单；撒叶用 PVE 成熟的规则系统(phyllotaxy/密度/条件)；骨架完全由我们控制→造型不再被反推破坏。
- **缺点**：撒叶的美术控制留在 PVE，SlowTree 视口看不到最终叶分布。

### 路线 B：SlowTree 出「骨架 + 叶实例 JSON」，PVE 直接绑骨导出
- SlowTree 把自己已经生成好的叶簇(LeafCluster)转成 `instancer_*` 实例数据一并写进 JSON。
- 进 UE 走 `GrowthDataJsonImporter`(会连叶一起读) → `Export` 直接烘 Nanite Assembly。
- **优点**：所见即所得——SlowTree 里的叶分布 1:1 进 UE；不依赖 PVE 撒叶。
- **缺点**：叶资产名要对应 UE 里已存在的网格；JSON 更复杂；需保证 UP/N/LFR 正确。

### 路线 C：StaticMesh + 我方自带骨架（不走 JSON）
- 直接生成 StaticMesh 走 `ExtractFromMesh`。**放弃**——正是造型变难看的路径。

> **建议**：先做**路线 A** 打通全链路(SlowTree→JSON→PVE→Nanite Assembly)，验证造型不被破坏；
> 再做**路线 B** 让叶分布也一并带过去。

---

## 四、SlowTree 侧实现方案

### 4.1 数据来源：复用现有 rings
SlowTree 每根枝(Trunk/Branch/Twig/Spine)生成时都产出 `std::vector<BranchRing>`（见 `TreeGenerator.cpp`，`CylinderSegment::buildNaturalRings`）。
- `ring.center` → JSON `points.positions` 的一个点
- `ring.radius` → 该点半径(pscale)
- `ring.up`(切线方向) → `budDirection`
- 一根枝的所有 ring 圆心 → 一个 `primitives.points` 数组
- 枝的父子关系 → 现有 NodeGraph `childrenOf` + processNode 递归时已知父枝
→ **骨架数据已经全部现成，只差"收集 + 编号 + 序列化"。**

### 4.2 新增导出模块（不碰生成逻辑）
建议新增 `src/io/PVEExport.{h,cpp}`：
- 遍历时给每根枝分配全局 `branchIndex`，记录 `parentBranchIndex`；
- 累积每根枝的 point 全局索引区间；
- 统计每点 `LengthFromRoot`（沿枝累计弧长 + 父枝末端 LFR）；
- 写出符合 2.3/2.4 schema 的 JSON（用现有 JSON 写法或引入轻量 header-only json）。
- 触发方式：仿照现有 **Export 节点**（`ExportParams.requestExport` 瞬时标志 + `Application::update` 每帧检测），加一个 `ExportPVEJson` 选项/新节点。

### 4.3 路线 B 额外：叶实例收集
- `buildLeafCluster` 里每片叶卡的锚点/朝向已知(anchor/normal)。
- 按其所属枝的 branchIndex 分组，填 `instancer_pivot/UP/N/scale/LFR`；
- `instancer_name` = 用户在 UI 指定的"UE 叶资产名"（新增一个字符串参数）。

### 4.4 坐标 / 单位自检清单
- [ ] SlowTree y-up 是否与 PVE 期望一致（导一棵简单直干验证 UE 里是竖直的）。
- [ ] 半径单位：我方米 → PVE ×100 = 厘米，检查树干粗细合理。
- [ ] 根枝 `parents = -1`，`children` 索引闭合无悬空。
- [ ] `branchNumber` 唯一且从 0 递增。

---

## 五、分阶段计划

| 阶段 | 内容 | 产出 |
|---|---|---|
| **P0 探明格式** | 用 PVE 自带 SampleAssets 找一个真实 growthData JSON 样本，对齐字段（下节 TODO） | 一份可参考的真 JSON |
| **P1 骨架导出** | 实现 `PVEExport`：points/primitives/parents/children/branchNumber/budDirection/pscale | `.json`，能被 `GrowthDataJsonImporter` 读入不报错 |
| **P2 UE 验证** | 在 Test58/Z2Project 建 PVE 图：JsonImporter→BoneReduction→MeshBuilder→Export，看骨架造型 | 确认造型 = SlowTree 所见 |
| **P3 PVE 内撒叶** | 图里加 FoliagePalette+FoliageDistributor，导出 Nanite Assembly Skeletal Mesh | 路线 A 打通 |
| **P4 叶实例导出** | 实现 `instancer_*`，路线 B | SlowTree 叶分布直达 UE |
| **P5 打磨** | UI(填叶资产名/导出路径)、坐标自检、批量变体 | 可用工具 |

**MVP = P0+P1+P2**（先证明"我方骨架能干净进 PVE"），成功后再决定 A 还是 B。

---

## 六、风险 / 未决

1. **JSON 真实样本**：`LoadGrowthDataJsonToCollection` 只校验 6 个必需路径，但 `FillAttributes` 会读**所有** points/primitives attributes。需要一份 PVE 真导出的 JSON 确认可选字段（如 `budNumber`、`LOD_totalPscaleGradient`）是否影响后续节点。→ **P0 必须先拿到样本**（查 `Content/SampleAssets` 或 `JsonDirectoryPath` 机制，或在 UE 里用 MegaPlants 反导一份）。
2. **半径来源**：Growth 路径半径取自 `BudLateralMeristem.LateralMeristem`——需确认这个属性从哪个 JSON 字段来（可能是 `budData`/`lateralMeristem`）。若拿不到，退回 MegaPlants 路径(`LoadMegaPlantsJsonToCollection` 用 `pscale`)可能更稳。
3. **开发节点可见性**：`GrowthDataJsonImporter` 是 debug-only 节点，需在 PVE 里开 debug 模式才能拖出来；或改用 `UProceduralVegetationGrowthDataAsset::UpdateDataAsset()`(读 `JsonDirectoryPath` 批量转 Variants) 这条更"正式"的通道。
4. **`GrowthDataAsset.JsonDirectoryPath`**：这暗示官方工作流就是"放一堆 JSON 到目录→UpdateDataAsset→变成 Variants→GrowthDataLoader 加载"。**这可能才是最稳的对接点**，P0 要一并验证。

---

## 七、给用户的决策问题

1. **先走哪条路线**：A(骨架进去、PVE 里撒叶) 还是 B(骨架+叶一起进去)？（建议先 A）
2. **对接入口**：debug 节点 `GrowthDataJsonImporter`，还是正式的 `GrowthDataAsset + JsonDirectoryPath` 工作流？（建议先确认哪个在你的 Z2Project 里能用）
3. **P0 样本**：你能否在 UE 里先用 PVE 导出/找到一份真实 growthData JSON 给我对齐字段？还是我先按源码推断的 schema 盲写、再迭代？
4. **叶资产**：路线 B 需要 UE 里已有叶网格资产名——叶网格是你在 UE 里准备，还是也想从 SlowTree 导出？
