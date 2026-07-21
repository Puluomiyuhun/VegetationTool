#pragma once
#include <string>
#include <map>

class NodeGraph;
class TreeNode;
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
    // 把已生成的树网格导出为 Wavefront OBJ(位置/法线/UV/面) + 同名 .mtl 材质(含贴图)。返回是否成功。
    bool exportOBJ(const TreeMeshData& mesh, const std::string& path);
    // 把已生成的树网格导出为 FBX(ASCII 7.4): 几何 + 材质 + 贴图引用。返回是否成功。
    bool exportFBX(const TreeMeshData& mesh, const std::string& path);
    // 把已生成的树网格导出为 USD(.usda) Nanite Assembly: 枝干合并为 base Mesh,
    // 叶片(四边形)提取为 PointInstancer 原型+实例, 供 UE 实例化省内存导入。返回是否成功。
    bool exportUSD(const TreeMeshData& mesh, const std::string& path);

    // ---- 单节点参数 kv 接口(供 API/脚本读写节点参数, 复用 .vtree 的字段命名与解析) ----
    // 把单个节点的全部参数导出为 key->value 字符串表(不含 NODE 头/ENDNODE)。
    std::map<std::string, std::string> nodeParamsToMap(const TreeNode* n);
    // 把 key->value(部分或全部)应用到节点参数; 未提供的键保持原值。
    void applyNodeParams(TreeNode* n, const std::map<std::string, std::string>& kv);
}

