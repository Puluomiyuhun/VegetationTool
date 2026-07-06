# SlowTree Python API + MCP 设计与可行性研究

> 状态：**设计草案（未落地实现）**。本文评估把 SlowTree 的树木生成能力封装成
> Python 模块并对接 MCP 的可行性，给出具体架构与落地步骤。实际添加 pybind11
> 依赖 / 改动 CMake 目标建议在确认后进行（会引入新 vcpkg 依赖）。

## 1. 结论（TL;DR）

**完全可行。** SlowTree 的网格生成链路（NodeGraph → TreeGenerator → TreeMeshData）
本身**不含任何 OpenGL 调用**，是纯 CPU 数据处理，天然适合做成无界面(headless)库并绑定到
Python。唯一的耦合点有两处，都很容易解耦：

1. `TreeMeshData` / `MeshBatch` 结构体定义在 `renderer/Renderer.h`（该头顺带包含 GL 头）。
   → 把这两个纯数据结构挪到一个无 GL 依赖的新头 `graph/MeshData.h`。
2. `graph/Nodes.cpp` 的 `drawProperties()` 用了 ImGui。
   → 把 `drawProperties()` 的实现挪到 `ui/NodesUI.cpp`（只有 GUI app 编译它），
   `Nodes.cpp` 只留构造函数 / `clone` / `copyParamsFrom` 等纯逻辑。

解耦后即可编译出一个不依赖 GL/ImGui/GLFW 的静态核心库 `slowtree_core`，
供桌面 app 和 Python 扩展共用。

## 2. 目标架构

```
                    +------------------------+
                    |     slowtree_core      |  静态库(无 GL/ImGui)
                    |  graph/  generator/    |
                    |  io/     MeshData.h    |
                    +-----------+------------+
                                |
              +-----------------+------------------+
              |                                    |
   +----------v-----------+            +-----------v-----------+
   |  VegetationTool.exe  |            |  slowtree (.pyd)      |
   |  renderer/ ui/ app/  |            |  pybind11 绑定         |
   |  (GL + ImGui + GLFW) |            +-----------+-----------+
   +----------------------+                        |
                                        +----------v-----------+
                                        |  mcp_server.py        |
                                        |  FastMCP 工具封装      |
                                        +-----------------------+
```

## 3. Python API 设计（pybind11）

模块名 `slowtree`。核心类型直接映射现有 C++ 概念：

```python
import slowtree as st

g = st.Graph()
trunk  = g.add_node(st.NodeType.Trunk)
branch = g.add_node(st.NodeType.Branch)
leaf   = g.add_node(st.NodeType.LeafCluster)
g.link(trunk, branch)          # 父 -> 子（内部转成 output/input pin 连线）
g.link(branch, leaf)

# 参数以 dict 读写，键名与 UI/工程文件一致
g.set_params(trunk,  {"length": 8.0, "jointCount": 12, "jointBulge": 0.4,
                      "noiseAmount": 3.0, "gnarl": 2.0, "taperPow": 1.05})  # 竹子
g.set_params(leaf,   {"planar": True, "leafAspect": 0.18, "sizeFalloff": 0.6})  # 蕨叶
params = g.get_params(trunk)   # -> dict

# 生成网格
mesh = g.generate()            # -> TreeMesh
for b in mesh.batches:
    b.is_leaf                  # bool
    b.material                 # dict(albedo, roughness, ... , baseColorTex, ...)
    b.vertices                 # numpy float32, 形状 (N,8) 枝干 / (N,11) 叶片
    b.indices                  # numpy uint32, 形状 (M,3)

# I/O 与导出
g.save("tree.vegtool")         # 复用现有 ProjectIO 文本格式
g.load("tree.vegtool")
mesh.export_obj("tree.obj")    # 新增：把所有 batch 写成 OBJ（需新增 ~40 行）
```

### 绑定要点
- `set_params/get_params` 用 `pybind11::dict` ↔ 各 `*Params` struct 字段做映射。
  可复用 `ProjectIO.cpp` 里已有的 key 名与 `applyParams` 逻辑（抽成公共
  `paramsToKV / kvToParams` 便于两处共享）。
- 顶点/索引用 `py::array_t`（numpy）零拷贝或一次拷贝返回，便于 Python 侧直接喂给
  trimesh / Open3D / Blender。
- 顶点布局已固定：枝干 8 floats(pos3+normal3+uv2)，叶片 11 floats(+albedo3)。

### CMake 增量（示意，OFF 默认，不影响现有 exe）
```cmake
option(SLOWTREE_PYTHON "Build Python bindings" OFF)
add_library(slowtree_core STATIC ${CORE_SOURCES})   # graph/ generator/ io/
target_include_directories(slowtree_core PUBLIC src/)
target_link_libraries(slowtree_core PUBLIC glm::glm)

if(SLOWTREE_PYTHON)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(slowtree python/bindings.cpp)
    target_link_libraries(slowtree PRIVATE slowtree_core)
endif()
```
vcpkg.json 增加 `"pybind11"`（仅在开启该 option 时需要）。
桌面 exe 改为 `target_link_libraries(VegetationTool PRIVATE slowtree_core ...)`，
`file(GLOB)` 改为 core 源码归 core、renderer/ui/app 归 exe。

## 4. MCP 服务器设计

用 Python 侧的 `mcp`（FastMCP）包，`import slowtree` 后把能力暴露成工具，
让 AI Agent 能"用自然语言造树"。参考实现 `python/mcp_server.py`：

```python
from mcp.server.fastmcp import FastMCP
import slowtree as st

mcp = FastMCP("slowtree")
_graphs = {}   # session_id -> st.Graph

@mcp.tool()
def new_tree(species: str = "generic") -> dict:
    """新建一株植物, 可选模板: generic/bamboo/fern。返回节点 id 表。"""

@mcp.tool()
def list_node_types() -> list[str]: ...

@mcp.tool()
def set_param(node_id: int, key: str, value: float) -> dict: ...

@mcp.tool()
def add_node(type: str, parent_id: int | None = None) -> int: ...

@mcp.tool()
def generate_obj(path: str) -> dict:
    """生成并导出 OBJ, 返回顶点/三角形/批次统计供 Agent 反馈。"""

@mcp.tool()
def describe_tree() -> dict:
    """返回当前图的节点树 + 各节点关键参数, 供 Agent 了解现状。"""
```

- 传输：stdio（Claude Desktop / Claude Code 直接挂载）。
- 无需 GL：MCP 进程只做参数编辑 + 生成 + 导出网格文件/统计，纯 headless。
- 预置模板（bamboo/fern/generic）把本次新增的竹节 & 平面羽叶参数固化成可一键
  生成的起点，Agent 只需微调。

## 5. 落地步骤（建议顺序）

1. **解耦（无新依赖，可独立验证）**
   - 新建 `src/graph/MeshData.h`，移入 `MeshBatch`/`TreeMeshData`；`Renderer.h`、
     `TreeGenerator.h` 改为包含它。
   - 移动 `Nodes.cpp` 的 `drawProperties()`/`drawMaterial()` 到 `src/ui/NodesUI.cpp`。
   - 编译桌面 exe 验证功能不变（此步不动行为）。
2. **拆 core 库**：CMake 引入 `slowtree_core`，exe 链接它。再次验证。
3. **OBJ 导出**：core 里加 `writeObj(const TreeMeshData&, path)`（顶点去 normal/uv
   可选，先导 v/vt/vn/f）。
4. **pybind11 绑定**：`python/bindings.cpp` + `SLOWTREE_PYTHON` option。
5. **MCP 服务器**：`python/mcp_server.py` + `requirements.txt`(mcp, numpy)。
6. 文档：README 增加 Python/MCP 用法段落。

## 6. 风险 / 注意
- 引入 pybind11 是新 vcpkg 依赖，会拉长 configure/build；用 option 隔离，默认 OFF。
- `std::mt19937` 决定生成结果确定性——同参数同 seed 结果可复现，利于 MCP 端幂等。
- 现网格是"实心圆柱 + 面片叶"，导出 OBJ 三角面数随 sides/lengthSegs/leafCount 增长，
  Agent 大批量生成时应给出面数上限提示。
