#include "Renderer.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Renderer::init() {
    m_branchShader.loadFromFiles("shaders/branch.vert", "shaders/branch.frag");
    m_leafShader.loadFromFiles("shaders/leaf.vert",     "shaders/leaf.frag");
    m_gridShader.loadFromFiles("shaders/grid.vert",     "shaders/grid.frag");
    buildGrid();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Renderer::shutdown() {
    m_branchShader.destroy();
    m_leafShader.destroy();
    m_gridShader.destroy();
    for (auto& b : m_batches) {
        b.mesh.destroy();
        b.texBaseColor.destroy();
        b.texRoughness.destroy();
        b.texNormal.destroy();
        b.texOpacity.destroy();
    }
    m_batches.clear();
    m_gridMesh.destroy();
}

void Renderer::uploadTreeMesh(const TreeMeshData& data) {
    for (auto& b : m_batches) {
        b.mesh.destroy();
        b.texBaseColor.destroy();
        b.texRoughness.destroy();
        b.texNormal.destroy();
        b.texOpacity.destroy();
    }
    m_batches.clear();

    for (const auto& batch : data.batches) {
        if (batch.vertices.empty()) continue;
        GpuBatch gb;
        gb.material = batch.material;
        gb.isLeaf   = batch.isLeaf;

        if (batch.isLeaf)
            // pos(3)+normal(3)+uv(2)+albedo(3) = 11
            gb.mesh.create(batch.vertices, batch.indices, {3, 3, 2, 3});
        else
            // pos(3)+normal(3)+uv(2) = 8
            gb.mesh.create(batch.vertices, batch.indices, {3, 3, 2});

        if (!batch.material.baseColorTex.empty())
            gb.texBaseColor.loadFromFile(batch.material.baseColorTex, true);
        if (!batch.material.roughnessTex.empty())
            gb.texRoughness.loadFromFile(batch.material.roughnessTex, false);
        if (!batch.material.normalTex.empty())
            gb.texNormal.loadFromFile(batch.material.normalTex, false);
        if (!batch.material.opacityTex.empty())
            gb.texOpacity.loadFromFile(batch.material.opacityTex, false);

        m_batches.push_back(std::move(gb));
    }
}

void Renderer::setLightUniforms(Shader& sh) {
    auto& l = lighting;
    sh.setVec3("uLightDir",   l.lightDir.x,   l.lightDir.y,   l.lightDir.z);
    sh.setVec3("uLightColor", l.lightColor.x, l.lightColor.y, l.lightColor.z);
    sh.setVec3("uAmbientTop", l.ambientTop.x, l.ambientTop.y, l.ambientTop.z);
    sh.setVec3("uAmbientBot", l.ambientBot.x, l.ambientBot.y, l.ambientBot.z);
}

void Renderer::bindBatchTextures(Shader& sh, GpuBatch& gb) {
    sh.setInt("uTexBaseColor", 0);
    sh.setInt("uHasBaseColor", gb.texBaseColor.valid() ? 1 : 0);
    if (gb.texBaseColor.valid()) gb.texBaseColor.bind(0);

    sh.setInt("uTexRoughness", 1);
    sh.setInt("uHasRoughness", gb.texRoughness.valid() ? 1 : 0);
    if (gb.texRoughness.valid()) gb.texRoughness.bind(1);

    sh.setInt("uTexNormal",    2);
    sh.setInt("uHasNormal",    gb.texNormal.valid() ? 1 : 0);
    if (gb.texNormal.valid()) gb.texNormal.bind(2);

    sh.setInt("uTexOpacity",   3);
    sh.setInt("uHasOpacity",   gb.texOpacity.valid() ? 1 : 0);
    if (gb.texOpacity.valid()) gb.texOpacity.bind(3);
}

void Renderer::render(const OrbitCamera& camera, float aspect, bool wireframe) {
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
            bindBatchTextures(m_branchShader, gb);
            gb.mesh.draw();
        }
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
    m_gridShader.setVec3("uColor", 0.25f, 0.25f, 0.28f);
    m_gridMesh.draw(GL_LINES);
}
