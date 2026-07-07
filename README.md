# VegetationTool

An open-source, node-based vegetation generation tool inspired by SpeedTree — built with C++17, OpenGL 3.3, and Dear ImGui.

> **Goal**: A free, API-friendly alternative to SpeedTree that works seamlessly with Unreal Engine 5 vegetation pipelines.

![screenshot](docs/screenshot.png)

---

## ✨ Features

### Node Graph Editor
- **7 node types**: Trunk, Roots, Branch, Twig, LeafCluster, and **Spine → Frond** (ferns)
- Drag-and-drop node connections (imgui-node-editor)
- Right-click context menu to add nodes
- One-click **Add Child Node** buttons in the Properties panel
- Node positions persist across parameter edits
- Copy / paste selected nodes; multiple independent plants per project

### Tree Generation
- **Natural growth** via noise perturbation + spiral gnarl + non-linear taper
- **Roots** node: radial buttress roots flaring from the trunk base, welded to the trunk surface
- **Spine + Frond** nodes for ferns: Spine builds a curved guide line, Frond extrudes a continuous curved fern-leaf strip along it (profile-varying width, curl, serrated edges)
- **Joint bulge** (`Joint Count` + `Joint Bulge`) for bamboo-style periodic swelling
- Branches and twigs attach to the *actual curved surface* of their parent — no floating geometry
- Flare-projection "collar" welds child bases smoothly onto the parent surface
- Parallel Transport Frame (PTF) for twist-free cylinder cross-sections
- Golden-angle (137.5°) azimuth distribution for natural branch spacing
- Per-node geometry controls: radius, length, spread angle, gravity, sides, segments, seed

### PBR Rendering (OpenGL 3.3 Core)
- **Branch/trunk shader**: GGX distribution + Smith geometry + Fresnel-Schlick BRDF
- **Leaf shader**: double-sided normals, hemisphere ambient, SSS (subsurface scattering) backlight approximation
- **Hemisphere ambient lighting** (sky color / ground color gradient, Valve-style)
- Per-node **material settings**: Albedo, Roughness, Metallic, AO Strength, SSS Strength
- **PBR texture maps** per node: BaseColor (sRGB), Roughness/Metallic (R/G channels), Normal Map (tangent-space)
- Reinhard tonemapping + gamma 2.2

### Viewport
- Orbit camera (left-drag = rotate, middle-drag = pan, scroll = zoom)
- Wireframe toggle
- MSAA anti-aliasing (Off / 2x / 4x / 8x)
- Shadow-map self-shadowing
- SpeedTree-style gradient sky background
- Real-time regeneration on any parameter change
- **Lighting panel**: light direction/color/intensity, ambient, exposure, sky gradient, shadow controls

### Project Files (`.vtree`)
- Plain-text, line-based format (no third-party JSON dependency)
- Saves the full node graph, per-node parameters, and material/texture paths
- **Lighting & scene settings are saved alongside the graph** (backward-compatible: older files without a lighting block keep defaults)

---

## 🏗 Architecture

```
src/
├── app/            Application loop (GLFW + OpenGL init)
├── generator/
│   ├── CylinderSegment   Bezier & kinked ring generation, PTF transport
│   └── TreeGenerator     Node-graph traversal → MeshBatch output
├── graph/
│   ├── NodeTypes         MaterialParams, per-node parameter structs
│   ├── NodeGraph         Node/Pin/Link management, addChildNode()
│   └── Nodes             Per-type drawProperties() UI
├── renderer/
│   ├── Renderer          MeshBatch upload, draw-call dispatch
│   ├── Mesh              VAO/VBO/EBO wrapper
│   ├── Texture           stb_image loader, GL texture wrapper
│   ├── Shader            GLSL shader wrapper
│   ├── Camera            Orbit camera
│   └── Framebuffer       Off-screen FBO for viewport
├── shaders/
│   ├── branch.vert/frag  PBR + optional texture maps
│   ├── leaf.vert/frag    SSS + optional texture maps
│   └── grid.vert/frag    Reference grid
└── ui/
    ├── UIManager         Dockspace + panel orchestration
    ├── NodeEditorPanel   imgui-node-editor integration
    ├── PropertyPanel     Per-node parameter editing + Add Child
    └── ViewportPanel     FBO display + camera input
```

---

## 🔧 Build

### Prerequisites
- **CMake** ≥ 3.22
- **vcpkg** (clone to `C:/vcpkg` and run `bootstrap-vcpkg.bat`)
- **Visual Studio 2022** (MSVC) — tested on Windows 10/11
- GPU with OpenGL 3.3 support

### Steps

```powershell
# Configure (vcpkg installs glfw3, glad, glm, imgui, stb automatically)
& "C:\Program Files\CMake\bin\cmake.exe" `
    -S E:\VegetationTool `
    -B E:\VegetationTool\build `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
& "C:\Program Files\CMake\bin\cmake.exe" --build E:\VegetationTool\build --config Release

# Run
E:\VegetationTool\build\Release\VegetationTool.exe
```

### Dependencies (auto-installed via vcpkg)
| Package | Version | Purpose |
|---------|---------|---------|
| glfw3 | 3.4 | Window & input |
| glad | 0.1.36 | OpenGL loader |
| glm | 1.0.3 | Math |
| imgui (docking) | 1.92 | UI framework |
| stb | 2024-07 | Image loading |
| imgui-node-editor | bundled in `external/` | Node graph UI |

---

## 🎮 Usage

### Basic Workflow
1. Launch — a default **Trunk → Branch → Twig → LeafCluster** chain is created
2. Click a node to select it and edit its parameters in the **Properties** panel
3. Use **+ Branch / + Roots / + Twig / + Leaf / + Spine / + Frond** buttons to add child nodes with auto-wiring
4. Right-click the canvas to add free-standing nodes; drag from `Out →` to `→ In` to connect
5. Delete selected nodes/links with **Delete** key

### Node Parameters
| Parameter | Description |
|-----------|-------------|
| Noise Amount / Freq | Organic centerline perturbation strength & frequency |
| Gnarl | Spiral twist applied along the length |
| Taper Power | Non-linear radius falloff from base to tip |
| Joint Count / Bulge | Periodic swelling (bamboo nodes) |
| Spread Angle | Branch divergence from parent axis |
| Gravity | Downward droop weight |
| Rotate Offset | Azimuth step between branches (137.5° = golden angle) |
| Region Start / End | Restrict child growth to a span of the parent |
| Seed | Random seed for reproducible variation |

### PBR Texture Maps
In the **Material** section of any node's properties, enter absolute file paths for:
- **BaseColor** — diffuse/albedo texture (loaded as sRGB)
- **Roughness/Metallic** — packed: R channel = roughness, G channel = metallic
- **Normal Map** — tangent-space normal map

Click the **×** button to clear a texture and revert to the uniform color value.

---

## 🗺 Roadmap

- [ ] Mesh export (FBX / OBJ / glTF)
- [ ] LOD generation
- [ ] Skeletal animation support
- [ ] Collision mesh generation
- [ ] Wind simulation shader
- [ ] Unreal Engine 5 plugin / MCP integration
- [ ] Procedural bark / leaf texture generation
- [ ] Branch profile curves (SpeedTree-style curve editor)
- [x] Save / load node graph + lighting/scene settings (`.vtree`)

---

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

## 🙏 Credits

- [Dear ImGui](https://github.com/ocornut/imgui) — immediate-mode GUI
- [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) — node graph editor
- [GLFW](https://www.glfw.org/) — windowing
- [GLM](https://github.com/g-truc/glm) — mathematics
- [stb_image](https://github.com/nothings/stb) — image loading
