#include "Renderer.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void Renderer::init() {
    m_branchShader.loadFromFiles("shaders/branch.vert", "shaders/branch.frag");
    m_leafShader.loadFromFiles("shaders/leaf.vert",     "shaders/leaf.frag");
    m_leafInstShader.loadFromFiles("shaders/leaf_inst.vert", "shaders/leaf.frag");
    m_gridShader.loadFromFiles("shaders/grid.vert",     "shaders/grid.frag");
    m_skyShader.loadFromFiles("shaders/sky.vert",       "shaders/sky.frag");
    m_depthShader.loadFromFiles("shaders/depth.vert",   "shaders/depth.frag");
    m_groundShader.loadFromFiles("shaders/ground.vert", "shaders/ground.frag");
    m_outlineShader.loadFromFiles("shaders/outline.vert", "shaders/outline.frag");
    glGenVertexArrays(1, &m_skyVao);
    initShadowMap();
    buildGrid();
    buildGround();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);   // 配合多重采样 FBO 抗锯齿
}

// 创建深度贴图 FBO：仅深度附件，供光源视角写入
void Renderer::initShadowMap() {
    glGenFramebuffers(1, &m_shadowFbo);
    glGenTextures(1, &m_shadowTex);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 m_shadowSize, m_shadowSize, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // 边界外视为“全亮”(深度=1)，避免视锥外区域误判为阴影
    float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, m_shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::shutdown() {
    m_branchShader.destroy();
    m_leafShader.destroy();
    m_leafInstShader.destroy();
    m_gridShader.destroy();
    m_skyShader.destroy();
    m_depthShader.destroy();
    m_groundShader.destroy();
    m_outlineShader.destroy();
    if (m_skyVao) { glDeleteVertexArrays(1, &m_skyVao); m_skyVao = 0; }
    if (m_shadowFbo) { glDeleteFramebuffers(1, &m_shadowFbo); m_shadowFbo = 0; }
    if (m_shadowTex) { glDeleteTextures(1, &m_shadowTex); m_shadowTex = 0; }
    for (auto& b : m_batches)
        b.mesh.destroy();
    m_batches.clear();
    for (auto& ib : m_instBatches)
        ib.destroy();
    m_instBatches.clear();
    for (auto& [k, t] : m_texCache) if (t) t->destroy();
    m_texCache.clear();
    m_gridMesh.destroy();
    m_groundMesh.destroy();
    m_hlMesh.destroy();
}

// 按路径缓存纹理：命中则直接复用，未命中才从磁盘加载一次。
std::shared_ptr<Texture> Renderer::getTexture(const std::string& path, bool sRGB) {
    if (path.empty()) return nullptr;
    std::string key = (sRGB ? "s:" : "l:") + path;
    auto it = m_texCache.find(key);
    if (it != m_texCache.end()) return it->second;
    auto tex = std::make_shared<Texture>();
    if (!tex->loadFromFile(path, sRGB)) tex.reset();
    m_texCache[key] = tex;
    return tex;
}

void Renderer::GpuInstancedBatch::destroy() {
    if (vao)      { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vboProto) { glDeleteBuffers(1, &vboProto); vboProto = 0; }
    if (ebo)      { glDeleteBuffers(1, &ebo); ebo = 0; }
    if (vboInst)  { glDeleteBuffers(1, &vboInst); vboInst = 0; }
    instanceCount = 0;
    subs.clear();
}

void Renderer::uploadTreeMesh(const TreeMeshData& data) {
    // 方案2: 复用已有 GPU buffer。拖参数时 batch 数量/布局通常不变(只有顶点数据变),
    // 逐个复用同索引处的 Mesh(update 只重传数据, 不 glGen/glDelete 对象), 避免每帧对象 churn。
    // 收集本次有效 batch(跳过空顶点)。
    std::vector<const MeshBatch*> valid;
    valid.reserve(data.batches.size());
    for (const auto& batch : data.batches)
        if (!batch.vertices.empty()) valid.push_back(&batch);

    // 多出的旧 batch 释放; 缺的补齐。
    for (size_t i = valid.size(); i < m_batches.size(); ++i)
        m_batches[i].mesh.destroy();
    m_batches.resize(valid.size());

    for (size_t i = 0; i < valid.size(); ++i) {
        const MeshBatch& batch = *valid[i];
        GpuBatch& gb = m_batches[i];
        gb.material = batch.material;
        gb.isLeaf   = batch.isLeaf;

        int wantStride = batch.isLeaf ? 16 : 10;
        if (gb.mesh.valid() && gb.mesh.strideFloats() == wantStride) {
            gb.mesh.update(batch.vertices, batch.indices);   // 复用: 只重传数据
        } else if (batch.isLeaf) {
            // pos(3)+normal(3)+uv(2)+albedo(3)+wind(2)+anchor(3) = 16
            gb.mesh.create(batch.vertices, batch.indices, {3, 3, 2, 3, 2, 3});
        } else {
            // pos(3)+normal(3)+uv(2)+wind(2) = 10
            gb.mesh.create(batch.vertices, batch.indices, {3, 3, 2, 2});
        }

        // 贴图走缓存复用：拖参数重建网格时不再重复解码磁盘 PNG
        gb.texBaseColor = getTexture(batch.material.baseColorTex, true);
        gb.texRoughness = getTexture(batch.material.roughnessTex, false);
        gb.texNormal    = getTexture(batch.material.normalTex,    false);
        gb.texOpacity   = getTexture(batch.material.opacityTex,   false);
    }

    for (auto& ib : m_instBatches)
        ib.destroy();
    m_instBatches.clear();

    // 散布叶实例化批次: 每个 proto 一份原型网格 + 每实例 transform 数组。
    // 原型顶点布局 (pos3 + normal3 + uv2 = 8 float); 实例布局 (pos3 + quat4 + scale3 = 10 float)。
    for (const auto& proto : data.protos) {
        if (proto.pts.empty() || proto.instances.empty()) continue;
        GpuInstancedBatch ib;

        std::vector<float> pv;
        pv.reserve(proto.pts.size() * 8);
        for (size_t i = 0; i < proto.pts.size(); ++i) {
            const glm::vec3& p = proto.pts[i];
            glm::vec3 n = i < proto.nrms.size() ? proto.nrms[i] : glm::vec3(0,0,1);
            glm::vec2 uv = i < proto.uvs.size() ? proto.uvs[i] : glm::vec2(0,0);
            pv.insert(pv.end(), {p.x,p.y,p.z, n.x,n.y,n.z, uv.x,uv.y});
        }
        std::vector<float> iv;
        iv.reserve(proto.instances.size() * 10);
        for (const auto& inst : proto.instances) {
            iv.insert(iv.end(), {inst.pos.x, inst.pos.y, inst.pos.z,
                                 inst.rot.x, inst.rot.y, inst.rot.z, inst.rot.w,
                                 inst.scale.x, inst.scale.y, inst.scale.z});
        }
        ib.instanceCount = (int)proto.instances.size();

        glGenVertexArrays(1, &ib.vao);
        glGenBuffers(1, &ib.vboProto);
        glGenBuffers(1, &ib.ebo);
        glGenBuffers(1, &ib.vboInst);
        glBindVertexArray(ib.vao);

        // 原型顶点 (location 0,1,2)
        glBindBuffer(GL_ARRAY_BUFFER, ib.vboProto);
        glBufferData(GL_ARRAY_BUFFER, pv.size()*sizeof(float), pv.data(), GL_DYNAMIC_DRAW);
        const int ps = 8 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ps, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, ps, (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, ps, (void*)(6*sizeof(float)));

        // 索引
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, proto.idx.size()*sizeof(uint32_t),
                     proto.idx.data(), GL_DYNAMIC_DRAW);

        // 每实例数据 (location 3,4,5; divisor=1)
        glBindBuffer(GL_ARRAY_BUFFER, ib.vboInst);
        glBufferData(GL_ARRAY_BUFFER, iv.size()*sizeof(float), iv.data(), GL_DYNAMIC_DRAW);
        const int is = 10 * sizeof(float);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, is, (void*)0);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, is, (void*)(3*sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, is, (void*)(7*sizeof(float)));
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);

        // 按材质分段: 每段一个 draw call, 共用同一 VAO/实例 VBO。贴图按段各自绑定。
        if (proto.subs.empty()) {
            GpuInstancedBatch::Sub s;
            s.indexOffset = 0;
            s.indexCount  = (int)proto.idx.size();
            s.material    = proto.material;
            s.texBaseColor = getTexture(s.material.baseColorTex, true);
            s.texRoughness = getTexture(s.material.roughnessTex, false);
            s.texNormal    = getTexture(s.material.normalTex,    false);
            s.texOpacity   = getTexture(s.material.opacityTex,   false);
            ib.subs.push_back(std::move(s));
        } else {
            for (const auto& ps : proto.subs) {
                GpuInstancedBatch::Sub s;
                s.indexOffset = (int)ps.idxOffset;
                s.indexCount  = (int)ps.idxCount;
                s.material    = ps.material;
                s.texBaseColor = getTexture(ps.material.baseColorTex, true);
                s.texRoughness = getTexture(ps.material.roughnessTex, false);
                s.texNormal    = getTexture(ps.material.normalTex,    false);
                s.texOpacity   = getTexture(ps.material.opacityTex,   false);
                ib.subs.push_back(std::move(s));
            }
        }

        m_instBatches.push_back(std::move(ib));
    }

    // 高亮描边网格(选中节点自身): pos+normal, 用 outline shader 沿法线外扩勾勒轮廓
    m_hlMesh.destroy();
    m_hlIndexCount = 0;
    if (!data.hlVerts.empty() && !data.hlIdx.empty()) {
        m_hlMesh.create(data.hlVerts, data.hlIdx, {3, 3});
        m_hlIndexCount = (int)data.hlIdx.size();
    }

    // 拾取三角(世界坐标 + 节点归属): 供鼠标射线拾取
    m_pickTris = data.pickTris;

    computeSceneBounds(data);
}

// 计算所有批次顶点的世界空间 AABB，用于把光源正交视锥拟合到场景
void Renderer::computeSceneBounds(const TreeMeshData& data) {
    bool any = false;
    glm::vec3 mn( 1e9f), mx(-1e9f);
    for (const auto& batch : data.batches) {
        int stride = batch.isLeaf ? 16 : 10;  // 每顶点 float 数
        for (size_t i = 0; i + 2 < batch.vertices.size(); i += stride) {
            glm::vec3 p(batch.vertices[i], batch.vertices[i+1], batch.vertices[i+2]);
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
            any = true;
        }
    }
    // 散布叶实例化: 几何不在 batches 里, 用实例位置(近似取锚点)纳入包围盒
    for (const auto& proto : data.protos) {
        for (const auto& inst : proto.instances) {
            mn = glm::min(mn, inst.pos);
            mx = glm::max(mx, inst.pos);
            any = true;
        }
    }
    if (!any) { mn = glm::vec3(-5.0f); mx = glm::vec3(5.0f); }
    // 略微外扩，保证边缘阴影不被视锥裁掉
    glm::vec3 pad = (mx - mn) * 0.05f + glm::vec3(0.5f);
    m_sceneMin = mn - pad;
    m_sceneMax = mx + pad;
}

void Renderer::setLightUniforms(Shader& sh) {
    auto& l = lighting;
    sh.setVec3("uLightDir",   l.lightDir.x,   l.lightDir.y,   l.lightDir.z);
    sh.setVec3("uLightColor", l.lightColor.x, l.lightColor.y, l.lightColor.z);
    sh.setVec3("uAmbientTop", l.ambientTop.x, l.ambientTop.y, l.ambientTop.z);
    sh.setVec3("uAmbientBot", l.ambientBot.x, l.ambientBot.y, l.ambientBot.z);
    sh.setFloat("uLightIntensity",  l.lightIntensity);
    sh.setFloat("uAmbientStrength", l.ambientStrength);
    sh.setFloat("uExposure",        l.exposure);
    // 阴影相关
    sh.setMat4("uLightSpace", glm::value_ptr(m_lightSpace));
    sh.setInt("uShadowMap", 4);
    sh.setInt("uShadowEnabled", l.shadowEnabled ? 1 : 0);
    sh.setFloat("uShadowStrength", l.shadowStrength);
    sh.setFloat("uShadowBias", l.shadowBias);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glActiveTexture(GL_TEXTURE0);
}

// 上传顶点风力 uniform。同一函数供 branch/leaf 两个 shader 使用；
// 目标 shader 若无对应 uniform, glGetUniformLocation 返回 -1, set 为安全空操作。
void Renderer::setWindUniforms(Shader& sh) {
    auto& w = wind;
    float rad = glm::radians(w.dirAngleDeg);
    glm::vec3 dir(std::cos(rad), 0.0f, std::sin(rad));
    sh.setInt("uWindEnabled", w.enabled ? 1 : 0);
    sh.setFloat("uWindTime", windTime);
    sh.setVec3("uWindDir", dir.x, dir.y, dir.z);
    sh.setFloat("uWindStrength",       w.strength);
    sh.setFloat("uWindGlobalStrength", w.globalStrength);
    sh.setFloat("uWindGlobalFreq",     w.globalFreq);
    sh.setFloat("uWindBranchStrength", w.branchStrength);
    sh.setFloat("uWindBranchFreq",     w.branchFreq);
    sh.setFloat("uWindLeafStrength",   w.leafStrength);
    sh.setFloat("uWindLeafFreq",       w.leafFreq);
    // 树高(世界空间), 用于把顶点高度归一化为 hr∈[0,1]
    sh.setFloat("uWindTreeHeight", m_sceneMax.y);
}

// 光源视角把整棵树写入深度贴图。正交视锥拟合场景 AABB。
void Renderer::renderShadowPass() {
    // 以场景包围球心为目标，沿光照方向反向放置光源
    glm::vec3 center = (m_sceneMin + m_sceneMax) * 0.5f;
    float radius = glm::length(m_sceneMax - m_sceneMin) * 0.5f + 0.001f;
    glm::vec3 dir = glm::normalize(lighting.lightDir);   // 指向光源方向
    glm::vec3 eye = center + dir * (radius * 2.0f);
    glm::vec3 up  = (std::abs(dir.y) > 0.99f) ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    glm::mat4 lightView = glm::lookAt(eye, center, up);
    glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius,
                                     0.01f, radius * 4.0f);
    m_lightSpace = lightProj * lightView;

    glViewport(0, 0, m_shadowSize, m_shadowSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glClear(GL_DEPTH_BUFFER_BIT);

    // 正面剔除减少 peter-panning；但叶片双面故对叶片关闭
    glm::mat4 model = glm::mat4(1.0f);
    m_depthShader.use();
    m_depthShader.setMat4("uLightSpace", glm::value_ptr(m_lightSpace));
    m_depthShader.setMat4("uModel", glm::value_ptr(model));
    m_depthShader.setInt("uTexOpacity", 3);
    m_depthShader.setInt("uTexBaseColor", 0);

    for (auto& gb : m_batches) {
        if (!gb.mesh.valid()) continue;
        m_depthShader.setInt("uIsLeaf", gb.isLeaf ? 1 : 0);
        m_depthShader.setFloat("uAlphaCutoff", gb.material.alphaCutoff);
        bool hasOp = gb.texOpacity && gb.texOpacity->valid();
        bool hasBc = gb.texBaseColor && gb.texBaseColor->valid();
        m_depthShader.setInt("uHasOpacity", hasOp ? 1 : 0);
        m_depthShader.setInt("uHasBaseColor", hasBc ? 1 : 0);
        if (hasOp) gb.texOpacity->bind(3);
        if (hasBc) gb.texBaseColor->bind(0);
        if (gb.isLeaf) glDisable(GL_CULL_FACE);
        else           glCullFace(GL_FRONT);
        gb.mesh.draw();
        if (gb.isLeaf) glEnable(GL_CULL_FACE);
        else           glCullFace(GL_BACK);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::bindBatchTextures(Shader& sh, GpuBatch& gb) {
    bool hasBc = gb.texBaseColor && gb.texBaseColor->valid();
    bool hasRo = gb.texRoughness && gb.texRoughness->valid();
    bool hasNo = gb.texNormal    && gb.texNormal->valid();
    bool hasOp = gb.texOpacity   && gb.texOpacity->valid();

    sh.setInt("uTexBaseColor", 0);
    sh.setInt("uHasBaseColor", hasBc ? 1 : 0);
    if (hasBc) gb.texBaseColor->bind(0);

    sh.setInt("uTexRoughness", 1);
    sh.setInt("uHasRoughness", hasRo ? 1 : 0);
    if (hasRo) gb.texRoughness->bind(1);

    sh.setInt("uTexNormal",    2);
    sh.setInt("uHasNormal",    hasNo ? 1 : 0);
    if (hasNo) gb.texNormal->bind(2);

    sh.setInt("uTexOpacity",   3);
    sh.setInt("uHasOpacity",   hasOp ? 1 : 0);
    if (hasOp) gb.texOpacity->bind(3);
}

void Renderer::render(const OrbitCamera& camera, float aspect, bool wireframe) {
    // 记录当前绑定的 FBO 与 viewport（ViewportPanel 已绑定离屏 FBO），
    // 阴影 pass 会切换 FBO/viewport，结束后需恢复。
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevVp[4];
    glGetIntegerv(GL_VIEWPORT, prevVp);

    // 1) 阴影深度 pass（从光源视角渲染到深度贴图）
    if (lighting.shadowEnabled && !m_batches.empty()) {
        renderShadowPass();
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    }

    // 2) 主 pass
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 先画渐变天空背景(覆盖整个视口，替代纯色 clear)
    renderSky(camera, aspect);

    glm::mat4 view  = camera.viewMatrix();
    glm::mat4 proj  = camera.projectionMatrix(aspect);
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 vp    = proj * view;

    renderGrid(vp);

    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    for (auto& gb : m_batches) {
        if (!gb.mesh.valid()) continue;

        if (gb.isLeaf) {
            glDisable(GL_CULL_FACE);
            m_leafShader.use();
            m_leafShader.setMat4("uModel",      glm::value_ptr(model));
            m_leafShader.setMat4("uView",       glm::value_ptr(view));
            m_leafShader.setMat4("uProjection", glm::value_ptr(proj));
            m_leafShader.setFloat("uRoughness",   gb.material.roughness);
            m_leafShader.setFloat("uAoStrength",  gb.material.aoStrength);
            m_leafShader.setFloat("uSssStrength", gb.material.sssStrength);
            m_leafShader.setFloat("uAlphaCutoff", gb.material.alphaCutoff);
            setLightUniforms(m_leafShader);
            setWindUniforms(m_leafShader);
            bindBatchTextures(m_leafShader, gb);
            gb.mesh.draw();
            glEnable(GL_CULL_FACE);
        } else {
            m_branchShader.use();
            m_branchShader.setMat4("uModel",      glm::value_ptr(model));
            m_branchShader.setMat4("uView",       glm::value_ptr(view));
            m_branchShader.setMat4("uProjection", glm::value_ptr(proj));
            m_branchShader.setVec3("uAlbedo",     gb.material.albedo.x, gb.material.albedo.y, gb.material.albedo.z);
            m_branchShader.setFloat("uRoughness", gb.material.roughness);
            m_branchShader.setFloat("uMetallic",  gb.material.metallic);
            m_branchShader.setFloat("uAoStrength",gb.material.aoStrength);
            setLightUniforms(m_branchShader);
            setWindUniforms(m_branchShader);
            bindBatchTextures(m_branchShader, gb);
            gb.mesh.draw();
        }
    }

    // 散布叶实例化绘制: 一份原型 + 每实例 transform, glDrawElementsInstanced 一次画完。
    if (!m_instBatches.empty()) {
        glDisable(GL_CULL_FACE);
        m_leafInstShader.use();
        m_leafInstShader.setMat4("uModel",      glm::value_ptr(model));
        m_leafInstShader.setMat4("uView",       glm::value_ptr(view));
        m_leafInstShader.setMat4("uProjection", glm::value_ptr(proj));
        setLightUniforms(m_leafInstShader);
        setWindUniforms(m_leafInstShader);
        for (auto& ib : m_instBatches) {
            if (!ib.vao || ib.instanceCount == 0) continue;
            glBindVertexArray(ib.vao);
            for (auto& s : ib.subs) {
                if (s.indexCount == 0) continue;
                m_leafInstShader.setVec3("uAlbedo", s.material.albedo.x,
                                         s.material.albedo.y, s.material.albedo.z);
                m_leafInstShader.setFloat("uRoughness",   s.material.roughness);
                m_leafInstShader.setFloat("uAoStrength",  s.material.aoStrength);
                m_leafInstShader.setFloat("uSssStrength", s.material.sssStrength);
                m_leafInstShader.setFloat("uAlphaCutoff", s.material.alphaCutoff);
                // 贴图绑定(与 bindBatchTextures 同约定: unit 0/1/2/3)
                bool hasBc = s.texBaseColor && s.texBaseColor->valid();
                bool hasRo = s.texRoughness && s.texRoughness->valid();
                bool hasNo = s.texNormal    && s.texNormal->valid();
                bool hasOp = s.texOpacity   && s.texOpacity->valid();
                m_leafInstShader.setInt("uTexBaseColor", 0);
                m_leafInstShader.setInt("uHasBaseColor", hasBc ? 1 : 0);
                if (hasBc) s.texBaseColor->bind(0);
                m_leafInstShader.setInt("uTexRoughness", 1);
                m_leafInstShader.setInt("uHasRoughness", hasRo ? 1 : 0);
                if (hasRo) s.texRoughness->bind(1);
                m_leafInstShader.setInt("uTexNormal", 2);
                m_leafInstShader.setInt("uHasNormal", hasNo ? 1 : 0);
                if (hasNo) s.texNormal->bind(2);
                m_leafInstShader.setInt("uTexOpacity", 3);
                m_leafInstShader.setInt("uHasOpacity", hasOp ? 1 : 0);
                if (hasOp) s.texOpacity->bind(3);

                glDrawElementsInstanced(GL_TRIANGLES, s.indexCount, GL_UNSIGNED_INT,
                    (void*)(intptr_t)(s.indexOffset * sizeof(uint32_t)), ib.instanceCount);
            }
        }
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // 地面在植被之后绘制: 半透明混合, 让埋在地下(被地面遮挡)的植被透出来
    if (lighting.groundEnabled)
        renderGround(vp, camera.position());

    // 选中节点高亮线框(黄色叠加): 关闭深度测试, 让高亮始终浮在模型之上
    renderHighlight(vp);
}

// 选中节点自身的轮廓描边(类 SpeedTree 选中效果)。用模板缓冲勾勒外轮廓而非线框:
// Pass A 把实心网格写入模板=1(不写颜色); Pass B 沿法线外扩一圈只在模板≠1 处上色,
// 于是只剩一圈外描边, 不显示内部拓扑。关闭深度测试, 让描边浮于模型之上。
void Renderer::renderHighlight(const glm::mat4& vp) {
    if (!m_hlMesh.valid() || m_hlIndexCount == 0) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);
    m_outlineShader.use();
    m_outlineShader.setMat4("uViewProjection", glm::value_ptr(vp));

    // Pass A: 实心网格 → 模板=1, 不写颜色
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    m_outlineShader.setFloat("uOutlineWidth", 0.0f);
    m_outlineShader.setVec3("uColor", 1.0f, 0.9f, 0.05f);
    m_hlMesh.draw();

    // Pass B: 沿法线外扩 → 只在模板≠1(即轮廓外圈)处上色
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);
    // 外扩宽度按场景尺度取一个小比例, 保证不同大小的树描边粗细相近
    float scale = glm::length(m_sceneMax - m_sceneMin);
    m_outlineShader.setFloat("uOutlineWidth", scale * 0.0016f + 0.004f);
    m_outlineShader.setVec3("uColor", 1.0f, 0.9f, 0.05f);
    m_hlMesh.draw();

    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

// 鼠标射线拾取: 把 NDC 反投影成世界射线, 与所有拾取三角求最近交点, 返回其节点 id。
uint32_t Renderer::pickNode(const OrbitCamera& camera, float aspect,
                            float ndcX, float ndcY) const {
    if (m_pickTris.empty()) return 0;
    glm::mat4 invVP = glm::inverse(camera.projectionMatrix(aspect) * camera.viewMatrix());
    glm::vec4 pNear = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    glm::vec3 ro = glm::vec3(pNear) / pNear.w;
    glm::vec3 rd = glm::normalize(glm::vec3(pFar) / pFar.w - ro);

    // Möller–Trumbore 射线-三角相交, 取最近命中
    float best = 1e30f;
    uint32_t hit = 0;
    const float EPS = 1e-7f;
    for (const auto& t : m_pickTris) {
        glm::vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        glm::vec3 pv = glm::cross(rd, e2);
        float det = glm::dot(e1, pv);
        if (std::abs(det) < EPS) continue;   // 平行(双面: 不剔除背面)
        float inv = 1.0f / det;
        glm::vec3 tv = ro - t.a;
        float u = glm::dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        glm::vec3 qv = glm::cross(tv, e1);
        float v = glm::dot(rd, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        float dist = glm::dot(e2, qv) * inv;
        if (dist > 1e-4f && dist < best) { best = dist; hit = t.node; }
    }
    return hit;
}

void Renderer::buildGrid() {
    std::vector<float>    verts;
    std::vector<uint32_t> idx;
    int N = 20; float step = 1.0f, half = N * step;
    for (int i = -N; i <= N; ++i) {
        float f = i * step;
        verts.insert(verts.end(), {-half,0,f, half,0,f, f,0,-half, f,0,half});
    }
    for (uint32_t i = 0; i < (uint32_t)(verts.size()/3); ++i) idx.push_back(i);
    m_gridMesh.create(verts, idx, {3});
}

void Renderer::renderGrid(const glm::mat4& vp) {
    m_gridShader.use();
    m_gridShader.setMat4("uViewProjection", glm::value_ptr(vp));
    m_gridShader.setVec3("uColor", 0.45f, 0.45f, 0.47f);
    m_gridMesh.draw(GL_LINES);
}

// 地面平面：一个大四边形，位于 y=0，法线朝上，UV 用于潜在纹理(此处仅用纯色)。
// 顶点格式 pos(3)+normal(3) = 6 floats。
void Renderer::buildGround() {
    float S = 100.0f;
    float y = -0.004f;   // 略低于 y=0, 避免与参考网格线 z-fighting
    std::vector<float> verts = {
        -S, y, -S,  0,1,0,
         S, y, -S,  0,1,0,
         S, y,  S,  0,1,0,
        -S, y,  S,  0,1,0,
    };
    std::vector<uint32_t> idx = {0,2,1, 0,3,2};
    m_groundMesh.create(verts, idx, {3, 3});
}

// 地面用与天空相同的渐变公式着色(按视线方向), 与远端天空无缝融合, 看不出面片;
// 仅额外接收阴影压暗。半透明混合让被遮挡的植被透出。植被绘制之后调用。
void Renderer::renderGround(const glm::mat4& vp, const glm::vec3& camPos) {
    glm::mat4 model = glm::mat4(1.0f);
    m_groundShader.use();
    m_groundShader.setMat4("uViewProjection", glm::value_ptr(vp));
    m_groundShader.setMat4("uModel", glm::value_ptr(model));
    auto& l = lighting;
    m_groundShader.setVec3("uCamPos", camPos.x, camPos.y, camPos.z);
    m_groundShader.setVec3("uSkyTop",     l.skyTop.x,     l.skyTop.y,     l.skyTop.z);
    m_groundShader.setVec3("uSkyHorizon", l.skyHorizon.x, l.skyHorizon.y, l.skyHorizon.z);
    m_groundShader.setVec3("uSkyGround",  l.skyGround.x,  l.skyGround.y,  l.skyGround.z);
    m_groundShader.setVec3("uLightDir", l.lightDir.x, l.lightDir.y, l.lightDir.z);
    m_groundShader.setFloat("uAlpha", l.groundAlpha);
    // 阴影
    m_groundShader.setMat4("uLightSpace", glm::value_ptr(m_lightSpace));
    m_groundShader.setInt("uShadowMap", 4);
    m_groundShader.setInt("uShadowEnabled", l.shadowEnabled ? 1 : 0);
    m_groundShader.setFloat("uGroundShadowStrength", l.groundShadowStrength);
    m_groundShader.setFloat("uShadowBias", l.shadowBias);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glActiveTexture(GL_TEXTURE0);

    // 半透明混合: 保留深度测试(地面挡在地下植被之前才混合), 但不写深度
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    m_groundMesh.draw();
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// 渐变天空：全屏三角形，按视线仰角在天顶/地平线/地面色间插值
void Renderer::renderSky(const OrbitCamera& camera, float aspect) {
    glm::mat4 view = camera.viewMatrix();
    glm::mat4 proj = camera.projectionMatrix(aspect);
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec3 camPos = camera.position();
    auto& l = lighting;

    // 天空写在最远处，不写深度，之后的几何体正常覆盖
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_skyShader.use();
    m_skyShader.setMat4("uInvViewProj", glm::value_ptr(invVP));
    m_skyShader.setVec3("uCamPos",    camPos.x, camPos.y, camPos.z);
    m_skyShader.setVec3("uSkyTop",     l.skyTop.x,     l.skyTop.y,     l.skyTop.z);
    m_skyShader.setVec3("uSkyHorizon", l.skyHorizon.x, l.skyHorizon.y, l.skyHorizon.z);
    m_skyShader.setVec3("uGround",     l.skyGround.x,  l.skyGround.y,  l.skyGround.z);
    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
