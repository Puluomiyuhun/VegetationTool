#pragma once
#include <string>

class NodeGraph;
struct LightingParams;
struct TreeMeshData;

// 工程文件读写：纯文本、行式格式(.vtree)，不依赖第三方JSON库。
// 每行 "key value..."，value 为该行剩余全部内容(允许含空格，如带空格的贴图路径)。
namespace ProjectIO {
    // lighting 可选：非空时把光照/场景设置一并写入/读出(向后兼容旧文件——旧文件无该块时保持默认)。
    bool save(const NodeGraph& graph, const std::string& path,
              const LightingParams* lighting = nullptr);
    bool load(NodeGraph& graph, const std::string& path,
              LightingParams* lighting = nullptr);
    // 加载内置默认工程(HelloTree)——用于启动/Reset to Default，取代过去简陋的空模板。
    void loadDefaultTemplate(NodeGraph& graph);
    // 把已生成的树网格导出为 Wavefront OBJ(位置/法线/UV/面)。返回是否成功。
    bool exportOBJ(const TreeMeshData& mesh, const std::string& path);
}
