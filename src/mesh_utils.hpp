#pragma once
// ============================================================================
// drawstuff-modern: Modern OpenGL-based drawing library for ODE
// mesh_utils.hpp - mesh generation utilities (public interface)
// ============================================================================

#include <unordered_map>
#include <vector>
#include <cstdint>
#include "drawstuff_core.hpp"   // VertexPN

namespace ds_internal {
    // 三角形メッシュ高速描画用ユーティリティ（CPU側の生成結果）
    struct MeshPN {
        std::vector<VertexPN> vertices;   // pos + normal
        std::vector<uint32_t> indices;    // 0-based index (3つで1三角形)
    };

    struct EdgeKey
    {
        uint32_t a, b; // a < b
        bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
    };
    struct EdgeKeyHash
    {
        size_t operator()(const EdgeKey &k) const noexcept
        {
            uint64_t v = (uint64_t(k.a) << 32) | uint64_t(k.b);
            return std::hash<uint64_t>{}(v);
        }
    };

    using EdgePointTable = std::unordered_map<EdgeKey, std::vector<uint32_t>, EdgeKeyHash>;

    extern const GLfloat gSphereIcosaVerts[12][3];
    extern const int gSphereIcosaFaces[20][3];

    uint32_t getFaceLatticeIndex(
            uint32_t A, uint32_t B, uint32_t C,
            int i, int j, int K,
            std::vector<glm::vec3> &pos,
            EdgePointTable &edgePoints,
            std::vector<uint32_t> &faceInterior);

    // スムーズシェーディング用メッシュ構築
    MeshPN buildSmoothVertexPNFromVerticesAndIndices(
        const std::vector<float>& vertices,         // x0,y0,z0,x1,y1,z1,...
        const std::vector<uint32_t>& indices);      // tr0_i0,tr0_i1,tr0_i2,...

    // クリース角（折り目）付きメッシュ構築
    MeshPN buildCreasedVertexPNFromVerticesAndIndices(
        const std::vector<float>& vertices,         // x,y,z,...
        const std::vector<uint32_t>& indices,       // 0-based index
        float creaseAngleDegrees);
} // namespace ds_internal
