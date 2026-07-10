#!/usr/bin/env python3
"""SlowTree (VegetationTool) MCP 服务器。

把编辑器内嵌的本地 HTTP 控制服务(默认 127.0.0.1:8765/rpc)包装成 MCP 工具,
让 Claude 能直接读取/编辑节点图、改参数、导出、并"看"到渲染结果。

用法(在 MCP 客户端配置里):
    command: python
    args:    ["F:/VegetationTool/mcp/slowtree_mcp.py"]
可选环境变量 SLOWTREE_URL 覆盖默认地址。
"""
import base64
import os
import json
from typing import Any, Optional

import httpx
from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.utilities.types import Image

RPC_URL = os.environ.get("SLOWTREE_URL", "http://127.0.0.1:8765/rpc")

mcp = FastMCP("slowtree")


def _rpc(method: str, params: Optional[dict] = None) -> Any:
    """调用编辑器 /rpc, 返回 result; 出错抛异常(含编辑器返回的 error 文本)。"""
    payload = {"method": method, "params": params or {}}
    try:
        r = httpx.post(RPC_URL, content=json.dumps(payload), timeout=30.0)
    except httpx.ConnectError:
        raise RuntimeError(
            f"无法连接编辑器 {RPC_URL}。请确认 VegetationTool.exe 正在运行。"
        )
    r.raise_for_status()
    data = r.json()
    if not data.get("ok"):
        raise RuntimeError(data.get("error", "unknown error"))
    return data.get("result")


NODE_TYPES = ["Trunk", "Roots", "Branch", "Twig", "LeafCluster", "Spine", "Frond", "Export"]


@mcp.tool()
def ping() -> str:
    """检查编辑器是否在线。返回应用信息。"""
    return json.dumps(_rpc("ping"), ensure_ascii=False)


@mcp.tool()
def graph_list() -> str:
    """列出当前节点图: 所有节点(id/类型/标签/坐标)、连线(含 fromNode/toNode)、当前选中节点。
    这是了解场景结构的首选工具。"""
    return json.dumps(_rpc("graph.list"), ensure_ascii=False, indent=2)


@mcp.tool()
def node_add(type: str, x: float = 0.0, y: float = 0.0) -> str:
    """新增一个节点。type 可用名称(Trunk/Roots/Branch/Twig/LeafCluster/Spine/Frond/Export)或序号。
    x,y 为编辑器画布坐标。返回新节点 id。"""
    return json.dumps(_rpc("node.add", {"type": type, "x": x, "y": y}), ensure_ascii=False)


@mcp.tool()
def node_add_child(parentId: int, type: str) -> str:
    """在 parentId 节点下新增子节点并自动连线(parent 输出 -> 新节点输入)。返回新节点 id。
    这是搭建树结构最方便的方式, 例如 Trunk->Branch->Twig->LeafCluster。"""
    return json.dumps(_rpc("node.addChild", {"parentId": parentId, "type": type}), ensure_ascii=False)


@mcp.tool()
def node_remove(id: int) -> str:
    """删除指定节点(及其关联连线)。"""
    return json.dumps(_rpc("node.remove", {"id": id}), ensure_ascii=False)


@mcp.tool()
def link_add(fromNode: int, toNode: int) -> str:
    """连接两个节点: fromNode 的输出 -> toNode 的输入。返回连线 id。"""
    return json.dumps(_rpc("link.add", {"fromNode": fromNode, "toNode": toNode}), ensure_ascii=False)


@mcp.tool()
def link_remove(id: int) -> str:
    """删除指定连线。"""
    return json.dumps(_rpc("link.remove", {"id": id}), ensure_ascii=False)


@mcp.tool()
def node_get_params(id: int) -> str:
    """读取节点的全部参数(键值对)。键名与 .vtree 工程文件一致, 例如 Trunk 的 length/endRadius/gnarl,
    Branch 的 count/lengthRatio/angle 等; 材质在 mat.* 前缀下。先读再改可避免猜错键名。"""
    return json.dumps(_rpc("node.getParams", {"id": id}), ensure_ascii=False, indent=2)


@mcp.tool()
def node_set_params(id: int, params: dict) -> str:
    """修改节点参数。params 为键值对(值可为数字/字符串/布尔), 未提供的键保持不变。
    向量类参数用空格分隔的字符串, 如 {"mat.albedo": "0.3 0.5 0.2"}。改完编辑器会自动重新生成网格。
    建议先用 node_get_params 查看可用键与当前值。"""
    return json.dumps(_rpc("node.setParams", {"id": id, "params": params}), ensure_ascii=False)


@mcp.tool()
def node_select(id: int) -> str:
    """在编辑器中选中某节点(id=0 取消选中)。选中节点会高亮其几何。"""
    return json.dumps(_rpc("node.select", {"id": id}), ensure_ascii=False)


@mcp.tool()
def project_new() -> str:
    """清空当前工程(空白图)。"""
    return json.dumps(_rpc("project.new"), ensure_ascii=False)


@mcp.tool()
def project_build_default() -> str:
    """重置为内置默认模板树(HelloTree)。"""
    return json.dumps(_rpc("project.buildDefault"), ensure_ascii=False)


@mcp.tool()
def project_save(path: str) -> str:
    """把当前工程保存为 .vtree 文件。path 为绝对路径。"""
    return json.dumps(_rpc("project.save", {"path": path}), ensure_ascii=False)


@mcp.tool()
def project_load(path: str) -> str:
    """从 .vtree 文件加载工程。path 为绝对路径。"""
    return json.dumps(_rpc("project.load", {"path": path}), ensure_ascii=False)


@mcp.tool()
def export_trigger(id: Optional[int] = None, path: Optional[str] = None) -> str:
    """触发导出(写 OBJ)。不指定 id 时用第一个 Export 节点; 可用 path 覆盖导出路径。
    导出在下一帧执行, 按 Export 节点的模式(下游标本/整株/上游链)生成。"""
    p: dict = {}
    if id is not None:
        p["id"] = id
    if path is not None:
        p["path"] = path
    return json.dumps(_rpc("export.trigger", p), ensure_ascii=False)


@mcp.tool()
def screenshot() -> Image:
    """抓取当前 3D 视口画面(上一帧渲染结果), 以图片形式返回, 便于"看到"生成效果。
    修改参数后建议截图确认。"""
    res = _rpc("render.screenshot", {"base64": True})
    raw = base64.b64decode(res["base64"])
    return Image(data=raw, format="png")


if __name__ == "__main__":
    mcp.run()
