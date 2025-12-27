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

    static void initUnitCapsuleParts(
        Mesh &bodyMesh,
        Mesh &capTopMesh,
        Mesh &capBottomMesh)
    {
        constexpr float PI = glm::pi<float>();
        using std::cos;
        using std::sin;

        // ========= パラメータ =========
        constexpr int capsule_quality = 3;  // 1〜3
        const int n = capsule_quality * 12; // 円周分割数（4の倍数）
        const float length = 2.0f;          // 平行部長さ
        const float radius = 1.0f;          // 半径

        const float l = length * 0.5f; // cylinder は z = ±l (=±1)
        const float r = radius;

        const float a = 2.0f * static_cast<float>(PI) / static_cast<float>(n);
        const float sa = std::sin(a);
        const float ca = std::cos(a);

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

        auto addCapTopVertex = [&](float x, float y, float z,
                                   float nx, float ny, float nz) -> uint32_t
        {
            VertexPN v;
            v.pos = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(nx, ny, nz));
            capTopVerts.push_back(v);
            return static_cast<uint32_t>(capTopVerts.size() - 1);
        };

        // 下キャップ用
        std::vector<VertexPN> capBottomVerts;
        std::vector<uint32_t> capBottomIndices;

        auto addCapBottomVertex = [&](float x, float y, float z,
                                      float nx, float ny, float nz) -> uint32_t
        {
            VertexPN v;
            v.pos = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(nx, ny, nz));
            capBottomVerts.push_back(v);
            return static_cast<uint32_t>(capBottomVerts.size() - 1);
        };

        // =================================================
        // 1. 円筒本体 (unit cylinder)
        // =================================================
        float ny = 1.0f;
        float nz = 0.0f;
        float tmp;
        bool firstPair = true;
        uint32_t prevTop = 0, prevBottom = 0;

        for (int i = 0; i <= n; ++i)
        {
            float nx0 = ny;
            float ny0 = nz;
            float nz0 = 0.0f;

            uint32_t vTop = addCylVertex(ny * r, nz * r, +l, nx0, ny0, nz0);
            uint32_t vBottom = addCylVertex(ny * r, nz * r, -l, nx0, ny0, nz0);

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

            // rotate ny,nz
            tmp = ca * ny - sa * nz;
            nz = sa * ny + ca * nz;
            ny = tmp;
        }

        // =================================================
        // 2. 上部キャップ (z >= +1)
        // =================================================
        float start_nx = 0.0f;
        float start_ny = 1.0f;

        for (int j = 0; j < (n / 4); ++j)
        {
            float start_nx2 = ca * start_nx + sa * start_ny;
            float start_ny2 = -sa * start_nx + ca * start_ny;

            float nx = start_nx;
            float nyc = start_ny;
            float nzc = 0.0f;

            float nx2 = start_nx2;
            float ny2c = start_ny2;
            float nz2c = 0.0f;

            firstPair = true;
            uint32_t prev0 = 0, prev1 = 0;

            for (int i = 0; i <= n; ++i)
            {
                // 元コードの頂点・法線
                uint32_t v0 = addCapTopVertex(
                    ny2c * r, nz2c * r, l + nx2 * r,
                    ny2c, nz2c, nx2);

                uint32_t v1 = addCapTopVertex(
                    nyc * r, nzc * r, l + nx * r,
                    nyc, nzc, nx);

                if (!firstPair)
                {
                    capTopIndices.push_back(prev0);
                    capTopIndices.push_back(prev1);
                    capTopIndices.push_back(v0);

                    capTopIndices.push_back(prev1);
                    capTopIndices.push_back(v1);
                    capTopIndices.push_back(v0);
                }
                else
                {
                    firstPair = false;
                }

                prev0 = v0;
                prev1 = v1;

                // rotate n, n2
                tmp = ca * nyc - sa * nzc;
                nzc = sa * nyc + ca * nzc;
                nyc = tmp;

                tmp = ca * ny2c - sa * nz2c;
                nz2c = sa * ny2c + ca * nz2c;
                ny2c = tmp;
            }

            start_nx = start_nx2;
            start_ny = start_ny2;
        }

        // =================================================
        // 3. 下部キャップ (z <= -1)
        // =================================================
        start_nx = 0.0f;
        start_ny = 1.0f;

        for (int j = 0; j < (n / 4); ++j)
        {
            float start_nx2 = ca * start_nx - sa * start_ny;
            float start_ny2 = sa * start_nx + ca * start_ny;

            float nx = start_nx;
            float nyc = start_ny;
            float nzc = 0.0f;

            float nx2 = start_nx2;
            float ny2c = start_ny2;
            float nz2c = 0.0f;

            firstPair = true;
            uint32_t prev0 = 0, prev1 = 0;

            for (int i = 0; i <= n; ++i)
            {
                uint32_t v0 = addCapBottomVertex(
                    nyc * r, nzc * r, -l + nx * r,
                    nyc, nzc, nx);

                uint32_t v1 = addCapBottomVertex(
                    ny2c * r, nz2c * r, -l + nx2 * r,
                    ny2c, nz2c, nx2);

                if (!firstPair)
                {
                    capBottomIndices.push_back(prev0);
                    capBottomIndices.push_back(prev1);
                    capBottomIndices.push_back(v0);

                    capBottomIndices.push_back(prev1);
                    capBottomIndices.push_back(v1);
                    capBottomIndices.push_back(v0);
                }
                else
                {
                    firstPair = false;
                }

                prev0 = v0;
                prev1 = v1;

                // rotate n, n2
                tmp = ca * nyc - sa * nzc;
                nzc = sa * nyc + ca * nzc;
                nyc = tmp;

                tmp = ca * ny2c - sa * nz2c;
                nz2c = sa * ny2c + ca * nz2c;
                ny2c = tmp;
            }

            start_nx = start_nx2;
            start_ny = start_ny2;
        }

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
        initUnitCapsuleParts(meshCapsuleBody_,
                             meshCapsuleCapTop_,
                             meshCapsuleCapBottom_);

        initTriangleMesh();
        initTrianglesBatchMesh();

        initLineMesh();
        initPyramidMesh();
    }
}