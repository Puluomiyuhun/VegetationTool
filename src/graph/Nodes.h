#pragma once
#include "NodeGraph.h"
#include "NodeTypes.h"
// imgui 只在 Nodes.cpp 中 include，这里不引入避免运算符冲突

// ---- Trunk ----
class TrunkNode : public TreeNode {
public:
    TrunkParams params;
    TrunkNode();
    NodeType    getType()  const override { return NodeType::Trunk; }
    const char* getLabel() const override { return "Trunk"; }
    bool        drawProperties() override;
};

// ---- Roots ----
class RootsNode : public TreeNode {
public:
    RootsParams params;
    RootsNode();
    NodeType    getType()  const override { return NodeType::Roots; }
    const char* getLabel() const override { return "Roots"; }
    bool        drawProperties() override;
};

// ---- Branch ----
class BranchNode : public TreeNode {
public:
    BranchParams params;
    BranchNode();
    NodeType    getType()  const override { return NodeType::Branch; }
    const char* getLabel() const override { return "Branch"; }
    bool        drawProperties() override;
};

// ---- Twig ----
class TwigNode : public TreeNode {
public:
    TwigParams params;
    TwigNode();
    NodeType    getType()  const override { return NodeType::Twig; }
    const char* getLabel() const override { return "Twig"; }
    bool        drawProperties() override;
};

// ---- LeafCluster ----
class LeafClusterNode : public TreeNode {
public:
    LeafClusterParams params;
    LeafClusterNode();
    NodeType    getType()  const override { return NodeType::LeafCluster; }
    const char* getLabel() const override { return "Leaf Cluster"; }
    bool        drawProperties() override;
};
