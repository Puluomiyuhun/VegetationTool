#include "NodeGraph.h"
#include "Nodes.h"
#include <algorithm>
#include <stdexcept>

static std::unique_ptr<TreeNode> makeNode(NodeType type) {
    switch (type) {
        case NodeType::Trunk:       return std::make_unique<TrunkNode>();
        case NodeType::Branch:      return std::make_unique<BranchNode>();
        case NodeType::Twig:        return std::make_unique<TwigNode>();
        case NodeType::LeafCluster: return std::make_unique<LeafClusterNode>();
    }
    return nullptr;
}

NodeId NodeGraph::addNode(NodeType type, glm::vec2 pos) {
    auto node       = makeNode(type);
    NodeId id       = m_nextNodeId++;
    node->id        = id;
    node->editorPos = pos;

    // 分配输出Pin
    node->outputPin.id    = m_nextPinId++;
    node->outputPin.owner = id;
    node->outputPin.input = false;
    m_pinOwner[node->outputPin.id] = id;

    // 所有节点都有1个输入Pin（Trunk也留着，方便扩展；LeafCluster也需要输入）
    if (true) {
        Pin inPin;
        inPin.id    = m_nextPinId++;
        inPin.owner = id;
        inPin.input = true;
        m_pinOwner[inPin.id] = id;
        node->inputPins.push_back(inPin);
    }

    m_nodes[id] = std::move(node);
    m_dirty = true;
    return id;
}

void NodeGraph::removeNode(NodeId id) {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return;

    // 删除关联连线
    auto& node = it->second;
    auto removeLinks = [&](PinId pin) {
        auto lIt = m_pinToLink.find(pin);
        if (lIt != m_pinToLink.end()) {
            LinkId lid = lIt->second;
            m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                [lid](const Link& l){ return l.id == lid; }), m_links.end());
            m_pinToLink.erase(lIt);
        }
        m_pinOwner.erase(pin);
    };
    removeLinks(node->outputPin.id);
    for (auto& p : node->inputPins) removeLinks(p.id);

    m_nodes.erase(it);
    m_dirty = true;
}

TreeNode* NodeGraph::getNode(NodeId id) {
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? it->second.get() : nullptr;
}

const std::unordered_map<NodeId, std::unique_ptr<TreeNode>>& NodeGraph::nodes() const {
    return m_nodes;
}

LinkId NodeGraph::addLink(PinId fromPin, PinId toPin) {
    // 先删除toPin上已有的连线
    auto it = m_pinToLink.find(toPin);
    if (it != m_pinToLink.end()) {
        LinkId old = it->second;
        m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
            [old](const Link& l){ return l.id == old; }), m_links.end());
        m_pinToLink.erase(it);
    }

    LinkId lid   = m_nextLinkId++;
    Link   link  = {lid, fromPin, toPin};
    m_links.push_back(link);
    m_pinToLink[fromPin] = lid;
    m_pinToLink[toPin]   = lid;
    m_dirty = true;
    return lid;
}

void NodeGraph::removeLink(LinkId id) {
    auto it = std::find_if(m_links.begin(), m_links.end(),
        [id](const Link& l){ return l.id == id; });
    if (it == m_links.end()) return;
    m_pinToLink.erase(it->fromPin);
    m_pinToLink.erase(it->toPin);
    m_links.erase(it);
    m_dirty = true;
}

LinkId NodeGraph::linkFromPin(PinId pin) const {
    auto it = m_pinToLink.find(pin);
    return it != m_pinToLink.end() ? it->second : INVALID_LINK;
}

std::vector<TreeNode*> NodeGraph::childrenOf(NodeId id) const {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return {};

    const auto& outPin = it->second->outputPin;
    auto lIt = m_pinToLink.find(outPin.id);
    if (lIt == m_pinToLink.end()) return {};

    std::vector<TreeNode*> result;
    for (const auto& link : m_links) {
        if (link.fromPin == outPin.id) {
            // toPin归哪个节点
            auto ownerIt = m_pinOwner.find(link.toPin);
            if (ownerIt != m_pinOwner.end()) {
                auto nodeIt = m_nodes.find(ownerIt->second);
                if (nodeIt != m_nodes.end())
                    result.push_back(nodeIt->second.get());
            }
        }
    }
    return result;
}

TreeNode* NodeGraph::rootNode() const {
    for (const auto& [id, node] : m_nodes) {
        if (node->type != NodeType::Trunk) continue;
        // 检查输入Pin是否有连线
        bool hasInput = false;
        for (const auto& pin : node->inputPins) {
            if (m_pinToLink.count(pin.id)) { hasInput = true; break; }
        }
        if (!hasInput) return node.get();
    }
    return nullptr;
}

void NodeGraph::markDirty() { m_dirty = true; }

void NodeGraph::buildDefaultTemplate() {    m_nodes.clear();
    m_links.clear();
    m_pinOwner.clear();
    m_pinToLink.clear();
    m_nextNodeId = 1;
    m_nextPinId  = 10000;
    m_nextLinkId = 100000;

    NodeId trunk  = addNode(NodeType::Trunk,       {100.0f, 200.0f});
    NodeId branch = addNode(NodeType::Branch,      {320.0f, 200.0f});
    NodeId twig   = addNode(NodeType::Twig,        {540.0f, 200.0f});
    NodeId leaf   = addNode(NodeType::LeafCluster, {760.0f, 200.0f});

    addLink(m_nodes[trunk]->outputPin.id,  m_nodes[branch]->inputPins[0].id);
    addLink(m_nodes[branch]->outputPin.id, m_nodes[twig]->inputPins[0].id);
    addLink(m_nodes[twig]->outputPin.id,   m_nodes[leaf]->inputPins[0].id);

    m_dirty = true;
}

NodeId NodeGraph::addChildNode(NodeId parentId, NodeType type) {
    auto* parent = getNode(parentId);
    if (!parent) return INVALID_NODE;
    glm::vec2 childPos = parent->editorPos + glm::vec2(220.0f, 0.0f);
    NodeId childId = addNode(type, childPos);
    auto* child = getNode(childId);
    if (child && !child->inputPins.empty())
        addLink(parent->outputPin.id, child->inputPins[0].id);
    return childId;
}
