// ============================================================================
// drawstuff - initialize primitive meshes
// src/primitive_meshes.cpp
// This file is part of drawstuff-modern, a modern reimplementation inspired by
// the drawstuff library distributed with the Open Dynamics Engine (ODE).
// The original drawstuff was developed by Russell L. Smith.
// This implementation has been substantially rewritten and redesigned.
// ============================================================================
// Copyright (c) 2025 Akihisa Konno
// Released under the BSD 3-Clause License.
// See the LICENSE file for details.

#include "drawstuff_core.hpp"
#include "mesh_utils.hpp"

namespace ds_internal {
    // =================================================
    // メッシュ初期化
    // 地面・空メッシュ初期化
    void DrawstuffApp::initGroundMesh()
    {
        if (vaoGround_ != 0)
            return; // すでに初期化済みなら何もしない

        const float gsize = 100.0f;
        const float offset = 0.0f; // 元コードの offset

        // 平面 z = offset 上の四角形を 2 つの三角形に分割（6頂点）
        glm::vec3 p0(-gsize, -gsize, offset);
        glm::vec3 p1(gsize, -gsize, offset);
        glm::vec3 p2(gsize, gsize, offset);
        glm::vec3 p3(-gsize, gsize, offset);

        glm::vec3 n(0.0f, 0.0f, 1.0f);

        // 頂点フォーマットは VertexPN に統一（色は持たない）
        std::array<VertexPN, 6> verts = {
            VertexPN{p0, n},
            VertexPN{p1, n},
            VertexPN{p2, n},

            VertexPN{p0, n},
            VertexPN{p2, n},
            VertexPN{p3, n},
        };

        glGenVertexArrays(1, &vaoGround_);
        glGenBuffers(1, &vboGround_);

        glBindVertexArray(vaoGround_);
        glBindBuffer(GL_ARRAY_BUFFER, vboGround_);

        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(verts),
                     verts.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        // layout(location = 1) normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        // 色属性(layout=2)は使わないので何もしない

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void DrawstuffApp::initSkyMesh()
    {
        if (vaoSky_ != 0)
            return;

        const float ssize = 1000.0f; // 元コードと同じ

        glm::vec3 p0(-ssize, -ssize, 0.0f);
        glm::vec3 p1(-ssize, ssize, 0.0f);
        glm::vec3 p2(ssize, ssize, 0.0f);
        glm::vec3 p3(ssize, -ssize, 0.0f);

        // 空の板はカメラの上側にあるので、法線は -Z 向きで統一
        glm::vec3 n(0.0f, 0.0f, -1.0f);

        // 頂点フォーマットは VertexPN に統一（色は持たない）
        std::array<VertexPN, 6> verts = {
            VertexPN{p0, n},
            VertexPN{p1, n},
            VertexPN{p2, n},

            VertexPN{p0, n},
            VertexPN{p2, n},
            VertexPN{p3, n},
        };

        glGenVertexArrays(1, &vaoSky_);
        glGenBuffers(1, &vboSky_);

        glBindVertexArray(vaoSky_);
        glBindBuffer(GL_ARRAY_BUFFER, vboSky_);

        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(verts),
                     verts.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        // layout(location = 1) normal（ライティングするなら使う）
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        // layout(location = 2) color は使わないので何もしない

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void DrawstuffApp::initSphereMeshForQuality(int quality, Mesh &dstMesh)
    {
        const int K = quality + 1;
        if (K < 1)
        {
            internalError("initSphereMeshForQuality: invalid level");
        }

        // --- 0) 初期 12 頂点（icosahedron） ---
        std::vector<glm::vec3> pos;
        pos.reserve(12 + 30 * (K > 1 ? (K - 1) : 0)); // ざっくり
        for (int i = 0; i < 12; ++i)
        {
            glm::vec3 p(gSphereIcosaVerts[i][0], gSphereIcosaVerts[i][1], gSphereIcosaVerts[i][2]);
            pos.push_back(glm::normalize(p));
        }

        // --- 1) エッジ分割点共有テーブル ---
        EdgePointTable edgePoints;
        edgePoints.reserve(64);

        // --- 2) index を生成（面ごとに K^2 個の三角形）---
        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(20) * static_cast<size_t>(K) * static_cast<size_t>(K) * 3);

        // 面ごと内部点の生成indexを集める（デバッグ用に使える）
        std::vector<uint32_t> faceInterior;
        faceInterior.reserve(static_cast<size_t>(20) * (K > 2 ? (K - 1) * (K - 2) / 2 : 0));

        for (int fi = 0; fi < 20; ++fi)
        {
            // あなたの元コードの順序（faces[i][2],[1],[0]）を踏襲
            uint32_t A = (uint32_t)gSphereIcosaFaces[fi][2];
            uint32_t B = (uint32_t)gSphereIcosaFaces[fi][1];
            uint32_t C = (uint32_t)gSphereIcosaFaces[fi][0];

            // face の格子 index を (i,j) で保持
            // i: 0..K, j: 0..K-i
            std::vector<std::vector<uint32_t>> grid(static_cast<size_t>(K) + 1);
            for (int i = 0; i <= K; ++i)
            {
                grid[static_cast<size_t>(i)].resize(static_cast<size_t>(K - i) + 1);
                for (int j = 0; j <= K - i; ++j)
                {
                    grid[static_cast<size_t>(i)][static_cast<size_t>(j)] =
                        getFaceLatticeIndex(A, B, C, i, j, K, pos, edgePoints, faceInterior);
                }
            }

            // 三角形化：各小セルを2枚（ただし端は1枚）
            // 典型的な三角格子の張り方
            for (int i = 0; i < K; ++i)
            {
                for (int j = 0; j < K - i; ++j)
                {
                    uint32_t v0 = grid[static_cast<size_t>(i)][static_cast<size_t>(j)];
                    uint32_t v1 = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(j)];
                    uint32_t v2 = grid[static_cast<size_t>(i)][static_cast<size_t>(j + 1)];

                    // tri 1
                    indices.push_back(v0);
                    indices.push_back(v1);
                    indices.push_back(v2);

                    // tri 2（右上が存在する時だけ）
                    if (j < K - i - 1)
                    {
                        uint32_t v3 = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(j + 1)];
                        indices.push_back(v1);
                        indices.push_back(v3);
                        indices.push_back(v2);
                    }
                }
            }
        }

        // --- 3) VertexPN 化（球なら normal=pos でスムース） ---
        std::vector<VertexPN> vertices;
        vertices.resize(pos.size());
        for (size_t i = 0; i < pos.size(); ++i)
        {
            vertices[i].pos = pos[i];
            vertices[i].normal = pos[i]; // unit sphere → smooth shading
        }

        // --- 4) VAO / VBO / EBO 作成（あなたのコードと同じ） ---
        glGenVertexArrays(1, &dstMesh.vao);
        glBindVertexArray(dstMesh.vao);

        glGenBuffers(1, &dstMesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, dstMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(VertexPN),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glGenBuffers(1, &dstMesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dstMesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(),
                     GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN),
                              reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN),
                              reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        dstMesh.indexCount = static_cast<GLsizei>(indices.size());

        glBindVertexArray(0);
    }

    void DrawstuffApp::initCylinderMeshForQuality(int quality, Mesh &dstMesh)
    {
        constexpr float PI = glm::pi<float>();
        using std::sin;
        using std::cos;

        // --- 1. quality → 分割数のマッピング ---
        int q = quality;
        if (q < 1)
            q = 1;
        if (q > 3)
            q = 3;

        int slices;
        switch (q)
        {
        case 1:
            slices = 12;
            break;
        case 2:
            slices = 24;
            break; // 旧 n=24 と対応
        default:
            slices = 48;
            break;
        }

        // --- 2. ジオメトリ生成（単位円柱：半径1, z∈[-0.5,0.5]） ---
        std::vector<VertexPN> vertices;
        std::vector<uint32_t> indices;

        const float r = 1.0f;
        const float halfLen = 0.5f;
        const int n = slices;
        const float a = 2.0f * static_cast<float>(PI) / static_cast<float>(n);

        // 2-1. 側面（サイド）
        //
        // 軸: Z
        // 円周: X-Y 平面
        //
        for (int i = 0; i < n; ++i)
        {
            float theta = a * static_cast<float>(i);
            float nx = std::cos(theta); // 半径方向 X
            float ny = std::sin(theta); // 半径方向 Y

            glm::vec3 normal(nx, ny, 0.0f);

            // 上側 (+Z)
            vertices.push_back({glm::vec3(r * nx, r * ny, +halfLen),
                                normal});

            // 下側 (-Z)
            vertices.push_back({glm::vec3(r * nx, r * ny, -halfLen),
                                normal});
        }

        // 側面インデックス
        // i番目スライスの2頂点:
        //   top   : 2*i
        //   bottom: 2*i + 1
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n; // 次のスライス（ラップアラウンド）
            uint32_t iTop0 = 2 * i;
            uint32_t iBot0 = 2 * i + 1;
            uint32_t iTop1 = 2 * j;
            uint32_t iBot1 = 2 * j + 1;

            // 三角形1: top0, bot0, top1
            indices.push_back(iTop0);
            indices.push_back(iBot0);
            indices.push_back(iTop1);

            // 三角形2: bot0, bot1, top1
            indices.push_back(iBot0);
            indices.push_back(iBot1);
            indices.push_back(iTop1);
        }

        // 2-2. 上キャップ (+Z)
        uint32_t topCenterIndex = static_cast<uint32_t>(vertices.size());
        vertices.push_back({
            glm::vec3(0.0f, 0.0f, +halfLen),
            glm::vec3(0.0f, 0.0f, +1.0f) // 法線 +Z
        });

        uint32_t topRingStart = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < n; ++i)
        {
            float theta = a * static_cast<float>(i);
            float x = std::cos(theta);
            float y = std::sin(theta);

            vertices.push_back({glm::vec3(r * x, r * y, +halfLen),
                                glm::vec3(0.0f, 0.0f, +1.0f)});
        }

        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n; // ラップアラウンド
            uint32_t i0 = topCenterIndex;
            uint32_t i1 = topRingStart + i;
            uint32_t i2 = topRingStart + j;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
        }

        // 2-3. 下キャップ (-Z)
        uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size());
        vertices.push_back({
            glm::vec3(0.0f, 0.0f, -halfLen),
            glm::vec3(0.0f, 0.0f, -1.0f) // 法線 -Z
        });

        uint32_t bottomRingStart = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < n; ++i)
        {
            float theta = a * static_cast<float>(i);
            float x = std::cos(theta);
            float y = std::sin(theta);

            vertices.push_back({glm::vec3(r * x, r * y, -halfLen),
                                glm::vec3(0.0f, 0.0f, -1.0f)});
        }

        // 下側は外から見て CCW になるように頂点順を反転
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n; // ラップアラウンド
            uint32_t i0 = bottomCenterIndex;
            uint32_t i1 = bottomRingStart + j;
            uint32_t i2 = bottomRingStart + i;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
        }

        // --- 3. VAO / VBO / EBO へアップロード（sphere と同様） ---
        glGenVertexArrays(1, &dstMesh.vao);
        glBindVertexArray(dstMesh.vao);

        glGenBuffers(1, &dstMesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, dstMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(VertexPN),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glGenBuffers(1, &dstMesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dstMesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) vec3 aPos;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        // layout(location = 1) vec3 aNormal;
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        dstMesh.indexCount = static_cast<GLsizei>(indices.size());

        glBindVertexArray(0);
    }

    void DrawstuffApp::initTriangleMesh()
    {
        glGenVertexArrays(1, &meshTriangle_.vao);
        glBindVertexArray(meshTriangle_.vao);

        // VBO
        glGenBuffers(1, &meshTriangle_.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshTriangle_.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     3 * sizeof(VertexPN),
                     nullptr,
                     GL_DYNAMIC_DRAW);

        // ★ VAO を bind した「状態」で EBO を bind することが重要
        glGenBuffers(1, &meshTriangle_.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshTriangle_.ebo);
        uint32_t indices[3] = {0, 1, 2};
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     sizeof(indices),
                     indices,
                     GL_STATIC_DRAW);

        // 属性
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            (void *)offsetof(VertexPN, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            (void *)offsetof(VertexPN, normal));

        glBindVertexArray(0);

        meshTriangle_.indexCount = 3;
    }

    void DrawstuffApp::initTrianglesBatchMesh()
    {
        glGenVertexArrays(1, &meshTrianglesBatch_.vao);
        glBindVertexArray(meshTrianglesBatch_.vao);

        glGenBuffers(1, &meshTrianglesBatch_.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshTrianglesBatch_.vbo);

        trianglesBatchCapacity_ = 0;
        // ここではまだ glBufferData はしない（最初の呼び出し時にサイズ決定）

        // インデックスは使わない
        meshTrianglesBatch_.ebo = 0;
        meshTrianglesBatch_.indexCount = 0;

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        glBindVertexArray(0);
    }
    void DrawstuffApp::initLineMesh()
    {
        glGenVertexArrays(1, &meshLine_.vao);
        glBindVertexArray(meshLine_.vao);

        glGenBuffers(1, &meshLine_.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshLine_.vbo);

        // ラインなので頂点は常に2つ分だけあればよい
        glBufferData(GL_ARRAY_BUFFER,
                     2 * sizeof(VertexPN),
                     nullptr,
                     GL_DYNAMIC_DRAW);

        // ライン用メッシュ構築（インデックスは使わない）
        meshLine_.ebo = 0;
        meshLine_.indexCount = 2;
        meshLine_.primitive = GL_LINES;

        // layout(location=0) pos, location=1 normal
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    void DrawstuffApp::initPyramidMesh()
    {
        if (meshPyramid_.vao != 0)
        {
            return; // すでに初期化済み
        }

        // 単位ピラミッド（中心原点・高さ1, 底面±1）を作る
        constexpr float k = 1.0f;
        const glm::vec3 top(0.0f, 0.0f, k);
        const glm::vec3 p1(-k, -k, 0.0f);
        const glm::vec3 p2(k, -k, 0.0f);
        const glm::vec3 p3(k, k, 0.0f);
        const glm::vec3 p4(-k, k, 0.0f);

        glm::vec3 n01 = glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f));
        glm::vec3 n12 = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
        glm::vec3 n23 = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
        glm::vec3 n30 = glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f));

        // 面ごとに頂点を重複させた 12 頂点
        std::array<VertexPN, 12> verts;

        // face 0: top, p1, p2
        verts[0] = {top, n01};
        verts[1] = {p1, n01};
        verts[2] = {p2, n01};
        // face 1: top, p2, p3
        verts[3] = {top, n12};
        verts[4] = {p2, n12};
        verts[5] = {p3, n12};
        // face 2: top, p3, p4
        verts[6] = {top, n23};
        verts[7] = {p3, n23};
        verts[8] = {p4, n23};
        // face 3: top, p4, p1
        verts[9] = {top, n30};
        verts[10] = {p4, n30};
        verts[11] = {p1, n30};

        glGenVertexArrays(1, &meshPyramid_.vao);
        glGenBuffers(1, &meshPyramid_.vbo);

        glBindVertexArray(meshPyramid_.vao);
        glBindBuffer(GL_ARRAY_BUFFER, meshPyramid_.vbo);

        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(verts),
                     verts.data(),
                     GL_STATIC_DRAW);

        // インデックスは使わず glDrawArrays する
        meshPyramid_.ebo = 0;
        meshPyramid_.indexCount = static_cast<GLsizei>(verts.size());

        // layout(location = 0) position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        // layout(location = 1) normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    static void initMeshFromVectors(
        Mesh &dst,
        const std::vector<VertexPN> &vertices,
        const std::vector<uint32_t> &indices)
    {
        // 既存リソースの破棄
        if (dst.vao)
        {
            glDeleteVertexArrays(1, &dst.vao);
            dst.vao = 0;
        }
        if (dst.vbo)
        {
            glDeleteBuffers(1, &dst.vbo);
            dst.vbo = 0;
        }
        if (dst.ebo)
        {
            glDeleteBuffers(1, &dst.ebo);
            dst.ebo = 0;
        }

        glGenVertexArrays(1, &dst.vao);
        glBindVertexArray(dst.vao);

        glGenBuffers(1, &dst.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(VertexPN),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glGenBuffers(1, &dst.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(),
                     GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, pos)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPN),
            reinterpret_cast<void *>(offsetof(VertexPN, normal)));

        dst.indexCount = static_cast<GLsizei>(indices.size());

        glBindVertexArray(0);
    }

    // =================================================
    // Capsule メッシュ初期化補助関数群
    // ---- ユーティリティ：量子化キーで頂点溶接（重複排除） ----
    struct VKey
    {
        int px, py, pz;
        int nx, ny, nz;
        bool operator==(const VKey &o) const
        {
            return px == o.px && py == o.py && pz == o.pz && nx == o.nx && ny == o.ny && nz == o.nz;
        }
    };

    struct VKeyHash
    {
        std::size_t operator()(const VKey &k) const noexcept
        {
            auto h = [](int v) -> std::size_t
            {
                // 32-bit mix
                std::uint32_t x = (std::uint32_t)v;
                x ^= x >> 16;
                x *= 0x7feb352dU;
                x ^= x >> 15;
                x *= 0x846ca68bU;
                x ^= x >> 16;
                return (std::size_t)x;
            };
            std::size_t r = 0;
            r ^= h(k.px) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            r ^= h(k.py) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            r ^= h(k.pz) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            r ^= h(k.nx) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            r ^= h(k.ny) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            r ^= h(k.nz) + 0x9e3779b97f4a7c15ULL + (r << 6) + (r >> 2);
            return r;
        }
    };

    static inline int qf(float x, float scale)
    {
        return (int)std::lrint(x * scale);
    }

    static uint32_t addVertexWelded(std::vector<VertexPN> &outVerts,
                                    std::unordered_map<VKey, uint32_t, VKeyHash> &map,
                                    const glm::vec3 &pos,
                                    const glm::vec3 &nrm,
                                    float posQuant = 1e6f,
                                    float nrmQuant = 1e6f)
    {
        VKey key{
            qf(pos.x, posQuant), qf(pos.y, posQuant), qf(pos.z, posQuant),
            qf(nrm.x, nrmQuant), qf(nrm.y, nrmQuant), qf(nrm.z, nrmQuant)};
        auto it = map.find(key);
        if (it != map.end())
            return it->second;

        VertexPN v;
        v.pos = pos;
        v.normal = glm::normalize(nrm);
        uint32_t idx = (uint32_t)outVerts.size();
        outVerts.push_back(v);
        map.emplace(key, idx);
        return idx;
    }

    // ---- cubed-sphere 面定義 ----
    enum class CubeFace
    {
        PX,
        NX,
        PY,
        NY,
        PZ,
        NZ
    };

    // u,v in [-1, 1]
    static glm::vec3 cubeToDir(CubeFace f, float u, float v)
    {
        switch (f)
        {
        case CubeFace::PX:
            return glm::vec3(1.f, v, -u);
        case CubeFace::NX:
            return glm::vec3(-1.f, v, u);
        case CubeFace::PY:
            return glm::vec3(u, 1.f, -v);
        case CubeFace::NY:
            return glm::vec3(u, -1.f, v);
        case CubeFace::PZ:
            return glm::vec3(u, v, 1.f);
        case CubeFace::NZ:
            return glm::vec3(-u, v, -1.f);
        }
        return glm::vec3(0);
    }

    // ---- クリッピング：半球平面 localZ >= 0 (top) / <= 0 (bottom) ----
    // 入力は “球面上の点” (center + r*dir) を想定。
    // 境界点は z=0 の赤道にスナップして (x,y) を正規化する。
    static inline float signedPlaneZ(const glm::vec3 &pLocal, bool top)
    {
        // top: inside if z >= 0
        // bottom: inside if z <= 0  => inside if -z >= 0
        return top ? pLocal.z : -pLocal.z;
    }

    static glm::vec3 snapToEquator(const glm::vec3 &pLocal, float r)
    {
        glm::vec3 d = pLocal;
        d.z = 0.0f;
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 1e-20f)
        {
            // 退化：理論上は起きにくい（赤道はz=0でx^2+y^2=1）
            return glm::vec3(r, 0, 0);
        }
        d.x /= len;
        d.y /= len;
        return glm::vec3(d.x * r, d.y * r, 0.0f);
    }

    // tri clip against plane signedPlaneZ>=0 (in local coords)
    // returns polygon (0..4 vertices) in local coords on sphere, already snapped on boundary.
    static void clipTriToHemisphere(const glm::vec3 &aL,
                                    const glm::vec3 &bL,
                                    const glm::vec3 &cL,
                                    bool top,
                                    float r,
                                    std::vector<glm::vec3> &outPoly)
    {
        outPoly.clear();
        glm::vec3 p[3] = {aL, bL, cL};
        float s[3] = {signedPlaneZ(p[0], top),
                      signedPlaneZ(p[1], top),
                      signedPlaneZ(p[2], top)};

        auto inside = [&](int i)
        { return s[i] >= 0.0f; };

        // Sutherland–Hodgman (triangle -> polygon)
        std::vector<glm::vec3> poly = {p[0], p[1], p[2]};
        std::vector<float> val = {s[0], s[1], s[2]};

        auto clipOnce = [&](std::vector<glm::vec3> &inP, std::vector<float> &inV,
                            std::vector<glm::vec3> &outP, std::vector<float> &outV)
        {
            outP.clear();
            outV.clear();
            const int m = (int)inP.size();
            for (int i = 0; i < m; ++i)
            {
                int j = (i + 1) % m;
                const glm::vec3 &P = inP[i];
                const glm::vec3 &Q = inP[j];
                float VP = inV[i];
                float VQ = inV[j];
                bool in1 = (VP >= 0.0f);
                bool in2 = (VQ >= 0.0f);

                auto emit = [&](const glm::vec3 &X, float VX)
                {
                    outP.push_back(X);
                    outV.push_back(VX);
                };

                if (in1 && in2)
                {
                    emit(Q, VQ);
                }
                else if (in1 && !in2)
                {
                    // leaving: add intersection
                    float t = VP / (VP - VQ); // VP + t*(VQ-VP) = 0
                    glm::vec3 I = P + t * (Q - P);
                    I = snapToEquator(I, r);
                    emit(I, 0.0f);
                }
                else if (!in1 && in2)
                {
                    // entering: add intersection + Q
                    float t = VP / (VP - VQ);
                    glm::vec3 I = P + t * (Q - P);
                    I = snapToEquator(I, r);
                    emit(I, 0.0f);
                    emit(Q, VQ);
                }
                else
                {
                    // outside -> outside : emit nothing
                }
            }
        };

        std::vector<glm::vec3> tmpP;
        std::vector<float> tmpV;
        clipOnce(poly, val, tmpP, tmpV);

        // 結果
        outPoly = tmpP;
    }

    // polygon(3 or 4) -> triangles fan
    static void triangulatePolyFan(const std::vector<glm::vec3> &poly,
                                   std::vector<std::array<glm::vec3, 3>> &outTris)
    {
        outTris.clear();
        if (poly.size() < 3)
            return;
        for (size_t i = 1; i + 1 < poly.size(); ++i)
        {
            outTris.push_back({poly[0], poly[i], poly[i + 1]});
        }
    }

    // ---- cubed-sphere 半球キャップ生成 ----
    // div: 1面の分割数（例: quality*2〜quality*4 あたりが無難）
    static void buildHemisphereCapCubedSphere(bool top,
                                              int div,
                                              float radius,
                                              float halfBodyLength, // l
                                              std::vector<VertexPN> &outVerts,
                                              std::vector<uint32_t> &outIndices)
    {
        outVerts.clear();
        outIndices.clear();

        const float r = radius;
        const float l = halfBodyLength;
        const glm::vec3 center = top ? glm::vec3(0, 0, +l) : glm::vec3(0, 0, -l);

        // 6面全部作って半球でクリップ（実装を単純化）
        const CubeFace faces[6] = {
            CubeFace::PX, CubeFace::NX, CubeFace::PY, CubeFace::NY, CubeFace::PZ, CubeFace::NZ};

        std::unordered_map<VKey, uint32_t, VKeyHash> welded;

        auto emitTriLocal = [&](const glm::vec3 &aL, const glm::vec3 &bL, const glm::vec3 &cL)
        {
            // aL,bL,cL は center を引いた “local”（球中心基準）
            // → pos は center + aL, normal は aL/r
            glm::vec3 na = glm::normalize(aL);
            glm::vec3 nb = glm::normalize(bL);
            glm::vec3 nc = glm::normalize(cL);

            uint32_t ia = addVertexWelded(outVerts, welded, center + aL, na);
            uint32_t ib = addVertexWelded(outVerts, welded, center + bL, nb);
            uint32_t ic = addVertexWelded(outVerts, welded, center + cL, nc);

            outIndices.push_back(ia);
            outIndices.push_back(ib);
            outIndices.push_back(ic);
        };

        // 面ごとに格子生成 -> 2三角形/セル -> 半球クリップ -> 出力
        for (CubeFace f : faces)
        {
            for (int y = 0; y < div; ++y)
            {
                float v0 = -1.0f + 2.0f * (float)y / (float)div;
                float v1 = -1.0f + 2.0f * (float)(y + 1) / (float)div;

                for (int x = 0; x < div; ++x)
                {
                    float u0 = -1.0f + 2.0f * (float)x / (float)div;
                    float u1 = -1.0f + 2.0f * (float)(x + 1) / (float)div;

                    // 4 corners on cube
                    glm::vec3 c00 = cubeToDir(f, u0, v0);
                    glm::vec3 c10 = cubeToDir(f, u1, v0);
                    glm::vec3 c01 = cubeToDir(f, u0, v1);
                    glm::vec3 c11 = cubeToDir(f, u1, v1);

                    // project to sphere directions
                    glm::vec3 d00 = glm::normalize(c00);
                    glm::vec3 d10 = glm::normalize(c10);
                    glm::vec3 d01 = glm::normalize(c01);
                    glm::vec3 d11 = glm::normalize(c11);

                    // local positions on sphere
                    glm::vec3 p00 = d00 * r;
                    glm::vec3 p10 = d10 * r;
                    glm::vec3 p01 = d01 * r;
                    glm::vec3 p11 = d11 * r;

                    // two tris: (00,10,11), (00,11,01)
                    glm::vec3 A[2][3] = {
                        {p00, p10, p11},
                        {p00, p11, p01}};

                    for (int t = 0; t < 2; ++t)
                    {
                        std::vector<glm::vec3> poly;
                        clipTriToHemisphere(A[t][0], A[t][1], A[t][2], top, r, poly);

                        std::vector<std::array<glm::vec3, 3>> tris;
                        triangulatePolyFan(poly, tris);

                        for (auto &tri : tris)
                        {
                            // tri は local coords。赤道は snap 済み。
                            emitTriLocal(tri[0], tri[1], tri[2]);
                        }
                    }
                }
            }
        }
    }

    // cubed-sphere の赤道リングを XY 平面投影で構築
    // div: 1面の分割数
    static std::vector<glm::vec2> buildCubedSphereEquatorRingXY(int div)
    {
        std::vector<glm::vec2> ring;
        ring.reserve(4 * div);

        auto push = [&](float x, float y)
        {
            glm::vec3 p = glm::normalize(glm::vec3(x, y, 0.0f));
            ring.emplace_back(p.x, p.y);
        };

        // +X face: (1, u) , u: -1 -> +1
        for (int i = 0; i < div; ++i) {
            float u = -1.0f + 2.0f * (float(i) / float(div));
            push( 1.0f,  u);
        }

        // +Y face: (u, 1) , u: +1 -> -1  ← ここを逆向きに
        for (int i = 0; i < div; ++i) {
            float u =  1.0f - 2.0f * (float(i) / float(div));
            push( u,  1.0f);
        }

        // -X face: (-1, u) , u: +1 -> -1
        for (int i = 0; i < div; ++i) {
            float u =  1.0f - 2.0f * (float(i) / float(div));
            push(-1.0f,  u);
        }

        // -Y face: (u, -1) , u: -1 -> +1
        for (int i = 0; i < div; ++i) {
            float u = -1.0f + 2.0f * (float(i) / float(div));
            push( u, -1.0f);
        }

        return ring; // 閉じていない（最後=最初ではない）
    }

    // ---- メイン関数：Capsule メッシュ生成 ----
    static void initUnitCapsulePartsForQuality(
        int quality,
        Mesh &bodyMesh,
        Mesh &capTopMesh,
        Mesh &capBottomMesh)
    {
        // ========= パラメータ =========
        const int capsule_quality = quality;  // 1〜3
        const float length = 2.0f;          // 平行部長さ
        const float radius = 1.0f;          // 半径

        const float l = length * 0.5f; // cylinder は z = ±l (=±1)
        const float r = radius;

        // 円筒用
        std::vector<VertexPN> cylVerts;
        std::vector<uint32_t> cylIndices;

        auto addCylVertex = [&](float x, float y, float z,
                                float nx, float ny, float nz) -> uint32_t
        {
            VertexPN v;
            v.pos = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(nx, ny, nz));
            cylVerts.push_back(v);
            return static_cast<uint32_t>(cylVerts.size() - 1);
        };

        // 上キャップ用
        std::vector<VertexPN> capTopVerts;
        std::vector<uint32_t> capTopIndices;

        // 下キャップ用
        std::vector<VertexPN> capBottomVerts;
        std::vector<uint32_t> capBottomIndices;

        // メッシュの品質（分割数）調整
        // divは偶数である必要あり。奇数だとキャップと円筒との間に隙間ができる。
        const int div = capsule_quality * 2 + 2;

        // =================================================
        // 1. 円筒本体 (unit cylinder)
        // =================================================
        const auto ring = buildCubedSphereEquatorRingXY(div);
        const int m = (int)ring.size();

        bool firstPair = true;
        uint32_t prevTop = 0, prevBottom = 0;

        for (int i = 0; i <= m; ++i)
        {
            const glm::vec2 xy = ring[i % m]; // i==m で閉じる
            const float x = xy.x;
            const float y = xy.y;

            // 位置：円筒、法線：半径方向（z=0）
            uint32_t vTop = addCylVertex(x * r, y * r, +l, x, y, 0.0f);
            uint32_t vBottom = addCylVertex(x * r, y * r, -l, x, y, 0.0f);

            if (!firstPair)
            {
                cylIndices.push_back(prevTop);
                cylIndices.push_back(prevBottom);
                cylIndices.push_back(vTop);

                cylIndices.push_back(prevBottom);
                cylIndices.push_back(vBottom);
                cylIndices.push_back(vTop);
            }
            else
            {
                firstPair = false;
            }

            prevTop = vTop;
            prevBottom = vBottom;
        }

        // =================================================
        // 2. 上部キャップ (cubed-sphere hemisphere)
        // =================================================
        buildHemisphereCapCubedSphere(
            /*top=*/true,
            div,
            /*radius=*/r,
            /*halfBodyLength=*/l,
            capTopVerts,
            capTopIndices
        );

        // =================================================
        // 3. 下部キャップ (cubed-sphere hemisphere)
        // =================================================
        buildHemisphereCapCubedSphere(
            /*top=*/false,
            div,
            /*radius=*/r,
            /*halfBodyLength=*/l,
            capBottomVerts,
            capBottomIndices
        );

        // =================================================
        // 4. Mesh へ転送
        // =================================================
        initMeshFromVectors(bodyMesh, cylVerts, cylIndices);
        initMeshFromVectors(capTopMesh, capTopVerts, capTopIndices);
        initMeshFromVectors(capBottomMesh, capBottomVerts, capBottomIndices);
    }

    void DrawstuffApp::createPrimitiveMeshes()
    {
        // 頂点配列: position + normal (+ texcoord)
        struct Vertex
        {
            float pos[3];
            float normal[3];
        };

        // 6面 × 4頂点 = 24 頂点
        std::vector<Vertex> vertices = {

            // +X 面
            {{+0.5f, -0.5f, -0.5f}, {+1, 0, 0}},
            {{+0.5f, +0.5f, -0.5f}, {+1, 0, 0}},
            {{+0.5f, +0.5f, +0.5f}, {+1, 0, 0}},
            {{+0.5f, -0.5f, +0.5f}, {+1, 0, 0}},

            // -X 面
            {{-0.5f, -0.5f, +0.5f}, {-1, 0, 0}},
            {{-0.5f, +0.5f, +0.5f}, {-1, 0, 0}},
            {{-0.5f, +0.5f, -0.5f}, {-1, 0, 0}},
            {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}},

            // +Y 面
            {{-0.5f, +0.5f, -0.5f}, {0, +1, 0}},
            {{-0.5f, +0.5f, +0.5f}, {0, +1, 0}},
            {{+0.5f, +0.5f, +0.5f}, {0, +1, 0}},
            {{+0.5f, +0.5f, -0.5f}, {0, +1, 0}},

            // -Y 面
            {{-0.5f, -0.5f, +0.5f}, {0, -1, 0}},
            {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}},
            {{+0.5f, -0.5f, -0.5f}, {0, -1, 0}},
            {{+0.5f, -0.5f, +0.5f}, {0, -1, 0}},

            // +Z 面
            {{-0.5f, -0.5f, +0.5f}, {0, 0, +1}},
            {{+0.5f, -0.5f, +0.5f}, {0, 0, +1}},
            {{+0.5f, +0.5f, +0.5f}, {0, 0, +1}},
            {{-0.5f, +0.5f, +0.5f}, {0, 0, +1}},

            // -Z 面
            {{+0.5f, -0.5f, -0.5f}, {0, 0, -1}},
            {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}},
            {{-0.5f, +0.5f, -0.5f}, {0, 0, -1}},
            {{+0.5f, +0.5f, -0.5f}, {0, 0, -1}},
        };
        // インデックス配列: 6面 × 2三角形 × 3頂点 = 36 インデックス
        std::vector<uint32_t> indices = {
            // +X
            0, 1, 2, 0, 2, 3,

            // -X
            4, 5, 6, 4, 6, 7,

            // +Y
            8, 9, 10, 8, 10, 11,

            // -Y
            12, 13, 14, 12, 14, 15,

            // +Z
            16, 17, 18, 16, 18, 19,

            // -Z
            20, 21, 22, 20, 22, 23};

        glGenVertexArrays(1, &meshBox_.vao);
        glBindVertexArray(meshBox_.vao);

        glGenBuffers(1, &meshBox_.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshBox_.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(Vertex),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glGenBuffers(1, &meshBox_.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshBox_.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) vec3 aPos;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, pos)));

        // layout(location = 1) vec3 aNormal;
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, normal)));

        meshBox_.indexCount = static_cast<GLsizei>(indices.size());

        glBindVertexArray(0);

        // --- ここから sphere 用メッシュ生成 ---

        initSphereMeshForQuality(1, meshSphere_[1]);
        initSphereMeshForQuality(2, meshSphere_[2]);
        initSphereMeshForQuality(3, meshSphere_[3]);

        // --- ここから cylinder 用メッシュ生成 ---
        initCylinderMeshForQuality(1, meshCylinder_[1]);
        initCylinderMeshForQuality(2, meshCylinder_[2]);
        initCylinderMeshForQuality(3, meshCylinder_[3]);

        // --- ここから capsule 用メッシュ生成 ---
        initUnitCapsulePartsForQuality(1, meshCapsuleCylinder_[1],
                                        meshCapsuleCapTop_[1],
                                        meshCapsuleCapBottom_[1]);
        initUnitCapsulePartsForQuality(2, meshCapsuleCylinder_[2],
                                        meshCapsuleCapTop_[2],
                                        meshCapsuleCapBottom_[2]);
        initUnitCapsulePartsForQuality(3, meshCapsuleCylinder_[3],
                                        meshCapsuleCapTop_[3],
                                        meshCapsuleCapBottom_[3]);

        initTriangleMesh();
        initTrianglesBatchMesh();

        initLineMesh();
        initPyramidMesh();
    }
}