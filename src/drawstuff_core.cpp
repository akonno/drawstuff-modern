// drawstuff_core.cpp - implementation file for core parts of drawstuff-modern
//
// This file is part of drawstuff-modern, a modern reimplementation inspired by
// the drawstuff library distributed with the Open Dynamics Engine (ODE).
//
// The original drawstuff was developed by Russell L. Smith.
// This implementation has been substantially rewritten and redesigned.
//
// Copyright (c) 2025 Akihisa Konno
// Released under the BSD 3-Clause License.
// See the LICENSE file for details.

#ifdef WIN32
#include <windows.h>
#endif

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
// 必要に応じて <GL/gl.h> など既存の include も

const char *DEFAULT_PATH_TO_TEXTURES = "../textures/";
// drawstuff_core.cpp

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include "drawstuff_core.hpp"
#include "mesh_utils.hpp"

// ==============================================================
// ds_internal namespace functions
// =============================================================
namespace ds_internal
{
    // =============================================================
    // Utility functions
    // =============================================================
    // Report error and exit
    void fatalError(const char *msg, ...)
    {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        exit(EXIT_FAILURE);
    }

    // Report error and abort with core dump (for debugging)
    void internalError(const char *msg, ...)
    {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        abort();
    }

    // skip over whitespace and comments in a stream.
    void skipWhiteSpace(const char *filename, FILE *f)
    {
        int c, d;
        for (;;)
        {
            c = fgetc(f);
            if (c == EOF)
                fatalError("unexpected end of file in \"%s\"", filename);

            // skip comments
            if (c == '#')
            {
                do
                {
                    d = fgetc(f);
                    if (d == EOF)
                        fatalError("unexpected end of file in \"%s\"", filename);
                } while (d != '\n');
                continue;
            }

            if (c > ' ')
            {
                ungetc(c, f);
                return;
            }
        }
    }

    // read a number from a stream, this return 0 if there is none (that's okay
    // because 0 is a bad value for all PPM numbers anyway).
    int readNumber(const char *filename, FILE *f)
    {
        int c, n = 0;
        for (;;)
        {
            c = fgetc(f);
            if (c == EOF)
                fatalError("unexpected end of file in \"%s\"", filename);
            if (c >= '0' && c <= '9')
                n = n * 10 + (c - '0');
            else
            {
                ungetc(c, f);
                return n;
            }
        }
    }
    // ====================================================================
    // Image and texture classes
    // ====================================================================
    typedef unsigned char byte;
    class Image
    {
        int image_width, image_height;
        byte *image_data;

    public:
        Image(const char *filename)
        {
            FILE *f = fopen(filename, "rb");
            if (!f)
                fatalError("Can't open image file `%s'", filename);

            // read in header
            if (fgetc(f) != 'P' || fgetc(f) != '6')
                fatalError("image file \"%s\" is not a binary PPM (no P6 header)", filename);
            skipWhiteSpace(filename, f);

            // read in image parameters
            image_width = readNumber(filename, f);
            skipWhiteSpace(filename, f);
            image_height = readNumber(filename, f);
            skipWhiteSpace(filename, f);
            int max_value = readNumber(filename, f);

            // check values
            if (image_width < 1 || image_height < 1)
                fatalError("bad image file \"%s\"", filename);
            if (max_value != 255)
                fatalError("image file \"%s\" must have color range of 255", filename);

            // read either nothing, LF (10), or CR,LF (13,10)
            int c = fgetc(f);
            if (c == 10)
            {
                // LF
            }
            else if (c == 13)
            {
                // CR
                c = fgetc(f);
                if (c != 10)
                    ungetc(c, f);
            }
            else
                ungetc(c, f);

            // read in rest of data
            image_data = new byte[image_width * image_height * 3];
            if (fread(image_data, image_width * image_height * 3, 1, f) != 1)
                fatalError("Can not read data from image file `%s'", filename);
            fclose(f);
        }
        // load from PPM file
        ~Image()
        {
            delete[] image_data;
        }
        int width() { return image_width; }
        int height() { return image_height; }
        byte *data() { return image_data; }
    };

    //***************************************************************************
    // Texture object.
    class Texture
    {
        Image *image = nullptr;
        GLuint name = 0;

    public:
        Texture(const char *filename)
        {
            image = new Image(filename);

            glGenTextures(1, &name);
            glBindTexture(GL_TEXTURE_2D, name);

            // ピクセルアンパック状態
            glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

            // 画像サイズ
            const GLint width = static_cast<GLint>(image->width());
            const GLint height = static_cast<GLint>(image->height());

            // テクスチャ本体を GPU に送る（ここでは RGB8 固定）
            glTexImage2D(
                GL_TEXTURE_2D,
                0,       // level
                GL_RGB8, // internal format（コアでも推奨されるサイズ付き）
                width,
                height,
                0,                // border (must be 0)
                GL_RGB,           // 画像側のフォーマット
                GL_UNSIGNED_BYTE, // 画像側の型
                image->data());

            // ミップマップを自前で生成（gluBuild2DMipmaps の代替）
            glGenerateMipmap(GL_TEXTURE_2D);

            // テクスチャパラメータ
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);

            // 固定機能のテクスチャ環境は core では無効なので一切設定しない
            // glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, ...); は削除

            glBindTexture(GL_TEXTURE_2D, 0);
        }

        ~Texture()
        {
            if (name != 0)
            {
                glDeleteTextures(1, &name);
                name = 0;
            }
            delete image;
            image = nullptr;
        }

        // modulate 引数は互換のため残すが、固定機能の GL_MODULATE/GL_DECAL は core では使えない。
        // 実際の「乗算するかどうか」は、シェーダ側の uniform で制御する前提。
        void bind(int /*modulate*/)
        {
            glBindTexture(GL_TEXTURE_2D, name);
        }
    };

    constexpr int DS_NUMTEXTURES = 4; // number of standard textures
    std::array<std::unique_ptr<Texture>, DS_NUMTEXTURES + 1> texture;

    // ============================================================================
    // TriMesh 用高速描画 API 実装
    // ============================================================================
    // GPU 側に MeshPN データを転送して Mesh を構築
    void buildTrianglesMeshFromMeshPN(
        Mesh &outMesh,
        MeshPN &meshPN)
    {
        const std::vector<VertexPN> &verts = meshPN.vertices;
        const std::vector<uint32_t> &indices = meshPN.indices;

        // 既存リソースの破棄
        if (outMesh.vao)
        {
            glDeleteVertexArrays(1, &outMesh.vao);
            outMesh.vao = 0;
        }
        if (outMesh.vbo)
        {
            glDeleteBuffers(1, &outMesh.vbo);
            outMesh.vbo = 0;
        }
        if (outMesh.ebo)
        {
            glDeleteBuffers(1, &outMesh.ebo);
            outMesh.ebo = 0;
        }

        // VAO/VBO/EBO 設定
        glGenVertexArrays(1, &outMesh.vao);
        glBindVertexArray(outMesh.vao);

        glGenBuffers(1, &outMesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, outMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     verts.size() * sizeof(VertexPN),
                     verts.data(),
                     GL_STATIC_DRAW);

        glGenBuffers(1, &outMesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outMesh.ebo);
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

        outMesh.indexCount = static_cast<GLsizei>(indices.size());
        outMesh.primitive = GL_TRIANGLES;

        glBindVertexArray(0);
    }

    // CPU 側：頂点配列データから MeshPN を構築
    MeshPN buildTrianglesMeshPNFromVerticesAndIndices(
        const std::vector<float> &vertices,   // x,y,z,...
        const std::vector<uint32_t> &indices) // 0-based index
    {
        MeshPN outMeshPN;
        // スムーズシェーディング版の VertexPN＋indices を構築
        // MeshPN meshPN = buildSmoothVertexPNFromVerticesAndIndices(vertices, indices);
        // 折り目付きシェーディング版の VertexPN＋indices を構築
        outMeshPN = buildCreasedVertexPNFromVerticesAndIndices(vertices, indices, 60.0f);

        // OpenGL の VAO/VBO/EBO を構築
        return outMeshPN;
    }

    // ============================================================================
    // Mesh generation utility members and functions
    // ============================================================================

    // =================================================
    // インスタンス描画用バッファ
    std::vector<InstanceBasic> sphereInstances_;
    std::vector<InstanceBasic> boxInstances_;
    std::vector<InstanceBasic> cylinderInstances_;
    std::vector<InstanceBasic> capsuleCapTopInstances_;
    std::vector<InstanceBasic> capsuleCapBottomInstances_;
    std::vector<InstanceBasic> capsuleCylinderInstances_;

    GLuint g_sphereInstanceVBO = 0;
    GLuint g_boxInstanceVBO = 0;
    GLuint g_cylinderInstanceVBO = 0;
    GLuint g_capsuleCapTopInstanceVBO = 0;
    GLuint g_capsuleCapBottomInstanceVBO = 0;
    GLuint g_capsuleCylinderInstanceVBO = 0;

    // ================ DrawstuffApp implementation =================
    DrawstuffApp &DrawstuffApp::instance()
    {
        static DrawstuffApp inst;
        return inst;
    }

    DrawstuffApp::DrawstuffApp()
        : current_state(SIM_STATE_NOT_STARTED),
        use_textures(true),
        use_shadows(true),
        writeframes(false),
        pausemode(false),
        view_xyz{{2.0f, 0.0f, 1.0f}},
        view_hpr{{180.0f, 0.0f, 0.0f}},
        current_color(1.0f, 1.0f, 1.0f, 1.0f),
        texture_id(0),
        callbacks_(nullptr)
    {
        // ここで旧 initMotionModel() の中身をそのまま書いてもよいし、
        // 別メンバ関数に切り出してもよい。
    }

    DrawstuffApp::~DrawstuffApp() = default;

    void DrawstuffApp::initMotionModel()
    {
        view_xyz = {{2.0f, 0.0f, 1.0f}};
        view_hpr = {{180.0f, 0.0f, 0.0f}};
    }

    void DrawstuffApp::wrapCameraAngles()
    {
        for (int i = 0; i < 3; i++)
        {
            while (view_hpr[i] > 180)
                view_hpr[i] -= 360;
            while (view_hpr[i] < -180)
                view_hpr[i] += 360;
        }
    }

    void DrawstuffApp::getViewpoint(float xyz[3], float hpr[3])
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            fatalError("DrawstuffApp::getViewpoint() called before simulation started");
        if (xyz)
        {
            xyz[0] = view_xyz[0];
            xyz[1] = view_xyz[1];
            xyz[2] = view_xyz[2];
        }
        if (hpr)
        {
            hpr[0] = view_hpr[0];
            hpr[1] = view_hpr[1];
            hpr[2] = view_hpr[2];
        }
    }

    void DrawstuffApp::setViewpoint(const float xyz[3], const float hpr[3])
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            fatalError("DrawstuffApp::setViewpoint() called before simulation started");
        if (xyz)
        {
            view_xyz[0] = xyz[0];
            view_xyz[1] = xyz[1];
            view_xyz[2] = xyz[2];
        }
        if (hpr)
        {
            view_hpr[0] = hpr[0];
            view_hpr[1] = hpr[1];
            view_hpr[2] = hpr[2];
            wrapCameraAngles();
        }
    }

    void DrawstuffApp::storeColor(const float r, const float g, const float b, const float alpha)
    {
        current_color[0] = r;
        current_color[1] = g;
        current_color[2] = b;
        current_color[3] = alpha;
    }

    // メンバ: 現在の色（もともと float[4] ならそこに合わせてください）
    float current_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    void DrawstuffApp::setColor(const float r, const float g, const float b, const float alpha)
    {
        // 3.3 core 版では固定機能マテリアルは使わない
        // glMaterial* はすべて削除する

        current_color[0] = r;
        current_color[1] = g;
        current_color[2] = b;
        current_color[3] = alpha;
    }

    void DrawstuffApp::setTexture(int texnum)
    {
        texture_id = texnum;
    }

    void DrawstuffApp::bindTextureUnit0(const int texId)
    {
        if (texId < 0 || !texture[texId])
        {
            // 無効なら何もしない
            return;
        }

        if (texId == currentBoundTextureId_)
        {
            // すでに同じテクスチャが GL_TEXTURE0 にバインドされている
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        texture[texId]->bind(0); // 内部で glBindTexture(GL_TEXTURE_2D, ...) している前提
        currentBoundTextureId_ = texId;
    }

    void DrawstuffApp::applyMaterials()
    {
        setColor(current_color[0], current_color[1], current_color[2], current_color[3]);

        if (current_color[3] < 1)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    // ==============================================================
    // drawing functions
    // ==============================================================
    void DrawstuffApp::drawMeshBasic(
        const Mesh &mesh,
        const glm::mat4 &model,
        const glm::vec4 &color)
    {
        glm::mat4 shadowMvp = proj_ * view_ * model;

        glUseProgram(programBasic_);
        glUniform3f(uLightDir_, lightDir_.x, lightDir_.y, lightDir_.z);
        glUniformMatrix4fv(uMVP_, 1, GL_FALSE, glm::value_ptr(shadowMvp));
        glUniform4fv(uColor_, 1, glm::value_ptr(color));
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(model));

        // テクスチャスケール（模様の大きさ）適当に調整
        glUniform1f(uTexScale_, 0.5f); // 0.1〜2.0 くらいを試して好みで

        // ★ テクスチャの ON/OFF
        if (use_textures && texture[DS_WOOD]) // 例として木目テクスチャを使う場合
        {
            glUniform1i(uUseTex_, GL_TRUE);

            glActiveTexture(GL_TEXTURE0);
            bindTextureUnit0(DS_WOOD);
            glUniform1i(uTex_, 0);     // sampler2D uTex はテクスチャユニット0を参照
        }
        else
        {
            glUniform1i(uUseTex_, GL_FALSE);
        }
        glBindVertexArray(mesh.vao);
        if (mesh.ebo != 0)
        {
            glDrawElements(mesh.primitive,
                           mesh.indexCount,
                           GL_UNSIGNED_INT,
                           nullptr);
        }
        else
        {
            // インデックスなし（VBOを先頭から indexCount 頂点ぶん）
            glDrawArrays(mesh.primitive,
                         0,
                         mesh.indexCount);
        }
        glBindVertexArray(0);

        glUseProgram(0);
    }

    void DrawstuffApp::drawShadowMesh(
        const Mesh &mesh,
        const glm::mat4 &model)
    {
        if (!use_shadows)
            return;

        glm::mat4 shadowModel = shadowProject_ * model;
        glm::mat4 mvp = proj_ * view_ * shadowModel;

        glUseProgram(programShadow_);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);

        glDisable(GL_BLEND); // ここは「上書き型の影」でいく前提

        // 行列・共通パラメータ
        glUniformMatrix4fv(uShadowMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(uShadowModel_, 1, GL_FALSE, glm::value_ptr(shadowModel));

        glUniform2f(uGroundScale_, ground_scale, ground_scale);
        glUniform2f(uGroundOffset_, ground_ofsx, ground_ofsy);
        glUniform1f(uShadowIntensity_, SHADOW_INTENSITY);

        if (use_textures)
        {
            // ★ テクスチャあり：旧 setShadowDrawingMode の「if (use_textures)」相当
            glUniform1i(uShadowUseTex_, GL_TRUE);

            glActiveTexture(GL_TEXTURE0);
            bindTextureUnit0(DS_GROUND);
            glUniform1i(uGroundTex_, 0);
        }
        else
        {
            // ★ テクスチャなし：旧コードの else 分岐相当
            glUniform1i(uShadowUseTex_, GL_FALSE);
            // GROUND_R/G/B は既存の地面色定数を流用
            glUniform3f(uGroundColor_, GROUND_R, GROUND_G, GROUND_B);
        }

        glBindVertexArray(mesh.vao);
        if (mesh.ebo != 0) {
            glDrawElements(mesh.primitive, mesh.indexCount,
                           GL_UNSIGNED_INT, nullptr);
        }
        else {
            glDrawArrays(mesh.primitive, 0, mesh.indexCount);
        }
        glBindVertexArray(0);

        glDisable(GL_POLYGON_OFFSET_FILL);
        glUseProgram(0);
    }

    void DrawstuffApp::drawSky(const float view_xyz[3])
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        initSkyProgram();
        initSkyMesh();

        // 元コードと同じ offset ロジック
        static float offset = 0.0f;
        offset += 0.002f;
        if (offset > 1.0f)
            offset -= 1.0f;

        // モデル行列：
        //   カメラ位置に合わせて xy をシフト、
        //   z は view_xyz[2] + sky_height の高さに。
        glm::mat4 model(1.0f);
        model = glm::translate(
            model,
            glm::vec3(view_xyz[0],
                      view_xyz[1],
                      view_xyz[2] + sky_height));

        glm::mat4 mvp = proj_ * view_ * model;

        // 「最奥にだけ書く」ための depth range
        glDepthRange(1, 1);

        glUseProgram(programSky_);
        glBindVertexArray(vaoSky_);

        glUniformMatrix4fv(uSkyMVP_, 1, GL_FALSE, glm::value_ptr(mvp));

        // 空色（テクスチャなしのとき用・あるいはテクスチャの色乗算）
        glm::vec4 skyColor(0.0f, 0.5f, 1.0f, 1.0f);
        glUniform4fv(uSkyColor_, 1, glm::value_ptr(skyColor));

        glUniform1f(uSkyScale_, sky_scale);
        glUniform1f(uSkyOffset_, offset);

        if (use_textures)
        {
            glUniform1i(uSkyUseTex_, 1);    // テクスチャを使う
            bindTextureUnit0(DS_SKY);
            glUniform1i(uSkyTex_, 0); // sampler2D uTex にユニット 0 を対応付け
        }
        else
        {
            glUniform1i(uSkyUseTex_, 0); // テクスチャは使わない
            glUniform1i(uSkyTex_, 0);    // sampler2D uTex にユニット 0 を対応付け
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
        glUseProgram(0);

        // depth 状態を元に戻す
        glDepthFunc(GL_LESS);
        glDepthRange(0, 1);
    }

    void DrawstuffApp::drawGround()
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        initGroundMesh();

        glm::mat4 model(1.0f);
        glm::mat4 mvp = proj_ * view_ * model;

        glUseProgram(programGround_);
        glBindVertexArray(vaoGround_);

        glUniformMatrix4fv(uGroundModel_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(uGroundMVP_, 1, GL_FALSE, glm::value_ptr(mvp));

        glm::vec4 groundColor;
        if (use_textures) {
            groundColor = glm::vec4(1.0f);
            glUniform1i(uGroundUseTex_, 1); // テクスチャ有効

            // テクスチャをユニット0に bind
            bindTextureUnit0(DS_GROUND);
            glUniform1i(uGroundTex_, 0);
        }
        else
        {
            groundColor = glm::vec4(GROUND_R, GROUND_G, GROUND_B, 1.0f);
            glUniform1i(uGroundUseTex_, 0); // テクスチャ無効
        }
        glUniform4fv(uGroundColor_, 1, glm::value_ptr(groundColor));

        // 元のパラメータを uniform で渡す
        glUniform1f(uGroundScale_, ground_scale);
        glUniform2f(uGroundOffset_, ground_ofsx, ground_ofsy);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
        glUseProgram(0);
    }

    void DrawstuffApp::drawPyramidGrid()
    {
        // 深度テストは有効にしておく
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // 共通描画状態（programBasic_, uLightDir, など）
        applyMaterials();

        // ピラミッド Mesh を必要に応じて初期化
        initPyramidMesh();

        // 補助オブジェクトなのでテクスチャは明示的に OFF にしておく
        // （drawMeshBasic の実装によっては不要だが、元挙動に合わせて上書き）
        glUniform1i(uUseTex_, 0);

        const float kScale = 0.03f; // 元コードの k に相当

        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {
                // 色（元コードと同じルール）
                glm::vec4 color;
                if (i == 1 && j == 0)
                    color = glm::vec4(1, 0, 0, 1); // +X
                else if (i == 0 && j == 1)
                    color = glm::vec4(0, 0, 1, 1); // +Y
                else
                    color = glm::vec4(1, 1, 0, 1); // others

                // モデル行列：平行移動 + スケール
                glm::mat4 model(1.0f);
                model = glm::translate(model,
                                       glm::vec3(static_cast<float>(i),
                                                 static_cast<float>(j),
                                                 0.0f));
                model = glm::scale(model, glm::vec3(kScale));

                // 共通パイプラインで描画
                drawMeshBasic(meshPyramid_, model, color);
                // （座標軸用なので影は描かない運用にしておくのが無難）
                // if (use_shadows) { drawShadowMesh(meshPyramid_, model); }
            }
        }

        // drawMeshBasic 側で VAO / program を片付ける設計なら、ここでの後処理は不要。
        // 念のため状態を戻したいなら以下を入れてもよい。
        // glBindVertexArray(0);
        // glUseProgram(0);
    }

    void DrawstuffApp::drawTriangleCore(const glm::vec3 p[3],
                                        const glm::vec3 &N,
                                        const glm::mat4 &model,
                                        const bool solid)
    {
        VertexPN tri[3];
        tri[0].pos = p[0];
        tri[1].pos = p[1];
        tri[2].pos = p[2];
        tri[0].normal = N;
        tri[1].normal = N;
        tri[2].normal = N;

        // 三角形メッシュの VBO を更新
        glBindBuffer(GL_ARRAY_BUFFER, meshTriangle_.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // あとは他のプリミティブと同じ「基本パイプライン」に投げる
        if (!solid) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        drawMeshBasic(meshTriangle_, model, current_color);
        if (!solid) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // 影（Core パス）
        if (use_shadows && solid) {
            drawShadowMesh(meshTriangle_, model);
        }
    }

    void DrawstuffApp::drawTrianglesBatch(
        const std::vector<VertexPN> &verts,
        const glm::mat4 &model,
        const bool solid)
    {
        if (verts.empty())
            return;

        const std::size_t needed = verts.size();

        glBindBuffer(GL_ARRAY_BUFFER, meshTrianglesBatch_.vbo);

        // 必要ならバッファサイズを拡張
        if (needed > trianglesBatchCapacity_)
        {
            trianglesBatchCapacity_ = needed;
            glBufferData(GL_ARRAY_BUFFER,
                         trianglesBatchCapacity_ * sizeof(VertexPN),
                         nullptr,
                         GL_DYNAMIC_DRAW);
        }

        // 実データを先頭から詰める
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        needed * sizeof(VertexPN),
                        verts.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        meshTrianglesBatch_.indexCount =
            static_cast<GLsizei>(needed); // 頂点数として使う

        // 本体
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        drawMeshBasic(meshTrianglesBatch_, model, current_color);
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // 影
        if (use_shadows && solid)
        {
            drawShadowMesh(meshTrianglesBatch_, model);
        }
    }
    
    // ==============================================================
    // instanced drawing functions
    // ==============================================================
    void uploadInstanceBuffer(GLuint &vbo, const std::vector<InstanceBasic> &instances)
    {
        if (vbo == 0)
        {
            // 念のため。初期化し忘れていた場合に備えるならこう。
            glGenBuffers(1, &vbo);
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        const GLsizeiptr bytes =
            static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceBasic));

        // インスタンスが0個のときでも、一応バッファサイズ0で呼んでおくか、
        // 早期returnするかは好み。ここではそのまま流す。
        glBufferData(
            GL_ARRAY_BUFFER,
            bytes,
            instances.empty() ? nullptr : instances.data(),
            GL_STREAM_DRAW); // 毎フレーム書き換えるので STREAM_DRAW / DYNAMIC_DRAW が無難
    }


    // DrawstuffApp のメンバ関数として
    void DrawstuffApp::setupSphereInstanceAttributes()
    {
        // インスタンス用 VBO がまだなければ作る
        if (g_sphereInstanceVBO == 0)
        {
            glGenBuffers(1, &g_sphereInstanceVBO);
        }

        const GLsizei stride = static_cast<GLsizei>(sizeof(InstanceBasic));

        // 品質ごとに VAO へ同じインスタンス属性を設定
        for (int sphere_quality_ = 1; sphere_quality_ <= 3; ++sphere_quality_)
        {
            Mesh &mesh = meshSphere_[sphere_quality_];

            if (mesh.vao == 0)
            {
                // まだメッシュが初期化されていない場合はスキップ
                continue;
            }

            glBindVertexArray(mesh.vao);

            // この VAO に対して「インスタンス用 VBO を使う」設定を記録する
            glBindBuffer(GL_ARRAY_BUFFER, g_sphereInstanceVBO);

            std::size_t offset = 0;

            // layout(location = 2..5) mat4 iModel
            for (int i = 0; i < 4; ++i)
            {
                const GLuint loc = 2 + i;
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(
                    loc,
                    4,
                    GL_FLOAT,
                    GL_FALSE,
                    stride,
                    reinterpret_cast<const void *>(offset));

                // ★ インスタンスごとに 1 つ進める
                glVertexAttribDivisor(loc, 1);

                offset += sizeof(glm::vec4);
            }

            // layout(location = 6) vec4 iColor
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(
                6,
                4,
                GL_FLOAT,
                GL_FALSE,
                stride,
                reinterpret_cast<const void *>(offsetof(InstanceBasic, color)));
            glVertexAttribDivisor(6, 1);
        }

        // 後始末（どの VAO も bind されていない状態に戻す）
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void DrawstuffApp::setupBoxInstanceAttributes()
    {
        glBindVertexArray(meshBox_.vao);

        // インスタンス用 VBO をバインド
        glBindBuffer(GL_ARRAY_BUFFER, g_boxInstanceVBO);

        GLsizei stride = sizeof(InstanceBasic);
        std::size_t offset = 0;

        // layout(location = 2..5) mat4 iModel
        for (int i = 0; i < 4; ++i)
        {
            GLuint loc = 2 + i;
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(
                loc,
                4,
                GL_FLOAT,
                GL_FALSE,
                stride,
                reinterpret_cast<const void *>(offset));
            glVertexAttribDivisor(loc, 1); // ★ インスタンスごとに1回進める
            offset += sizeof(glm::vec4);
        }

        // layout(location = 6) vec4 iColor
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(
            6,
            4,
            GL_FLOAT,
            GL_FALSE,
            stride,
            reinterpret_cast<const void *>(offsetof(InstanceBasic, color)));
        glVertexAttribDivisor(6, 1);

        glBindVertexArray(0);
    }
    // どこかにある前提
    // extern GLuint g_cylinderInstanceVBO;
    // extern Mesh   meshCylinder_[/* quality index */];

    void DrawstuffApp::setupCylinderInstanceAttributes()
    {
        // インスタンス用 VBO がまだなければ作る
        if (g_cylinderInstanceVBO == 0)
        {
            glGenBuffers(1, &g_cylinderInstanceVBO);
        }

        const GLsizei stride = static_cast<GLsizei>(sizeof(InstanceBasic));

        // 品質ごとに VAO へ同じインスタンス属性を設定
        // ※ 範囲 1..3 はあなたの環境に合わせて変更可
        for (int cylinder_quality_ = 1; cylinder_quality_ <= 3; ++cylinder_quality_)
        {
            Mesh &mesh = meshCylinder_[cylinder_quality_];

            if (mesh.vao == 0)
            {
                // まだメッシュが初期化されていない場合はスキップ
                continue;
            }

            glBindVertexArray(mesh.vao);

            // この VAO に対して「インスタンス用 VBO を使う」設定を記録する
            glBindBuffer(GL_ARRAY_BUFFER, g_cylinderInstanceVBO);

            std::size_t offset = 0;

            // layout(location = 2..5) mat4 iModel
            for (int i = 0; i < 4; ++i)
            {
                const GLuint loc = 2 + i;
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(
                    loc,
                    4,
                    GL_FLOAT,
                    GL_FALSE,
                    stride,
                    reinterpret_cast<const void *>(offset));

                // インスタンスごとに 1 つ進める
                glVertexAttribDivisor(loc, 1);

                offset += sizeof(glm::vec4);
            }

            // layout(location = 6) vec4 iColor
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(
                6,
                4,
                GL_FLOAT,
                GL_FALSE,
                stride,
                reinterpret_cast<const void *>(offsetof(InstanceBasic, color)));
            glVertexAttribDivisor(6, 1);
        }

        // 後始末（どの VAO も bind されていない状態に戻す）
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void DrawstuffApp::setupCapsuleInstanceAttributes()
    {
        if (g_capsuleCapTopInstanceVBO == 0)    glGenBuffers(1, &g_capsuleCapTopInstanceVBO);
        if (g_capsuleCapBottomInstanceVBO == 0) glGenBuffers(1, &g_capsuleCapBottomInstanceVBO);
        if (g_capsuleCylinderInstanceVBO == 0)  glGenBuffers(1, &g_capsuleCylinderInstanceVBO);

        const GLsizei stride = static_cast<GLsizei>(sizeof(InstanceBasic));

        auto setupForMesh = [&](Mesh& mesh, GLuint instanceVbo)
        {
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);

            // もし InstanceBasic の先頭が model でない可能性があるなら offsetof を使う
            std::size_t offset = 0; // = offsetof(InstanceBasic, model); とできるならそれがより安全

            for (int i = 0; i < 4; ++i)
            {
                const GLuint loc = 2 + i;
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(
                    loc, 4, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset));
                glVertexAttribDivisor(loc, 1);
                offset += sizeof(glm::vec4);
            }

            glEnableVertexAttribArray(6);
            glVertexAttribPointer(
                6, 4, GL_FLOAT, GL_FALSE, stride,
                reinterpret_cast<const void*>(offsetof(InstanceBasic, color)));
            glVertexAttribDivisor(6, 1);

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        };

        for (int capsule_quality_ = 1; capsule_quality_ <= 3; ++capsule_quality_)
        {
            Mesh& meshTop      = meshCapsuleCapTop_[capsule_quality_];
            Mesh& meshBottom   = meshCapsuleCapBottom_[capsule_quality_];
            Mesh& meshCylinder = meshCapsuleCylinder_[capsule_quality_];

            if (meshTop.vao == 0 || meshBottom.vao == 0 || meshCylinder.vao == 0)
                continue;

            setupForMesh(meshTop,      g_capsuleCapTopInstanceVBO);
            setupForMesh(meshBottom,   g_capsuleCapBottomInstanceVBO);
            setupForMesh(meshCylinder, g_capsuleCylinderInstanceVBO); // ←ここが重要
        }
    }

    // ==============================================================
    // main simulation loop and related functions
    // ==============================================================
    int DrawstuffApp::runSimulation(const int argc, const char *const argv[],
                                    const int window_width, const int window_height,
                                    const dsFunctions *fn)
    {
        if (current_state != SIM_STATE_NOT_STARTED)
        {
            fatalError("DrawstuffApp::runSimulation() called more than once");
            return -1;
        }
        current_state = SIM_STATE_RUNNING;
        callbacks_storage_ = *fn;
        callbacks_ = &callbacks_storage_;

        int initial_pause = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "-notex") == 0)
                use_textures = false;
            if (strcmp(argv[i], "-noshadow") == 0)
                use_shadows = false;
            if (strcmp(argv[i], "-noshadows") == 0)
                use_shadows = false;
            if (strcmp(argv[i], "-pause") == 0)
                initial_pause = 1;
            if (strcmp(argv[i], "-texturepath") == 0 && ++i < argc)
            {
                if (++i < argc)
                    callbacks_storage_.path_to_textures = argv[i];
            }
        }

        // ウィンドウ生成・GL 初期化・メインループなど、
        // 既存の dsSimulationLoop の残りをここに移していく
        // （当面は OpenGL 1.x のままで OK）
        initMotionModel();
        platformSimulationLoop(window_width, window_height, callbacks_, initial_pause);

        current_state = SIM_STATE_FINISHED;

        return 0;
    }

    void DrawstuffApp::stopSimulation()
    {
        if (current_state != SIM_STATE_RUNNING)
        {
            std::string s = "DrawstuffApp::stopSimulation() called without a running simulation. Current_state = " + std::to_string(static_cast<int>(current_state));
            fatalError(s.c_str());
            return;
        }
        current_state = SIM_STATE_FINISHED; // stopping
    }

    void DrawstuffApp::startGraphics(const int width, const int height, const dsFunctions *fn)
    {
        const char *prefix = DEFAULT_PATH_TO_TEXTURES;
        if (fn->version >= 2 && fn->path_to_textures)
            prefix = fn->path_to_textures;

        // GL 初期化
        gladLoadGL();

        // OpenGL バージョンチェック
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        // OpenGL 3.3 未満ならエラー終了
        if (major < 3 || (major == 3 && minor < 3)) {
            std::cerr << "OpenGL 3.3 or higher is required." << std::endl;
            std::cerr << "Your system supports OpenGL "
                      << major << "." << minor << std::endl;
            // ベンダー提供ドライバーのインストールを推奨
            std::cerr << "Please install the vendor-provided graphics drivers." << std::endl;
            exit(EXIT_FAILURE);
        }

        // シェーダープログラム初期化
        initBasicProgram();
        initBasicInstancedProgram();

        initShadowProgram();
        initShadowInstancedProgram();

        createPrimitiveMeshes();

        // ground メッシュがまだなら初期化
        initGroundProgram();
        initGroundMesh();

        std::string skypath = std::string(prefix) + "/sky.ppm";
        texture[DS_SKY] = std::make_unique<Texture>(skypath.c_str());
        
        std::string groundpath = std::string(prefix) + "/ground.ppm";
        texture[DS_GROUND] = std::make_unique<Texture>(groundpath.c_str());

        std::string woodpath = std::string(prefix) + "/wood.ppm";
        texture[DS_WOOD] = std::make_unique<Texture>(woodpath.c_str());

        std::string checkeredpath = std::string(prefix) + "/checkered.ppm";
        texture[DS_CHECKERED] = std::make_unique<Texture>(checkeredpath.c_str());

        // 基本形状描画用バッファ初期化
        constexpr std::size_t initialInstanceBufferSize = 1024*128;
        sphereInstances_.clear();
        sphereInstances_.reserve(initialInstanceBufferSize);
        boxInstances_.clear();
        boxInstances_.reserve(initialInstanceBufferSize);
        cylinderInstances_.clear();
        cylinderInstances_.reserve(initialInstanceBufferSize);
        capsuleCapTopInstances_.clear();
        capsuleCapTopInstances_.reserve(initialInstanceBufferSize);
        capsuleCapBottomInstances_.clear();
        capsuleCapBottomInstances_.reserve(initialInstanceBufferSize);
        capsuleCylinderInstances_.clear();
        capsuleCylinderInstances_.reserve(initialInstanceBufferSize);


        if (g_sphereInstanceVBO == 0)
        {
            glGenBuffers(1, &g_sphereInstanceVBO);
        }
        if (g_boxInstanceVBO == 0)
        {
            glGenBuffers(1, &g_boxInstanceVBO);
        }
        if (g_cylinderInstanceVBO == 0)
        {
            glGenBuffers(1, &g_cylinderInstanceVBO);
        }
        if (g_capsuleCapTopInstanceVBO == 0)
        {
            glGenBuffers(1, &g_capsuleCapTopInstanceVBO);
        }
        if (g_capsuleCapBottomInstanceVBO == 0)
        {
            glGenBuffers(1, &g_capsuleCapBottomInstanceVBO);
        }
        if (g_capsuleCylinderInstanceVBO == 0)
        {
            glGenBuffers(1, &g_capsuleCylinderInstanceVBO);
        }
        setupSphereInstanceAttributes();
        setupBoxInstanceAttributes();
        setupCylinderInstanceAttributes();
        setupCapsuleInstanceAttributes();
    }

    void DrawstuffApp::stopGraphics()
    {
        for (int i = 0; i < DS_NUMTEXTURES; i++)
        {
            texture[i].reset();
        }
    }


    void DrawstuffApp::renderFrame(const int width,
                                 const int height,
                                 const dsFunctions *fn,
                                 const int pause)
    {
        if (current_state == SIM_STATE_NOT_STARTED)
        {
            internalError("renderFrame: internal error; called before startGraphics()");
        }
        else if (current_state == SIM_STATE_FINISHED)
        {
            // This might happen if too many objects are drawn in one frame.
            return;
        }
        current_state = SIM_STATE_DRAWING;

        // ---- 基本 GL 状態（core で有効なものだけ）----
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        // ※ GL_LIGHT0 / GL_TEXTURE_GEN_* / glLight* / glColor* は 3.3 core では使わない

        // ---- ビューポート ----
        glViewport(0, 0, width, height);

        // ---- 投影行列を GLM で計算してメンバ proj_ に保持 ----
        constexpr float vnear = 0.1f;
        constexpr float vfar = 100.0f;
        constexpr float k = 0.8f; // 1 = ±45°

        float left, right, bottom, top;
        if (width >= height)
        {
            const float k2 = (height > 0)
                                 ? static_cast<float>(height) / static_cast<float>(width)
                                 : 1.0f;
            left = -vnear * k;
            right = vnear * k;
            bottom = -vnear * k * k2;
            top = vnear * k * k2;
        }
        else
        {
            const float k2 = (height > 0)
                                 ? static_cast<float>(width) / static_cast<float>(height)
                                 : 1.0f;
            left = -vnear * k * k2;
            right = vnear * k * k2;
            bottom = -vnear * k;
            top = vnear * k;
        }

        proj_ = glm::frustum(left, right, bottom, top, vnear, vfar);

        // ★ 3.3 core なので glMatrixMode/glLoadMatrixf は一切呼ばない。
        //   proj_ は drawMeshBasic / drawSky / drawGround / drawPyramidGrid の中で
        //   uMVP に使われる前提。

        // ---- 画面クリア ----
        glClearColor(0.5f, 0.5f, 0.5f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- カメラパラメータのスナップショット ----
        const auto view2_xyz = view_xyz; // std::array<float,3> などを想定
        const auto view2_hpr = view_hpr;

        // ---- view_ を更新する（setCamera の内部で glm による view 行列を作る想定）----
        setCamera(view2_xyz[0], view2_xyz[1], view2_xyz[2],
                  view2_hpr[0], view2_hpr[1], view2_hpr[2]);
        // ※ setCamera は 3.3 core 版になっていて、
        //   glMatrixMode/glLoadIdentity/glRotatef/glTranslatef ではなく
        //   view_ を直接更新する実装になっている前提。

        // ---- ライト方向の更新（固定機能 glLight* は使わない）----
        // --> 投影行列を定数化したので，不要．

        // ---- 背景（空・地面など）----
        drawSky(view2_xyz.data()); // 既存シグネチャに合わせて必要なら .data()
        drawGround();

        // ---- 地面のマーカー ----
        drawPyramidGrid();

        // ---- ユーザ描画前の状態整備 ----
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // setColor は 3.3 core 版（current_color を更新するだけ）の実装になっている前提
        setColor(1.0f, 1.0f, 1.0f, 1.0f);

        current_color[0] = 1.0f;
        current_color[1] = 1.0f;
        current_color[2] = 1.0f;
        current_color[3] = 1.0f;

        texture_id = 0; // 「テクスチャ未使用」の初期値として継続利用

        // ---- ユーザ描画コールバック ----
        if (fn && fn->step)
        {
            fn->step(pause);
        }

        // ---- 球，直方体，円柱のバッチ描画パス ----
        if (!sphereInstances_.empty())
        {
            // インスタンスバッファを GPU にアップロード
            uploadInstanceBuffer(g_sphereInstanceVBO, sphereInstances_); // VBO or SSBO
        }
        if (!boxInstances_.empty())
        {
            // インスタンスバッファを GPU にアップロード
            uploadInstanceBuffer(g_boxInstanceVBO, boxInstances_); // VBO or SSBO
        }
        if (!cylinderInstances_.empty())
        {
            // インスタンスバッファを GPU にアップロード
            uploadInstanceBuffer(g_cylinderInstanceVBO, cylinderInstances_); // VBO or SSBO
        }
        if (!capsuleCapTopInstances_.empty())
        {
            uploadInstanceBuffer(g_capsuleCapTopInstanceVBO, capsuleCapTopInstances_);
        }
        if (!capsuleCapBottomInstances_.empty())
        {
            uploadInstanceBuffer(g_capsuleCapBottomInstanceVBO, capsuleCapBottomInstances_);
        }
        if (!capsuleCylinderInstances_.empty())
        {
            uploadInstanceBuffer(g_capsuleCylinderInstanceVBO, capsuleCylinderInstances_);
        }

        glUseProgram(programBasicInstanced_);

        glUniformMatrix4fv(uProjInst_, 1, GL_FALSE, glm::value_ptr(proj_));
        glUniformMatrix4fv(uViewInst_, 1, GL_FALSE, glm::value_ptr(view_));
        glUniform3f(uLightDirInst_, lightDir_.x, lightDir_.y, lightDir_.z);
        glUniform1f(uTexScaleInst_, 0.5f);

        if (use_textures && texture[DS_WOOD])
        {
            glUniform1i(uUseTexInst_, GL_TRUE);
            glActiveTexture(GL_TEXTURE0);
            bindTextureUnit0(DS_WOOD);
            glUniform1i(uTexInst_, 0);
        }
        else
        {
            glUniform1i(uUseTexInst_, GL_FALSE);
        }

        if (!sphereInstances_.empty())
        {
            // 球をまとめて描画
            glBindVertexArray(meshSphere_[sphere_quality].vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshSphere_[sphere_quality].indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(sphereInstances_.size()));
        }
        if (!boxInstances_.empty())
        {
            // 直方体をまとめて描画
            glBindVertexArray(meshBox_.vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshBox_.indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(boxInstances_.size()));
        }
        if (!cylinderInstances_.empty())
        {
            // 円柱をまとめて描画
            glBindVertexArray(meshCylinder_[cylinder_quality].vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshCylinder_[cylinder_quality].indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(cylinderInstances_.size()));
        }
        if (!capsuleCapTopInstances_.empty())
        {
            // カプセル上部半球をまとめて描画
            glBindVertexArray(meshCapsuleCapTop_[capsule_quality].vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshCapsuleCapTop_[capsule_quality].indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(capsuleCapTopInstances_.size()));
        }
        if (!capsuleCapBottomInstances_.empty())
        {
            // カプセル下部半球をまとめて描画
            glBindVertexArray(meshCapsuleCapBottom_[capsule_quality].vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshCapsuleCapBottom_[capsule_quality].indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(capsuleCapBottomInstances_.size()));
        }
        if (!capsuleCylinderInstances_.empty())
        {
            // カプセル円柱部をまとめて描画
            glBindVertexArray(meshCapsuleCylinder_[capsule_quality].vao);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                meshCapsuleCylinder_[capsule_quality].indexCount,
                GL_UNSIGNED_INT,
                nullptr,
                static_cast<GLsizei>(capsuleCylinderInstances_.size()));
        }

        if (use_shadows)
        {
            glUseProgram(programShadowInstanced_);

            // Z-fighting 対策（★必須）
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);

            // 影は上書き型なら blend off
            glDisable(GL_BLEND);

            // 共通行列
            glUniformMatrix4fv(uShadowModelInst_, 1, GL_FALSE, glm::value_ptr(shadowProject_)); // world→shadow
            glm::mat4 shadowMVP = proj_ * view_ * shadowProject_;
            glUniformMatrix4fv(uShadowMVPInst_, 1, GL_FALSE, glm::value_ptr(shadowMVP));

            glUniform2f(uGroundScaleInst_, ground_scale, ground_scale);
            glUniform2f(uGroundOffsetInst_, ground_ofsx, ground_ofsy);
            glUniform1f(uShadowIntensityInst_, SHADOW_INTENSITY);

            if (use_textures)
            {
                glUniform1i(uShadowUseTexInst_, GL_TRUE);
                glActiveTexture(GL_TEXTURE0);
                bindTextureUnit0(DS_GROUND);
                glUniform1i(uGroundTexInst_, 0);
            }
            else
            {
                glUniform1i(uShadowUseTexInst_, GL_FALSE);
                glUniform3f(uGroundColor_, GROUND_R, GROUND_G, GROUND_B);
            }

            // 球の影
            if (!sphereInstances_.empty())
            {
                glBindVertexArray(meshSphere_[shadow_sphere_quality].vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshSphere_[shadow_sphere_quality].indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(sphereInstances_.size()));
            }
            if (!boxInstances_.empty())
            {
                // 直方体の影
                glBindVertexArray(meshBox_.vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshBox_.indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(boxInstances_.size()));
            }
            if (!cylinderInstances_.empty())
            {
                // 円柱の影
                glBindVertexArray(meshCylinder_[shadow_cylinder_quality].vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshCylinder_[shadow_cylinder_quality].indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(cylinderInstances_.size()));
            }
            if (!capsuleCapTopInstances_.empty())
            {
                // カプセル上部半球の影
                glBindVertexArray(meshCapsuleCapTop_[shadow_cylinder_quality].vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshCapsuleCapTop_[shadow_cylinder_quality].indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(capsuleCapTopInstances_.size()));
            }
            if (!capsuleCapBottomInstances_.empty())
            {
                // カプセル下部半球の影
                glBindVertexArray(meshCapsuleCapBottom_[shadow_cylinder_quality].vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshCapsuleCapBottom_[shadow_cylinder_quality].indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(capsuleCapBottomInstances_.size()));
            }
            if (!capsuleCylinderInstances_.empty())
            {
                // カプセル円柱部の影
                glBindVertexArray(meshCapsuleCylinder_[shadow_cylinder_quality].vao);
                glDrawElementsInstanced(
                    GL_TRIANGLES,
                    meshCapsuleCylinder_[shadow_cylinder_quality].indexCount,
                    GL_UNSIGNED_INT,
                    nullptr,
                    static_cast<GLsizei>(capsuleCylinderInstances_.size()));
            }
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindVertexArray(0);
        glUseProgram(0);

        // インスタンスバッファをクリア
        sphereInstances_.clear();
        boxInstances_.clear();
        cylinderInstances_.clear();
        capsuleCapTopInstances_.clear();
        capsuleCapBottomInstances_.clear();
        capsuleCylinderInstances_.clear();

        current_state = SIM_STATE_RUNNING;
    }

    // Change the camera position according to mouse motion
    void DrawstuffApp::motion(const int mode, const int deltax, const int deltay)
    {
        using std::sin;
        using std::cos;

        float side = 0.01f * float(deltax);
        float fwd = (mode == 4) ? (0.01f * float(deltay)) : 0.0f;
        float s = (float)std::sin(view_hpr[0] * DEG_TO_RAD);
        float c = (float)std::cos(view_hpr[0] * DEG_TO_RAD);

        if (mode == 1)
        {
            view_hpr[0] += float(deltax) * 0.5f;
            view_hpr[1] += float(deltay) * 0.5f;
        }
        else
        {
            view_xyz[0] += -s * side + c * fwd;
            view_xyz[1] += c * side + s * fwd;
            if (mode == 2 || mode == 5)
                view_xyz[2] += 0.01f * float(deltay);
        }
        wrapCameraAngles();
    }

    // ==============================================================
    // TriMesh高速描画API
    // メッシュ登録
    struct MeshResource
    {
        MeshPN meshPN;
        Mesh meshGL;
        bool dirty = true; // GPU 側の再構築が必要かどうか
    };
    // TriMesh 用高速描画 API 実装
    std::vector<MeshResource> meshRegistry_;

    MeshHandle DrawstuffApp::registerIndexedMesh(
        const std::vector<float> &vertices,
        const std::vector<unsigned int> &indices)
    {
        MeshResource meshRes;
        meshRes.meshPN = buildTrianglesMeshPNFromVerticesAndIndices(
            vertices, indices);
        meshRes.dirty = true;
        meshRegistry_.push_back(std::move(meshRes));
        MeshHandle h = static_cast<unsigned int>(meshRegistry_.size() - 1);

        return h;
    }

    void DrawstuffApp::drawRegisteredMesh(
        MeshHandle h,
        const float pos[3], const float R[12], const bool solid)
    {
        MeshResource &meshRes = meshRegistry_[h];

        if (meshRes.dirty)
        {
            // メッシュ VBO/VAO を初期化・更新
            buildTrianglesMeshFromMeshPN(meshRes.meshGL, meshRes.meshPN);
            meshRes.dirty = false;
        }

        glm::mat4 model = buildModelMatrix(pos, R);

        applyMaterials(); // 色・ブレンドなど
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        drawMeshBasic(meshRes.meshGL, model, current_color);
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        if (use_shadows)
        {
            drawShadowMesh(meshRes.meshGL, model);
        }
    }

} // namespace ds_internal
