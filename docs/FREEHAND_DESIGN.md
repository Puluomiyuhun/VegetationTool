# SlowTree 手绘枝条（Freehand）功能 · 设计方案

> 状态：设计草案，未写代码。对标 SpeedTree "Hand Drawing" 功能。

---

## 1. SpeedTree Freehand 是什么（调研结论）

参考 SpeedTree / Unity SpeedTree Modeler 官方文档，核心机制：

1. **绘制**：按住空格 + 鼠标左键拖拽，在**垂直于当前视图的 2D 平面**上画一条线。画的过程中实时生成 3D 枝条几何。起点可以是地面圆盘（Tree node）或任意已有枝条（手绘的或程序化的）表面。
2. **转样条**：画完后，笔迹被拟合成 **Bezier 样条 + 一串控制点**。`Curve Fit` 标量控制控制点密度（1=精确但点多，<0.5=点少但不精确）。
3. **编辑**：进入节点编辑模式，点枝条显示控制点；可拖控制点/手柄，**把点拖出原 2D 平面来增加三维深度**。控制点风格：Corner / Linear / Smooth（决定切线手柄如何联动）。
4. **混合程序化**：手绘枝条上**仍可挂程序化子节点**（Twig / Leaf / Branch 等），即"该精确的地方手绘，其余交给程序化"。
5. **蒙皮属性照旧**：半径锥度、噪声、welding 等蒙皮属性对手绘枝条依然生效（只是 Generation 分布参数不适用——每个点是手动加的）。

### 对 SlowTree 的取舍

SpeedTree 的完整版很重（平板压感、Bezier 手柄联动、Resample、subdivision surface）。SlowTree 是轻量工具，建议**抓核心、砍花哨**：

- ✅ 要：鼠标画线 → 拟合控制点 → 沿控制点生成圆柱枝条 → 可拖点微调 → 手绘枝条能挂子节点。
- ⚠️ 简化：先用 **Catmull-Rom / 折线圆滑**代替完整 Bezier 手柄编辑（控制点只有位置，无独立切线手柄），交互成本大幅降低，视觉上足够自然。后续需要再升级到 Bezier 手柄。
- ❌ 暂不做：平板压感、Resample UI、多套 Curve Fit 预设、subdivision。

---

## 2. 现有架构关键结论（决定方案形态）

| 方面 | 现状 | 对 Freehand 的影响 |
|------|------|--------------------|
| 视口 | ViewportPanel 渲染到 FBO，再 `ImGui::Image` 贴回（UV 上下翻转） | 屏幕叠加绘制用 `ImDrawList` 画在 Image 上；`aspect` 只在 `ViewportPanel::render` 内可得，**交互入口必须放这里** |
| 相机 | `OrbitCamera` 球坐标，VP 矩阵每帧现算不缓存 | 反投影需自行 `inverse(proj*view)` |
| 拾取 | CPU 端射线-三角求交（Möller-Trumbore），`Renderer::pickNode` 内联了反投影 | **抽出公共 `screenToWorldRay()`**，Freehand 与 picking 共用 |
| 渲染叠加 | grid 用 `m_gridShader` + `GL_LINES` 画任意线；无通用 overlay 入口 | 3D 预览线可复用 grid shader；或直接屏幕空间 ImDrawList |
| 生成触发 | `Application::update`：`graph.isDirty()` 或选中变化 → `generator.generate()` → 上传 | Freehand 改动后 `markDirty()` 即可复用整套重生成路径 |
| 几何登记 | 每个 build 走 `getBatch → appendCylinder → afterAppend`；`afterAppend` 登记拾取三角+高亮几何 | **手绘枝条几何必须走 `afterAppend`** 才能被点选/高亮 |
| 交互范本 | LeafCutoutEditor：`InvisibleButton` 捕获三键，加点/拖点(8px 命中)/删点(右键)/拖动状态 `m_dragIdx` | **这套交互逻辑几乎可照搬**，区别仅坐标映射（2D screenToUv → 3D 反投影+平面求交） |
| 无全局编辑模式 | 选中节点 `NodeId&` 引用层层传递；模式化编辑靠独立子窗口 + 瞬时 `requestXxx` 标志 | 需新增一个"手绘模式"标志（放 Application），引用传入 ViewportPanel，避免和左键轨道旋转冲突 |
| 新节点接线点 | `NodeType` 枚举 / `makeNode` / `nodeTypeName`+`parseNodeType` / `processNode` switch / windW 表 / ProjectIO 读写 | 新增节点类型需 6 处同步登记 |
| Bezier 能力 | `CylinderSegment::buildCurvedRings(p0,p1,p2,...)` 已有二次 Bezier | 多段折线可分段调用或新增 Catmull-Rom |

---

## 3. 方案设计

### 3.1 数据模型 —— 新增 `FreehandNode`

新增节点类型 `NodeType::Freehand`。参数结构（`NodeTypes.h`）：

```cpp
// 一根手绘枝条：一串世界空间控制点 + 半径/锥度/子节点挂接
struct FreehandStroke {
    std::vector<glm::vec3> points;   // 有序控制点（世界坐标）
    float startRadius = 0.08f;       // 起点半径
    float endRatio    = 0.25f;       // 末端半径比例
};

struct FreehandParams {
    std::vector<FreehandStroke> strokes;  // 可画多根
    int   sides       = 6;
    int   lengthSegs  = 12;      // 每段样条细分（曲线平滑度）
    float taperPow    = 1.2f;
    float smoothing   = 0.5f;    // Catmull-Rom 张力 / 折线圆滑度
    // 交互瞬时标志（不序列化或仅存布尔）
    bool  requestDraw = false;   // UI 点击"开始手绘"→ 置 true，进入手绘模式
    MaterialParams material = { ... };
};
```

**为什么是节点而非全局对象**：与现有架构一致（节点图 = 单一数据源），手绘枝条能像其他节点一样挂子节点、参与导出、序列化、拾取。手绘节点的**输入**可连到 Trunk/Branch（表示"从这根父枝表面发出"），也可无输入（从地面/原点发出）。

### 3.2 交互流程

```
选中/新建 Freehand 节点
  → 属性面板点 "Draw Stroke" 按钮 (置 requestDraw)
  → UIManager 检测 → 通知 ViewportPanel 进入手绘模式 (m_freehandNode = id)
  → 视口 hover 时：
       · 左键按下  → 新建一根 stroke，记录起点
       · 左键拖动  → 沿途按最小间距采样追加控制点（屏幕空间笔迹用 ImDrawList 实时预览）
       · 左键松开  → 笔迹拟合（降采样 + Catmull-Rom），写入 stroke.points，markDirty
       · ESC/再次点按钮 → 退出手绘模式，恢复轨道相机
  → 编辑已有 stroke（非手绘模式、选中该节点时）：
       · 左键命中控制点(8px) → 拖动（反投影到"过该点、平行屏幕"的平面）
       · 右键命中 → 删除控制点
       · 拖动后 markDirty 重生成
```

**绘制平面**：起笔时确定一个平面 = 过相机 target（或起点命中的枝条表面点）、法线朝相机的平面。笔迹的每个屏幕点用 `screenToWorldRay` 反投影，与该平面求交得世界坐标。这正是 SpeedTree "垂直于视图的 2D 画布"。**Z 深度靠事后拖控制点补**（第一版可选：拖点时允许沿相机前后方向移动）。

### 3.3 几何生成 —— `buildFreehand`

在 `TreeGenerator::processNode` switch 增加分派。对每根 stroke：

1. 控制点序列 → Catmull-Rom 采样成密集中心线点（`lengthSegs` 每段）。
2. 沿中心线构造 `BranchRing` 序列：`center` = 采样点，`up` = 切线方向，`right` = PTF（平行传输）避免扭转，`radius` = 从 startRadius 按 `taperPow` 锥度插值到 `startRadius*endRatio`。
   - 可复用 `CylinderSegment` 的 PTF 逻辑（`ptfTransport`）。
3. `getBatch → appendCylinder(rings, sides, uvU, uvV) → afterAppend`（获得拾取/高亮）。
4. 末端 rings 传给 `childrenOf` 递归 → 子节点（Twig/Leaf）从手绘枝条末端或沿途生长。
   - **复用现有子节点管线**：像 Branch 一样，把 rings 传给子节点即可挂叶。

**若节点有输入父级**：起点吸附到父级表面（复用 Branch 的 `sampleRings` + 表面发射 + `appendCollar` 枝领裙边逻辑），让手绘枝根部贴合父枝。

### 3.4 需要抽出的公共工具

- `Renderer::screenToWorldRay(camera, aspect, ndcX, ndcY, glm::vec3& ro, glm::vec3& rd)` —— 从 `pickNode` 抽出，公开。
- `rayPlaneIntersect(ro, rd, planePoint, planeNormal) -> glm::vec3` —— 新增小工具。
- `CylinderSegment::buildRingsAlongPolyline(points, r0, r1, segs, taperPow, smoothing)` —— 新增，Catmull-Rom + PTF。

### 3.5 预览渲染

- **手绘中**（笔迹未提交）：ViewportPanel 用 `ImGui::GetWindowDrawList()->AddLine` 在 Image 上画屏幕空间折线（照搬 LeafCutoutEditor 绘制模式），零 GL 改动。
- **控制点显示**（选中 Freehand 节点时）：把控制点世界坐标投影到屏幕，用 `AddCircleFilled` 画小圆点；命中点高亮。同样纯 ImDrawList。
- **已提交枝条**：走正常网格生成，无需特殊预览。

---

## 4. 分阶段实施计划

| 阶段 | 内容 | 产出 |
|------|------|------|
| **P0 基础设施** | 抽 `screenToWorldRay` 公共函数；加 `rayPlaneIntersect`；ViewportPanel 加"手绘模式"标志与 hover 分支 | 能在视口点击得到世界坐标（临时打点验证） |
| **P1 画线成枝** | 新增 `FreehandNode` + 参数 + 6 处接线；`buildRingsAlongPolyline`；`buildFreehand`；左键拖拽采样→提交→生成圆柱 | 能手绘出一根 3D 枝条 |
| **P2 编辑控制点** | 控制点屏幕投影显示；拖点（反投影到屏幕平行面）；右键删点；加点 | 画完能微调形状 |
| **P3 混合程序化** | 手绘枝条挂子节点（Twig/Leaf）；有输入时根部吸附父级表面 + 枝领 | 手绘主枝 + 程序化枝叶 |
| **P4 序列化** | ProjectIO 读写 strokes（点数 + 坐标列表，仿 cutoutPoints 格式）；导出 OBJ 兼容 | 工程可保存/加载手绘枝条 |
| **P5 打磨（可选）** | Curve Fit 密度调节；Smooth/Corner 风格；Z 深度拖拽；多 stroke 管理 UI | 接近 SpeedTree 体验 |

**建议 MVP = P0+P1+P2+P4**（能画、能改、能存），P3 让它真正有用，P5 视需求。

---

## 5. 风险与注意点

1. **左键冲突**：轨道相机左键旋转 vs 手绘左键画线 —— 必须靠"手绘模式"标志切换，模式内禁用相机旋转（参考 `ViewportPanel.cpp:42-50` hover 分支）。SpeedTree 用"按住空格"作 modifier，SlowTree 可用同样方式（空格+左键）或进入模式后独占左键。
2. **FBO UV 翻转**：视口 Image 用 `uv0=(0,1) uv1=(1,0)`，屏幕→NDC 时 Y 要反号（`ViewportPanel.cpp:63-64` 已有范例），控制点投影回屏幕时同样注意。
3. **反投影平面选择**：第一版固定"过 target、朝相机"的平面最简单；进阶可让起点吸附到点击命中的枝条表面（用 pickNode 拿命中点）。
4. **重生成开销**：拖控制点时每帧 markDirty 会整树重生成。当前 `generate` 是全量重建，若手绘枝条多可能卡顿 —— 第一版可接受（拖动时只重建预览线，松手才 markDirty）。
5. **序列化格式**：strokes 是变长嵌套（每 stroke 变长点集），仿 `cutoutPoints` 的"先写数量再写坐标"行格式，注意 getF/getS 读取。
6. **imgui-node-editor ID 空间**：新增节点类型不涉及新 ID 段，安全。
7. **NodeType 枚举顺序**：序列化为 int，**必须追加在末尾**（Custom 之后），保持旧工程兼容。

---

## 6. 待你决策的问题

1. **控制点模型**：先做简化版 Catmull-Rom（控制点只有位置，无手柄）？还是一步到位 Bezier（有切线手柄，更接近 SpeedTree 但交互复杂）？——**建议先 Catmull-Rom**。
2. **modifier 键**：进入手绘模式后独占左键，还是全程"空格+左键"画线（可随时旋转相机）？——SpeedTree 是空格 modifier，体验更流畅但实现稍复杂。
3. **绘制平面**：固定朝相机平面（简单）先行，Z 深度靠拖点补？还是一开始就要"起点吸附父枝表面"？
4. **是否复用现有 Branch 的子节点/枝领管线**，让手绘枝条根部贴合父级、末端挂叶？（推荐复用）
5. **MVP 范围**：P0+P1+P2+P4 是否符合你的预期，还是先只做"能画出一根枝"（P0+P1）看效果？
