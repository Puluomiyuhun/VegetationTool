#pragma once
#include <string>

class NodeGraph;

// 工程文件读写：纯文本、行式格式(.vtree)，不依赖第三方JSON库。
// 每行 "key value..."，value 为该行剩余全部内容(允许含空格，如带空格的贴图路径)。
namespace ProjectIO {
    bool save(const NodeGraph& graph, const std::string& path);
    bool load(NodeGraph& graph, const std::string& path);
}
