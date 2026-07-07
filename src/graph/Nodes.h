#pragma once
#include "NodeGraph.h"
#include "NodeTypes.h"
#include <memory>
// imgui 只在 Nodes.cpp 中 include，这里不引入避免运算符冲突

// ---- Trunk ----
class TrunkNode : public TreeNode {
public:
    TrunkParams params;
    TrunkNode();
    NodeType    getType()  const override { return NodeType::Trunk; }
    const char* getLabel() const override { return "Trunk"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<TrunkNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const TrunkNode*>(o)) params = p->params; }
};

// ---- Roots ----
class RootsNode : public TreeNode {
public:
    RootsParams params;
    RootsNode();
    NodeType    getType()  const override { return NodeType::Roots; }
    const char* getLabel() const override { return "Roots"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<RootsNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const RootsNode*>(o)) params = p->params; }
};

// ---- Branch ----
class BranchNode : public TreeNode {
public:
    BranchParams params;
    BranchNode();
    NodeType    getType()  const override { return NodeType::Branch; }
    const char* getLabel() const override { return "Branch"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<BranchNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const BranchNode*>(o)) params = p->params; }
};

// ---- Twig ----
class TwigNode : public TreeNode {
public:
    TwigParams params;
    TwigNode();
    NodeType    getType()  const override { return NodeType::Twig; }
    const char* getLabel() const override { return "Twig"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<TwigNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const TwigNode*>(o)) params = p->params; }
};

// ---- LeafCluster ----
class LeafClusterNode : public TreeNode {
public:
    LeafClusterParams params;
    LeafClusterNode();
    NodeType    getType()  const override { return NodeType::LeafCluster; }
    const char* getLabel() const override { return "Leaf Cluster"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<LeafClusterNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const LeafClusterNode*>(o)) params = p->params; }
};

// ---- Spine ----
class SpineNode : public TreeNode {
public:
    SpineParams params;
    SpineNode();
    NodeType    getType()  const override { return NodeType::Spine; }
    const char* getLabel() const override { return "Spine"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<SpineNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const SpineNode*>(o)) params = p->params; }
};

// ---- Frond ----
class FrondNode : public TreeNode {
public:
    FrondParams params;
    FrondNode();
    NodeType    getType()  const override { return NodeType::Frond; }
    const char* getLabel() const override { return "Frond"; }
    bool        drawProperties() override;
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<FrondNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const FrondNode*>(o)) params = p->params; }
};
