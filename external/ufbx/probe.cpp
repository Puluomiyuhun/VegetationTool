// One-shot FBX structure probe: dump node tree, mesh grouping, bones.
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#define UFBX_NO_ASSERT
#include "ufbx.h"

static void indent(int d){ for(int i=0;i<d;i++) printf("  "); }

static void walk(ufbx_node* n, int depth){
    indent(depth);
    const char* nm = n->name.length ? n->name.data : "(unnamed)";
    printf("%s", nm);
    if (n->bone)  printf("  [BONE]");
    if (n->mesh){
        ufbx_mesh* m = n->mesh;
        size_t tris = 0;
        for (size_t f=0; f<m->faces.count; ++f) tris += (m->faces.data[f].num_indices>=3)? (m->faces.data[f].num_indices-2):0;
        printf("  [MESH v=%zu tri=%zu", m->num_vertices, tris);
        if (m->skin_deformers.count) printf(" skin=%zu", (size_t)m->skin_deformers.count);
        if (m->materials.count){
            ufbx_material* mat = m->materials.data[0];
            printf(" mat='%.*s'", (int)mat->name.length, mat->name.data);
        }
        printf("]");
    }
    printf("\n");
    for (size_t c=0;c<n->children.count;++c) walk(n->children.data[c], depth+1);
}

// bone-only children
static std::vector<ufbx_node*> boneKids(ufbx_node* n){
    std::vector<ufbx_node*> k;
    for (size_t c=0;c<n->children.count;++c) if(n->children.data[c]->bone) k.push_back(n->children.data[c]);
    return k;
}
// subtree max bone-depth (memoized)
static int subDepth(ufbx_node* n, std::unordered_map<ufbx_node*,int>& memo){
    auto it=memo.find(n); if(it!=memo.end()) return it->second;
    int best=0; for(ufbx_node* c: boneKids(n)){ int d=1+subDepth(c,memo); if(d>best)best=d; }
    memo[n]=best; return best;
}
// assign branch order: longest-subtree child continues (same order), others +1
static void assignOrder(ufbx_node* n, int order,
                        std::unordered_map<ufbx_node*,int>& memo,
                        std::unordered_map<ufbx_node*,int>& outOrder){
    outOrder[n]=order;
    auto kids=boneKids(n); if(kids.empty()) return;
    int contIdx=0, best=-1;
    for(size_t i=0;i<kids.size();++i){ int d=subDepth(kids[i],memo); if(d>best){best=d;contIdx=(int)i;} }
    for(size_t i=0;i<kids.size();++i)
        assignOrder(kids[i], order + (int)(i!=(size_t)contIdx), memo, outOrder);
}

int main(int argc, char** argv){
    if (argc < 2){ printf("usage: probe file.fbx\n"); return 1; }
    ufbx_load_opts opts; memset(&opts, 0, sizeof(opts));
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    ufbx_error err;
    ufbx_scene* s = ufbx_load_file(argv[1], &opts, &err);
    if (!s){ printf("load fail: %s\n", err.description.data); return 2; }
    printf("=== SUMMARY meshes=%zu bones? nodes=%zu ===\n", (size_t)s->meshes.count, (size_t)s->nodes.count);
    size_t boneCount=0; for(size_t i=0;i<s->nodes.count;++i) if(s->nodes.data[i]->bone) boneCount++;
    printf("bone_nodes=%zu\n", boneCount);
    printf("=== MESH LIST (grouping) ===\n");
    for (size_t i=0;i<s->meshes.count;++i){
        ufbx_mesh* m = s->meshes.data[i];
        const char* inm = (m->instances.count && m->instances.data[0]->name.length)? m->instances.data[0]->name.data : "(no-inst)";
        printf("mesh[%zu] node='%s' v=%zu faces=%zu skin=%zu materials=%zu\n",
            i, inm, m->num_vertices, (size_t)m->faces.count, (size_t)m->skin_deformers.count, (size_t)m->materials.count);
    }

    // === bone depth stats ===
    printf("=== BONE STATS ===\n");
    size_t maxDepth=0, terminal=0;
    size_t depthHist[32]; for(int i=0;i<32;i++) depthHist[i]=0;
    for (size_t i=0;i<s->nodes.count;++i){
        ufbx_node* n = s->nodes.data[i];
        if (!n->bone) continue;
        size_t d=0; for (ufbx_node* p=n->parent; p; p=p->parent) if(p->bone) d++;
        if (d>maxDepth) maxDepth=d; if(d<32) depthHist[d]++;
        if (boneKids(n).empty()) terminal++;
    }
    printf("maxBoneDepth=%zu terminalBones=%zu\n", maxDepth, terminal);
    printf("depthHist: "); for(size_t d=0; d<=maxDepth && d<32; ++d) printf("d%zu=%zu ", d, depthHist[d]); printf("\n");

    // === branch ORDER (generation) via longest-subtree continuation ===
    // find bone root(s)
    printf("=== BRANCH ORDER (longest-subtree continuation) ===\n");
    std::unordered_map<ufbx_node*,int> memo, ord;
    for (size_t i=0;i<s->nodes.count;++i){
        ufbx_node* n=s->nodes.data[i];
        if(n->bone && (!n->parent || !n->parent->bone)) assignOrder(n,0,memo,ord);
    }
    int maxOrder=0; for(auto&kv:ord) if(kv.second>maxOrder)maxOrder=kv.second;
    std::vector<size_t> orderHist(maxOrder+1,0), orderTerm(maxOrder+1,0);
    for(auto&kv:ord){ orderHist[kv.second]++; if(boneKids(kv.first).empty()) orderTerm[kv.second]++; }
    printf("maxOrder=%d\n", maxOrder);
    for(int o=0;o<=maxOrder;++o) printf("order%d: bones=%zu terminals=%zu\n", o, orderHist[o], orderTerm[o]);

    // === CANDIDATE A: naming-based (Bone_N_Start/_End segment grouping) ===
    // Each number N = one branch segment. Parent segment of N = nearest ancestor
    // bone that belongs to a different segment. Leaf segment = has no child segment.
    printf("=== CANDIDATE A: segment(name) grouping ===\n");
    {
        std::unordered_map<ufbx_node*,int> segOf;
        std::unordered_map<int, ufbx_node*> segStart;
        std::unordered_map<int, std::vector<ufbx_node*>> segNodes;
        int maxSeg=-1;
        for (size_t i=0;i<s->nodes.count;++i){
            ufbx_node* n=s->nodes.data[i]; if(!n->bone) continue;
            int N=-1; const char* nm=n->name.data;
            if (nm && strncmp(nm,"Bone_",5)==0){ N=atoi(nm+5); }
            segOf[n]=N;
            if (N>=0){ segNodes[N].push_back(n); if(N>maxSeg)maxSeg=N;
                if (strstr(nm,"Start")) segStart[N]=n; }
        }
        std::unordered_map<int,int> segParent;
        std::unordered_map<int,std::vector<int>> segKids;
        for (auto& kv : segNodes){
            int N=kv.first; ufbx_node* anyNode=kv.second[0];
            int par=-2;
            for (ufbx_node* p=anyNode->parent; p; p=p->parent){
                if(!p->bone) break;
                auto it=segOf.find(p);
                if(it!=segOf.end() && it->second!=N){ par=it->second; break; }
            }
            segParent[N]=par;
            if(par>=0) segKids[par].push_back(N);
        }
        std::unordered_map<int,int> segOrder;
        for (auto&kv:segNodes){ int N=kv.first,o=0,cur=N,guard=0;
            while(guard++<1000){ auto it=segParent.find(cur); if(it==segParent.end()||it->second<0)break; o++; cur=it->second; }
            segOrder[N]=o; }
        int maxSegOrder=0, leafSeg=0; for(auto&kv:segOrder) if(kv.second>maxSegOrder)maxSegOrder=kv.second;
        std::vector<int> segHist(maxSegOrder+1,0), segLeaf(maxSegOrder+1,0);
        for(auto&kv:segOrder){ segHist[kv.second]++; if(segKids[kv.first].empty()){leafSeg++;segLeaf[kv.second]++;} }
        printf("total_segments=%zu maxSegOrder=%d leafSegments=%d\n",(size_t)segNodes.size(),maxSegOrder,leafSeg);
        for(int o=0;o<=maxSegOrder;++o) printf("segOrder%d: segments=%d leafSegments=%d\n",o,segHist[o],segLeaf[o]);
    }

    // === COORD SPACE CHECK: bone restPos AABB vs mesh vertex AABB (world) ===
    printf("=== COORD SPACE CHECK ===\n");
    {
        ufbx_real bmn[3]={1e30,1e30,1e30}, bmx[3]={-1e30,-1e30,-1e30};
        size_t nb=0;
        for (size_t i=0;i<s->nodes.count;++i){
            ufbx_node* n=s->nodes.data[i]; if(!n->bone) continue;
            ufbx_vec3 p = n->node_to_world.cols[3];
            ufbx_real v[3]={p.x,p.y,p.z};
            for(int k=0;k<3;k++){ if(v[k]<bmn[k])bmn[k]=v[k]; if(v[k]>bmx[k])bmx[k]=v[k]; }
            nb++;
        }
        printf("bone restPos(node_to_world.t) AABB: min(%.3f %.3f %.3f) max(%.3f %.3f %.3f) n=%zu\n",
               bmn[0],bmn[1],bmn[2], bmx[0],bmx[1],bmx[2], nb);

        ufbx_real vmn[3]={1e30,1e30,1e30}, vmx[3]={-1e30,-1e30,-1e30};
        size_t nv=0;
        for (size_t mi=0; mi<s->meshes.count; ++mi){
            ufbx_mesh* m = s->meshes.data[mi];
            if(!m->instances.count) continue;
            ufbx_matrix g2w = m->instances.data[0]->geometry_to_world;
            for (size_t vi=0; vi<m->vertices.count; ++vi){
                ufbx_vec3 wp = ufbx_transform_position(&g2w, m->vertices.data[vi]);
                ufbx_real v[3]={wp.x,wp.y,wp.z};
                for(int k=0;k<3;k++){ if(v[k]<vmn[k])vmn[k]=v[k]; if(v[k]>vmx[k])vmx[k]=v[k]; }
                nv++;
            }
        }
        printf("mesh vertex(geometry_to_world) AABB: min(%.3f %.3f %.3f) max(%.3f %.3f %.3f) n=%zu\n",
               vmn[0],vmn[1],vmn[2], vmx[0],vmx[1],vmx[2], nv);
    }

    ufbx_free_scene(s);
    return 0;
}
