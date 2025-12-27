// ============================================================================
// drawstuff-modern: Modern OpenGL-based drawing library for ODE
// mesh_utils.cpp - mesh generation utilities
// This file is part of drawstuff-modern, a modern reimplementation inspired by
// the drawstuff library distributed with the Open Dynamics Engine (ODE).
// The original drawstuff was developed by Russell L. Smith.
// This implementation has been substantially rewritten and redesigned.

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp> // glm::pi
#include "mesh_utils.hpp"

namespace ds_internal {
    // ============================================================================
    // Mesh generation utility members and functions
    // ============================================================================
    constexpr float ICX = 0.525731112119133606f;
    constexpr float ICZ = 0.850650808352039932f;

    const GLfloat gSphereIcosaVerts[12][3] = {
        {-ICX, 0, ICZ},
        {ICX, 0, ICZ},
        {-ICX, 0, -ICZ},
        {ICX, 0, -ICZ},
        {0, ICZ, ICX},
        {0, ICZ, -ICX},
        {0, -ICZ, ICX},
        {0, -ICZ, -ICX},
        {ICZ, ICX, 0},
        {-ICZ, ICX, 0},
        {ICZ, -ICX, 0},
        {-ICZ, -ICX, 0}};

    const int gSphereIcosaFaces[20][3] = {
        {0, 4, 1},
        {0, 9, 4},
        {9, 5, 4},
        {4, 5, 8},
        {4, 8, 1},
        {8, 10, 1},
        {8, 3, 10},
        {5, 3, 8},
        {5, 2, 3},
        {2, 7, 3},
        {7, 10, 3},
        {7, 6, 10},
        {7, 11, 6},
        {11, 0, 6},
        {0, 1, 6},
        {6, 1, 10},
        {9, 0, 11},
        {9, 11, 2},
        {9, 2, 5},
        {7, 2, 11},
    };

    // edgePoints[key] は「a->b の順に K+1 点」(0..K) の index を持つ。
    // 0 は a, K は b, 中間が (K-1) 個。

    static const std::vector<uint32_t> &getOrBuildEdgePoints(
        uint32_t a, uint32_t b, int K,
        std::vector<glm::vec3> &pos,
        EdgePointTable &edgePoints)
    {
        EdgeKey key{std::min(a, b), std::max(a, b)};
        auto it = edgePoints.find(key);
        if (it != edgePoints.end())
            return it->second;

        // a< b の canonical 方向で作る
        uint32_t lo = key.a, hi = key.b;
        std::vector<uint32_t> idx;
        idx.resize(static_cast<size_t>(K) + 1);

        idx[0] = lo;
        idx[static_cast<size_t>(K)] = hi;

        // 中間点を生成
        for (int t = 1; t < K; ++t)
        {
            float s = float(t) / float(K);
            glm::vec3 p = glm::normalize((1.0f - s) * pos[lo] + s * pos[hi]);
            idx[static_cast<size_t>(t)] = static_cast<uint32_t>(pos.size());
            pos.push_back(p);
        }

        auto [insIt, ok] = edgePoints.emplace(key, std::move(idx));
        return insIt->second;
    }

    // face 内の格子点 (i,j) を取得する。
    // i は B 方向、j は C 方向、残りは A 方向。
    // i>=0, j>=0, i+j<=K
    uint32_t getFaceLatticeIndex(
        uint32_t A, uint32_t B, uint32_t C,
        int i, int j, int K,
        std::vector<glm::vec3> &pos,
        EdgePointTable &edgePoints,
        std::vector<uint32_t> &faceInterior) // face 固有の内部点 index を積む
    {
        // 頂点
        if (i == 0 && j == 0)
            return A;
        if (i == K && j == 0)
            return B;
        if (i == 0 && j == K)
            return C;

        // エッジ上
        if (j == 0)
        {
            // A-B 上: i = 0..K
            const auto &e = getOrBuildEdgePoints(A, B, K, pos, edgePoints);
            // e は min->max 方向。A->B の向きに合わせて t を変換する
            uint32_t lo = std::min(A, B);
            bool forward = (A == lo); // A が lo なら forward
            int t = forward ? i : (K - i);
            return e[static_cast<size_t>(t)];
        }
        if (i == 0)
        {
            // A-C 上: j = 0..K
            const auto &e = getOrBuildEdgePoints(A, C, K, pos, edgePoints);
            uint32_t lo = std::min(A, C);
            bool forward = (A == lo);
            int t = forward ? j : (K - j);
            return e[static_cast<size_t>(t)];
        }
        if (i + j == K)
        {
            // B-C 上: j = 0..K で C 側、i = 0..K で B 側
            // ここでは i を使って B->C の t とみなす（i=0 -> B, i=K -> C）
            const auto &e = getOrBuildEdgePoints(B, C, K, pos, edgePoints);
            uint32_t lo = std::min(B, C);
            bool forward = (B == lo);

            int tFromB = K - i;
            int t = forward ? tFromB : (K - tFromB);
            return e[static_cast<size_t>(t)];
        }

        // 面内部点：ここで作る（この点はこの面にしか属さないので共有不要）
        // バリセントリックで作って球面へ射影
        float a = float(K - i - j) / float(K);
        float b = float(i) / float(K);
        float c = float(j) / float(K);
        glm::vec3 p = glm::normalize(a * pos[A] + b * pos[B] + c * pos[C]);

        uint32_t idx = static_cast<uint32_t>(pos.size());
        pos.push_back(p);
        faceInterior.push_back(idx);
        return idx;
    }

    // 三角形メッシュ高速描画用ユーティリティ
    // スムーズシェーディング用メッシュ構築
    MeshPN buildSmoothVertexPNFromVerticesAndIndices(
        const std::vector<float> &vertices,   // x0,y0,z0,x1,y1,z1,x2,y2,z2,...
        const std::vector<uint32_t> &indices) // tr0_i0,tr0_i1,tr0_i2, tr1_i0,...
    {
        MeshPN result;

        const std::size_t numVerts = vertices.size() / 3;
        const std::size_t numIndices = indices.size();

        result.vertices.resize(numVerts);
        result.indices = indices; // スムーズ版ではインデックスはそのままコピー

        // 1. float配列 → VertexPN配列（法線は0で初期化）
        for (std::size_t i = 0; i < numVerts; ++i)
        {
            result.vertices[i].pos = glm::vec3(
                vertices[3 * i + 0],
                vertices[3 * i + 1],
                vertices[3 * i + 2]);
            result.vertices[i].normal = glm::vec3(0.0f);
        }

        // 2. 面法線を各頂点に加算（スムーズシェーディング用）
        for (std::size_t t = 0; t + 2 < numIndices; t += 3)
        {
            uint32_t i0 = result.indices[t + 0];
            uint32_t i1 = result.indices[t + 1];
            uint32_t i2 = result.indices[t + 2];

            // 念のため範囲チェック（本番では assert でもよい）
            if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts)
                continue;

            const glm::vec3 &p0 = result.vertices[i0].pos;
            const glm::vec3 &p1 = result.vertices[i1].pos;
            const glm::vec3 &p2 = result.vertices[i2].pos;

            glm::vec3 U = p1 - p0;
            glm::vec3 V = p2 - p0;
            glm::vec3 N = glm::cross(U, V); // 正規化はあとでまとめて

            result.vertices[i0].normal += N;
            result.vertices[i1].normal += N;
            result.vertices[i2].normal += N;
        }

        // 3. 各頂点の法線を正規化
        for (std::size_t i = 0; i < numVerts; ++i)
        {
            glm::vec3 &n = result.vertices[i].normal;
            const float len2 = glm::dot(n, n);
            if (len2 > 0.0f)
            {
                n = glm::normalize(n);
            }
            else
            {
                // 万一孤立頂点があれば、とりあえず上向きなどにしておく
                n = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }

        return result;
    }
    struct CornerRef
    {
        uint32_t tri;   // 三角形インデックス（0..numTris-1）
        uint8_t corner; // 0,1,2 のどの頂点か
    };

    // クリース角（折り目）付きメッシュ構築
    MeshPN buildCreasedVertexPNFromVerticesAndIndices(
        const std::vector<float> &vertices,   // x,y,z,...
        const std::vector<uint32_t> &indices, // 0-based index, 3つで1三角形
        const float creaseAngleDegrees)
    {
        MeshPN result;

        const std::size_t numVerts = vertices.size() / 3;
        const std::size_t numIndices = indices.size();
        const std::size_t numTris = numIndices / 3;

        if (numVerts == 0 || numTris == 0)
        {
            // 空メッシュ
            return result;
        }

        // 0. 元の頂点位置だけ確保（法線はあとで作る）
        std::vector<glm::vec3> positions(numVerts);
        for (std::size_t i = 0; i < numVerts; ++i)
        {
            positions[i] = glm::vec3(
                vertices[3 * i + 0],
                vertices[3 * i + 1],
                vertices[3 * i + 2]);
        }

        // 1. 三角形ごとの面法線（単位ベクトル）を計算
        std::vector<glm::vec3> faceNormals(numTris);

        for (std::size_t t = 0; t < numTris; ++t)
        {
            uint32_t i0 = indices[3 * t + 0];
            uint32_t i1 = indices[3 * t + 1];
            uint32_t i2 = indices[3 * t + 2];

            if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts)
            {
                faceNormals[t] = glm::vec3(0.0f, 1.0f, 0.0f); // 保険
                continue;
            }

            const glm::vec3 &p0 = positions[i0];
            const glm::vec3 &p1 = positions[i1];
            const glm::vec3 &p2 = positions[i2];

            glm::vec3 U = p1 - p0;
            glm::vec3 V = p2 - p0;
            glm::vec3 N = glm::cross(U, V);
            float len2 = glm::dot(N, N);
            if (len2 > 0.0f)
            {
                N = glm::normalize(N);
            }
            else
            {
                N = glm::vec3(0.0f, 1.0f, 0.0f); // 保険
            }
            faceNormals[t] = N;
        }

        // 2. 各頂点に接続する「三角形の corner の一覧」を作る
        std::vector<std::vector<CornerRef>> adjacency(numVerts);
        adjacency.assign(numVerts, {}); // 明示初期化

        for (std::size_t t = 0; t < numTris; ++t)
        {
            for (uint8_t c = 0; c < 3; ++c)
            {
                uint32_t vi = indices[3 * t + c];
                if (vi >= numVerts)
                    continue;
                adjacency[vi].push_back(CornerRef{static_cast<uint32_t>(t), c});
            }
        }

        // 3. しきい値（cosθ）を計算
        const float creaseRad = glm::radians(creaseAngleDegrees);
        const float cosThreshold = std::cos(creaseRad);
        // 角度が小さい(=cosが大きい)もの同士は同じクラスタにまとめる。
        // 例: crease=60° → cos≈0.5 → 60°未満は同一クラスタ。

        // 出力側のバッファはここで構築する
        result.indices.resize(numIndices); // 三角形数は変わらない

        // 4. 頂点ごとに面法線をクラスタリングして、頂点を複製しつつ indices を書き換える
        result.vertices.clear();
        result.vertices.reserve(numVerts); // おおよその大きさ（エッジが多いと増える）

        for (std::size_t v = 0; v < numVerts; ++v)
        {
            const auto &corners = adjacency[v];

            if (corners.empty())
            {
                // この頂点はどの三角形にも使われていない → スキップ
                continue;
            }

            // クラスタ構造体
            struct Cluster
            {
                glm::vec3 normalAccum;
                glm::vec3 repNormal; // 代表法線（正規化済み）
                std::vector<CornerRef> members;
            };

            std::vector<Cluster> clusters;

            for (const CornerRef &cr : corners)
            {
                const glm::vec3 &nFace = faceNormals[cr.tri];

                int bestIndex = -1;
                float bestDot = -1.0f;

                // 既存クラスタのどれと一番近いかを探す
                for (std::size_t ci = 0; ci < clusters.size(); ++ci)
                {
                    float d = glm::dot(nFace, clusters[ci].repNormal);
                    if (d > bestDot)
                    {
                        bestDot = d;
                        bestIndex = static_cast<int>(ci);
                    }
                }

                if (bestIndex < 0 || bestDot < cosThreshold)
                {
                    // 新しいクラスタを作る
                    Cluster c;
                    c.normalAccum = nFace;
                    c.repNormal = nFace; // unit なのでそのまま代表に
                    c.members.push_back(cr);
                    clusters.push_back(c);
                }
                else
                {
                    // 既存クラスタに追加
                    Cluster &c = clusters[bestIndex];
                    c.normalAccum += nFace;
                    c.members.push_back(cr);

                    // 代表法線を更新（normalAccumを正規化）
                    float len2 = glm::dot(c.normalAccum, c.normalAccum);
                    if (len2 > 0.0f)
                    {
                        c.repNormal = glm::normalize(c.normalAccum);
                    }
                }
            }

            // できたクラスタごとに「新しい頂点」を作り、インデックスを書き換える
            for (const Cluster &c : clusters)
            {
                VertexPN newV;
                newV.pos = positions[v];

                float len2 = glm::dot(c.normalAccum, c.normalAccum);
                if (len2 > 0.0f)
                {
                    newV.normal = glm::normalize(c.normalAccum);
                }
                else
                {
                    newV.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                // このクラスタ用の新しい頂点インデックス
                const uint32_t newIndex =
                    static_cast<uint32_t>(result.vertices.size());
                result.vertices.push_back(newV);

                // このクラスタに属する corner について indices を置き換える
                for (const CornerRef &cr : c.members)
                {
                    result.indices[3 * cr.tri + cr.corner] = newIndex;
                }
            }
        }

        return result;
    }

}