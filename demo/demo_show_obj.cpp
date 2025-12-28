// obj_viewer_ds.cpp
// Build example (conceptual):
//   g++ obj_viewer_ds.cpp -std=c++17 -O2 -I/path/to/drawstuff/include -L... -ldrawstuff ... -o obj_viewer_ds
//
// Usage:
//   ./obj_viewer_ds path/to/model.obj

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <algorithm>

#include "drawstuff/drawstuff.h"   // ←環境に合わせて include パス調整

#define DS_HUD_FPS_IMPLEMENTATION
#include "hud_fps.hpp"
static fps_hud::FpsHud g_fps_hud;

// ------------------------------
// OBJ loader (minimal)
// ------------------------------

struct Vec3 {
  float x=0, y=0, z=0;
};

struct AABB {
  Vec3 mn{+std::numeric_limits<float>::infinity(),
          +std::numeric_limits<float>::infinity(),
          +std::numeric_limits<float>::infinity()};
  Vec3 mx{-std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()};
  void expand(const Vec3& v) {
    mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
    mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
  }
  Vec3 center() const { return Vec3{(mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f, (mn.z+mx.z)*0.5f}; }
  float diag() const {
    float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
    return std::max({dx,dy,dz});
  }
};

// OBJ file --> verices (flat xyz array), indices (0-based triangle list)
// Helpers
static inline void trimLeft(std::string &s)
{
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    s.erase(0, i);
}

static inline int parseObjPositionIndexToken(const std::string &tok)
{
    // tok can be: "v", "v/vt", "v//vn", "v/vt/vn"
    // We only need the first integer before the first slash.
    if (tok.empty()) throw std::runtime_error("OBJ: empty face token.");

    size_t slash = tok.find('/');
    std::string first = (slash == std::string::npos) ? tok : tok.substr(0, slash);

    if (first.empty()) throw std::runtime_error("OBJ: missing position index in face token: " + tok);

    // stoi allows leading +/-; OBJ indices are 1-based, negatives allowed.
    int idx = 0;
    try {
        idx = std::stoi(first);
    } catch (...) {
        throw std::runtime_error("OBJ: invalid position index in face token: " + tok);
    }
    if (idx == 0) throw std::runtime_error("OBJ: position index cannot be 0: " + tok);
    return idx;
}

static inline unsigned int resolveObjIndexToZeroBased(int objIndex1BasedOrNegative, int vertexCount)
{
    // OBJ:
    //  positive: 1..vertexCount
    //  negative: -1..-vertexCount  (relative to the end)
    // Convert to 0-based [0..vertexCount-1]
    int zeroBased = 0;
    if (objIndex1BasedOrNegative > 0)
    {
        zeroBased = objIndex1BasedOrNegative - 1;
    }
    else
    {
        // -1 means last vertex => vertexCount-1
        zeroBased = vertexCount + objIndex1BasedOrNegative;
    }

    if (zeroBased < 0 || zeroBased >= vertexCount)
        throw std::runtime_error("OBJ: face index out of range.");

    return static_cast<unsigned int>(zeroBased);
}

// Main
// vertices: flat xyz array (size = 3 * numVerts)
// indices : triangle indices (size = 3 * numTris), 0-based
static void loadObjIndexedMesh(
    const std::string& path,
    std::vector<float>& vertices,
    std::vector<unsigned int>& indices)
{
    vertices.clear();
    indices.clear();

    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("OBJ: cannot open file: " + path);

    std::string line;
    int vertexCount = 0;

    while (std::getline(ifs, line))
    {
        // Strip comments
        if (auto p = line.find('#'); p != std::string::npos) line.erase(p);

        trimLeft(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag.empty()) continue;

        if (tag == "v")
        {
            float x, y, z;
            if (!(iss >> x >> y >> z))
                throw std::runtime_error("OBJ: invalid vertex line: " + line);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertexCount++;
        }
        else if (tag == "f")
        {
            // Read all vertex tokens in this face
            std::vector<unsigned int> face; // 0-based position indices
            face.reserve(8);

            std::string tok;
            while (iss >> tok)
            {
                const int objIdx = parseObjPositionIndexToken(tok);
                const unsigned int v0 = resolveObjIndexToZeroBased(objIdx, vertexCount);
                face.push_back(v0);
            }

            if (face.size() < 3)
                throw std::runtime_error("OBJ: face has fewer than 3 vertices: " + line);

            // Fan triangulation: (0, i, i+1)
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                indices.push_back(face[0]);
                indices.push_back(face[i]);
                indices.push_back(face[i + 1]);
            }
        }
        else
        {
            // ignore: vt, vn, o, g, s, usemtl, mtllib, etc.
            continue;
        }
    }

    if (vertexCount == 0)
        throw std::runtime_error("OBJ: no vertices found: " + path);
    // indices can be 0 if OBJ has no faces; allow it if you want, but usually it's an error:
    if (indices.empty())
        throw std::runtime_error("OBJ: no faces found: " + path);
}

// vertices: flat xyz array (size = 3 * numVerts)
// indices : triangle indices (size = 3 * numTris)
// triVerts9: output (size = 9 * numTris)
// numTris: output triangle count (optional; can also just triVerts9.size()/9)
static void buildTriVerts9FromIndexedMesh(
    const std::vector<float>& vertices,
    const std::vector<unsigned int>& indices,
    std::vector<float>& triVerts9)
{
    if (vertices.size() % 3 != 0)
        throw std::runtime_error("vertices size is not multiple of 3.");
    if (indices.size() % 3 != 0)
        throw std::runtime_error("indices size is not multiple of 3.");

    const size_t numVerts = vertices.size() / 3;
    const size_t numTris = indices.size() / 3;

    triVerts9.clear();
    triVerts9.reserve(static_cast<size_t>(numTris) * 9);

    for (size_t t = 0; t < indices.size(); t += 3)
    {
        const unsigned int i0 = indices[t + 0];
        const unsigned int i1 = indices[t + 1];
        const unsigned int i2 = indices[t + 2];

        if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts)
            throw std::runtime_error("index out of range in indices.");

        // v0
        triVerts9.push_back(vertices[3 * i0 + 0]);
        triVerts9.push_back(vertices[3 * i0 + 1]);
        triVerts9.push_back(vertices[3 * i0 + 2]);
        // v1
        triVerts9.push_back(vertices[3 * i1 + 0]);
        triVerts9.push_back(vertices[3 * i1 + 1]);
        triVerts9.push_back(vertices[3 * i1 + 2]);
        // v2
        triVerts9.push_back(vertices[3 * i2 + 0]);
        triVerts9.push_back(vertices[3 * i2 + 1]);
        triVerts9.push_back(vertices[3 * i2 + 2]);
    }
}

// ------------------------------
// drawstuff callbacks
// ------------------------------
enum TrimeshDrawMode {
  TRIMESH_DRAW_TRIANGLES = 1,
  TRIMESH_DRAW_REGISTERED_MESH = 2
};

constexpr float gap = 0.05f;

static std::vector<float> g_vertices;
static std::vector<unsigned int> g_indices;
static std::vector<float> g_triVerts9;
static int g_numTris = 0;
static AABB g_aabb;
static dsMeshHandle g_trimesh;

static TrimeshDrawMode g_draw_mode = TRIMESH_DRAW_REGISTERED_MESH;

static float g_pos[3] = {0.0f, 0.0f, 2.0f};
static float g_R[12]  = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0
};

static int gridExtent = 2;
static int g_solid = 1;

static const char* kHelpText =
    "Keys:\n"
    "  W : wireframe\n"
    "  S : solid\n"
    "  +/- : grid extent\n"
    "  Space : toggle draw mode\n"
    "    triangles / registered mesh\n";

static void simStart() {
    std::cout << kHelpText << std::endl;
    g_fps_hud.init();

  // viewpoint: center + a bit back
  const Vec3 c = g_aabb.center();
  float d = g_aabb.diag();
  if (!(d > 0)) d = 1.0f;

  // drawstuff viewpoint: (xyz, hpr)  ※環境により解釈が違うことがあるが、まずはこれで
  float xyz[3] = { c.x, c.y - 2.5f*d, c.z + 1.5f*d };
  float hpr[3] = { 90.0f, -15.0f, 0.0f };
  dsSetViewpoint(xyz, hpr);

  // 描画を中心に寄せたい場合（ここではオブジェクト座標を平行移動）
  // → dsDrawTriangles の pos で打ち消す
  g_pos[0] -= c.x;
  g_pos[1] -= c.y;
  g_pos[2] -= c.z;
}

static void simCommand(int cmd) {
  if (cmd == 'w' || cmd == 'W') g_solid = 0;
  if (cmd == 's' || cmd == 'S') g_solid = 1;
  if (cmd == '+') gridExtent = std::min(gridExtent + 1, 5);
  if (cmd == '-') gridExtent = std::max(gridExtent - 1, 0);
  if (cmd == ' ') {
    // toggle draw mode
    if (g_draw_mode == TRIMESH_DRAW_TRIANGLES) {
        g_draw_mode = TRIMESH_DRAW_REGISTERED_MESH;
        std::cout << "Draw mode: registered mesh\n";
    }
    else
    {
        g_draw_mode = TRIMESH_DRAW_TRIANGLES;
        std::cout << "Draw mode: triangles\n";
    }
  }
}

static void simStep(int /*pause*/)
{
    const float gridPitchX = (g_aabb.mx.x - g_aabb.mn.x) + gap;
    const float gridPitchY = (g_aabb.mx.y - g_aabb.mn.y) + gap;
    dsSetColor(0.8f, 0.8f, 0.9f);
    for (int i = -gridExtent; i <= gridExtent; ++i)
    {
        const float x = static_cast<float>(i) * gridPitchX;
        for (int j = -gridExtent; j <= gridExtent; ++j)
        {
            const float y = static_cast<float>(j) * gridPitchY;
            const float pos[3] = {g_pos[0] + x, g_pos[1] + y, g_pos[2]};
            if (g_draw_mode == TRIMESH_DRAW_REGISTERED_MESH)
            {
                dsDrawRegisteredMesh(g_trimesh, pos, g_R, g_solid != 0);
            }
            else // TRIMESH_DRAW_TRIANGLES
            {
                dsDrawTriangles(pos, g_R, g_triVerts9.data(), g_numTris, g_solid);
            }
        }
    }
}

static void simStop() {}

static void postStep(int pause)
{
    // HUD FPS display
    g_fps_hud.tick();
    g_fps_hud.render(1024, 768); // adjust if needed
}

// ------------------------------
// main
// ------------------------------

int main(int argc, char** argv) {
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " path/to/model.obj\n";
        return 2;
    }

    const std::string objPath = argv[1];

    loadObjIndexedMesh(objPath, g_vertices, g_indices);
    g_numTris = static_cast<int>(g_indices.size() / 3);
    buildTriVerts9FromIndexedMesh(
        g_vertices, g_indices, g_triVerts9);

    std::cerr << "Loaded triangles: " << g_numTris << "\n";

    // Compute AABB
    g_aabb = AABB();
    for (size_t i = 0; i < g_vertices.size(); i += 3)
    {
        Vec3 v{ g_vertices[i + 0], g_vertices[i + 1], g_vertices[i + 2] };
        g_aabb.expand(v);
    }

    // Register mesh
    g_trimesh = dsRegisterIndexedMesh(
        g_vertices, g_indices);

    dsFunctions fn;
    fn.version = DS_VERSION;
    fn.start = &simStart;
    fn.step = &simStep;
    fn.command = &simCommand;
    fn.stop = &simStop;
    fn.path_to_textures = "../textures";
    fn.postStep = &postStep;

    // window size etc.
    dsSimulationLoop(argc, argv, 1024, 768, &fn);
    return 0;
}
