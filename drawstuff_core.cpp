// drawstuff_core.cpp
// This code is rewritten from drawstuff (part of ODE) to provide
// backward compatibility for programs that used drawstuff for rendering.

// Original drawstuff license:
/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001-2003 Russell L. Smith.       *
 * All rights reserved.  Email: russ@q12.org   Web: www.q12.org          *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of EITHER:                                  *
 *   (1) The GNU Lesser General Public License as published by the Free  *
 *       Software Foundation; either version 2.1 of the License, or (at  *
 *       your option) any later version. The text of the GNU Lesser      *
 *       General Public License is included with this library in the     *
 *       file LICENSE.TXT.                                               *
 *   (2) The BSD-style license that is included with this library in     *
 *       the file LICENSE-BSD.TXT.                                       *
 *                                                                       *
 * This library is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the files    *
 * LICENSE.TXT and LICENSE-BSD.TXT for more details.                     *
 *                                                                       *
 *************************************************************************/
#include "drawstuff_core.hpp"

#ifdef WIN32
#include <windows.h>
#endif

#include <array>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ode/ode.h>
// #include "config.h"

#ifdef HAVE_APPLE_OPENGL_FRAMEWORK
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include "drawstuff_core.hpp"
#include <X11/Xlib.h>  // Display, Window, XChangeProperty など
#include <X11/Xatom.h> // XA_STRING, XA_WM_NAME など
#include <GL/glx.h>
#include <sys/time.h> // gettimeofday
// 必要に応じて <GL/gl.h> など既存の include も

const char *DEFAULT_PATH_TO_TEXTURES = "../textures/";
// drawstuff_core.cpp

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

namespace
{

    GLuint compileShader(GLenum type, const char *src)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(logLen, '\0');
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());
            fprintf(stderr, "Shader compile error:\n%s\n", log.c_str());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint linkProgram(GLuint vs, GLuint fs)
    {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        GLint ok = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(logLen, '\0');
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            fprintf(stderr, "Program link error:\n%s\n", log.c_str());
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    }

} // namespace

namespace ds_internal
{
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
    //***************************************************************************
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

    void DrawstuffApp::initBasicProgram()
    {
        // すでに作ってあれば何もしない
        if (programBasic_ != 0)
            return;

        // ライティング付きシェーダ
        static const char *vsSrc = R"GLSL(
// basic.vs
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vLocalPos;
out vec3 vLocalNormal;
out vec3 vWorldPos;
out vec3 vWorldNormal;

void main()
{
    vLocalPos      = aPos;
    vLocalNormal   = aNormal;

    vec4 worldPos4 = uModel * vec4(aPos, 1.0);
    vWorldPos      = worldPos4.xyz;
    // 非一様スケールがきつい場合は本当は逆転置行列が必要だが、
    // 今回は簡易版として mat3(uModel) を使用
    vWorldNormal = mat3(uModel) * aNormal;

    gl_Position = uMVP * vec4(aPos, 1.0);
}
    )GLSL";

        static const char *fsSrc = R"GLSL(
// basic.fs
#version 330 core

in vec3 vLocalPos;
in vec3 vLocalNormal;
in vec3 vWorldNormal;
in vec3 vWorldPos;

uniform vec4      uColor;
uniform sampler2D uTex;
uniform bool      uUseTex;
uniform float     uTexScale;   // 例: 0.5f など

uniform vec3 uLightDir; // 光源方向をシェーダ内で指定

out vec4 FragColor;

void main()
{
    vec3 N_tex = normalize(vLocalNormal);

    vec3 base = uColor.rgb;

    if (uUseTex) {
        // トライプラナー重み
        vec3 an  = abs(N_tex);
        float sum = an.x + an.y + an.z + 1e-5;
        vec3 w   = an / sum;

        // 各軸方向からの投影座標
        vec2 uvX = vLocalPos.yz * uTexScale; // X向きの面 → YZ平面
        vec2 uvY = vLocalPos.xz * uTexScale; // Y向きの面 → XZ平面
        vec2 uvZ = vLocalPos.xy * uTexScale; // Z向きの面 → XY平面

        vec3 texX = texture(uTex, uvX).rgb;
        vec3 texY = texture(uTex, uvY).rgb;
        vec3 texZ = texture(uTex, uvZ).rgb;

        vec3 texColor = w.x * texX + w.y * texY + w.z * texZ;

        base *= texColor;
    }

    // 簡単なディフューズライティング
    vec3 L = normalize(uLightDir); // 光線方向（光源→頂点）
    vec3 N_lit = normalize(vWorldNormal);

    // vec3 rgb = base * (0.3 + 0.7 * diff);
    const float A = 1.0/3.0;  // 陰側
    const float B = 2.0/3.0;  // 光源側とのコントラスト

    float diff = max(dot(N_lit, L), 0.0);
    float lightFactor = A + B * diff;  // 陰側 ≒0.33, 光側=1.0

    vec3 rgb = base * lightFactor;

    FragColor = vec4(rgb, uColor.a);

    // FragColor = vec4(0.5 * N + 0.5, 1.0);  // デバッグ用：法線を色で可視化
}
    )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs)
        {
            internalError("Failed to compile basic shaders");
        }

        programBasic_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programBasic_)
        {
            internalError("Failed to link basic shader program");
        }

        

        // uniform ロケーションを取得
        uMVP_ = glGetUniformLocation(programBasic_, "uMVP");
        uModel_ = glGetUniformLocation(programBasic_, "uModel");
        uColor_ = glGetUniformLocation(programBasic_, "uColor");
        uUseTex_ = glGetUniformLocation(programBasic_, "uUseTex");
        uTex_ = glGetUniformLocation(programBasic_, "uTex");
        uTexScale_ = glGetUniformLocation(programBasic_, "uTexScale");
    }

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

    void DrawstuffApp::initGroundProgram()
    {
        if (programGround_ != 0)
            return;
        static const char *ground_vs_src = R"GLSL(
// ground_vs.glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

// ground_scale, ground_ofsx, ground_ofsy を uniform で渡す想定
uniform float uGroundScale;
uniform vec2  uGroundOffset;

out vec3 vNormal;
out vec2 vTex;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;

    // 元の fixed-function の対応：
    // u = x * ground_scale + ground_ofsx
    // v = y * ground_scale + ground_ofsy
    vTex = aPos.xy * uGroundScale + uGroundOffset;
}
)GLSL";

        static const char *ground_fs_src = R"GLSL(
        #version 330 core
        in vec3 vNormal;
        in vec2 vTex;

        uniform sampler2D uTex;
        uniform vec4 uColor;
        uniform int  uUseTex; // 1 = テクスチャ使用, 0 = 単色

        out vec4 FragColor;

        void main()
        {
            vec3 rgb;

            if (uUseTex == 1)
            {
                vec4 texColor = texture(uTex, vTex);
                rgb = uColor.rgb * texColor.rgb;
            }
            else
            {
                // テクスチャなし
                rgb = uColor.rgb;
            }
            FragColor = vec4(rgb, uColor.a);
        }
    )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, ground_vs_src); // 上記 GLSL
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, ground_fs_src);
        if (!vs || !fs)
            internalError("Failed to compile ground shaders");

        programGround_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programGround_)
            internalError("Failed to link ground shader program");

        uGroundMVP_ = glGetUniformLocation(programGround_, "uMVP");
        uGroundModel_ = glGetUniformLocation(programGround_, "uModel");
        uGroundColor_ = glGetUniformLocation(programGround_, "uColor");
        uGroundTex_ = glGetUniformLocation(programGround_, "uTex");
        uGroundScale_ = glGetUniformLocation(programGround_, "uGroundScale");
        uGroundOffset_ = glGetUniformLocation(programGround_, "uGroundOffset");
        uGroundUseTex_ = glGetUniformLocation(programGround_, "uUseTex");
        uShadowIntensity_ = glGetUniformLocation(programShadow_, "uShadowIntensity");
        uLightDir_ = glGetUniformLocation(programBasic_, "uLightDir");
    }

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

    void DrawstuffApp::initSkyProgram()
    {
        if (programSky_ != 0)
            return;
        static const char *sky_vs_src = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;
uniform float uSkyScale;   // sky_scale
uniform float uSkyOffset;  // offset

out vec2 vTex;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);

    // aPos.xy は [-ssize, ssize] の範囲
    //   vTex = aPos.xy * sky_scale + offset
    vTex = aPos.xy * uSkyScale + vec2(uSkyOffset);
}
)GLSL";

        static const char *sky_fs_src = R"GLSL(
#version 330 core

in vec2 vTex;

uniform sampler2D uTex;
uniform vec4      uColor;   // テクスチャを使わないときの空色
uniform int       uUseTex;  // 1 = テクスチャ, 0 = 単色

out vec4 FragColor;

void main()
{
    vec4 color = uColor;

    if (uUseTex != 0)
    {
        FragColor = texture(uTex, vTex);  // テクスチャそのまま
    }
    else
    {
        FragColor = uColor;
    }
}
)GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, sky_vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, sky_fs_src);
        if (!vs || !fs)
            internalError("Failed to compile sky shaders");

        programSky_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programSky_)
            internalError("Failed to link sky shader program");

        uSkyMVP_ = glGetUniformLocation(programSky_, "uMVP");
        uSkyColor_ = glGetUniformLocation(programSky_, "uColor");
        uSkyTex_ = glGetUniformLocation(programSky_, "uTex");
        uSkyScale_ = glGetUniformLocation(programSky_, "uSkyScale");
        uSkyOffset_ = glGetUniformLocation(programSky_, "uSkyOffset");
        uSkyUseTex_ = glGetUniformLocation(programSky_, "uUseTex");
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

    void DrawstuffApp::initShadowProgram()
    {
        if (programShadow_ != 0)
            return;

        static const char *shadow_vs_src = R"GLSL(
// shadow.vs
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uShadowMVP;    // proj * view * shadowModel
uniform mat4 uShadowModel;  // ★ 影としてのモデル行列（= shadowModel）
uniform vec2 uGroundScale;
uniform vec2 uGroundOffset;

out vec2 vTex;

void main()
{
    // 影として地面上に投影されたワールド座標
    vec4 shadowWorld = uShadowModel * vec4(aPos, 1.0);

    // ground と同じ定義: (x,y) にスケール＋オフセット
    vTex = shadowWorld.xy * uGroundScale + uGroundOffset;

    // 位置も同じ shadowWorld を使う（事前に uShadowMVP = proj*view*uShadowModel にしてある前提）
    gl_Position = uShadowMVP * vec4(aPos, 1.0);
}
)GLSL";

        static const char *shadow_fs_src = R"GLSL(
// shadow.fs
#version 330 core

in vec2 vTex;
out vec4 FragColor;

uniform sampler2D uGroundTex;
uniform float uShadowIntensity;  // 例: 0.5f
uniform bool  uUseTex;      // ★ 追加：テクスチャを使うか
uniform vec3  uGroundColor;      // ★ 追加：テクスチャ無し時の地面色 (GROUND_R,G,B)

void main()
{
    vec3 base;

    if (uUseTex) {
        // テクスチャあり：地面テクスチャをそのままベースに
        base = texture(uGroundTex, vTex).rgb;
    } else {
        // テクスチャなし：地面のフラットカラー
        base = uGroundColor;
    }

    // SHADOW_INTENSITY 倍だけ暗くする
    vec3 shaded = base * uShadowIntensity;

    FragColor = vec4(shaded, 1.0);
}
)GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, shadow_vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, shadow_fs_src);
        if (!vs || !fs)
        {
            internalError("Failed to compile shadow shaders");
        }

        programShadow_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programShadow_)
        {
            internalError("Failed to link shadow shader program");
        }

        // すでにあるもの
        uShadowMVP_ = glGetUniformLocation(programShadow_, "uShadowMVP");
        uShadowModel_ = glGetUniformLocation(programShadow_, "uShadowModel");
        uGroundScale_ = glGetUniformLocation(programShadow_, "uGroundScale");
        uGroundOffset_ = glGetUniformLocation(programShadow_, "uGroundOffset");
        uGroundTex_ = glGetUniformLocation(programShadow_, "uGroundTex");
        uShadowIntensity_ = glGetUniformLocation(programShadow_, "uShadowIntensity");

        // ★ 新しく追加
        uShadowUseTex_ = glGetUniformLocation(programShadow_, "uUseTex");
        uGroundColor_ = glGetUniformLocation(programShadow_, "uGroundColor");
    }

    void DrawstuffApp::drawShadowMesh(
        const Mesh &mesh,
        const glm::mat4 &model)
    {
        if (!use_shadows)
            return;

        initShadowProgram();

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

    Display *display = nullptr;
    int screen = 0;
    XVisualInfo *visual = nullptr; // best visual for openGL
    Colormap colormap = 0;         // window's colormap
    Atom wm_protocols_atom = 0;
    Atom wm_delete_window_atom = 0;

    // window and openGL
    Window win = 0;             // X11 window, 0 if not initialized
    int width = 0, height = 0;  // window size
    GLXContext glx_context = 0; // openGL rendering context
    int last_key_pressed = 0;   // last key pressed in the window
    int pausemode = 0;          // 1 if in `pause' mode
    int singlestep = 0;         // 1 if single step key pressed
    int writeframes = 0;        // 1 if frame files to be written

    void DrawstuffApp::createMainWindow(const int _width, const int _height)
    {
        // create X11 display connection
        display = XOpenDisplay(NULL);
        if (!display)
            fatalError("can not open X11 display");
        screen = DefaultScreen(display);

        // get GL visual
        static int attribListDblBuf[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16,
                                         GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
        static int attribList[] = {GLX_RGBA, GLX_DEPTH_SIZE, 16,
                                   GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
        visual = glXChooseVisual(display, screen, attribListDblBuf);
        if (!visual)
            visual = glXChooseVisual(display, screen, attribList);
        if (!visual)
            fatalError("no good X11 visual found for OpenGL");

        // create colormap
        colormap = XCreateColormap(display, RootWindow(display, screen),
                                   visual->visual, AllocNone);

        // initialize variables
        win = 0;
        width = _width;
        height = _height;
        glx_context = 0;
        last_key_pressed = 0;

        if (width < 1 || height < 1)
            internalError(0, "bad window width or height");

        // create the window
        XSetWindowAttributes attributes;
        attributes.background_pixel = BlackPixel(display, screen);
        attributes.colormap = colormap;
        attributes.event_mask = ButtonPressMask | ButtonReleaseMask |
                                KeyPressMask | KeyReleaseMask | ButtonMotionMask | PointerMotionHintMask |
                                StructureNotifyMask;
        win = XCreateWindow(display, RootWindow(display, screen), 50, 50, width, height,
                            0, visual->depth, InputOutput, visual->visual,
                            CWBackPixel | CWColormap | CWEventMask, &attributes);

        // associate a GLX context with the window
        glx_context = glXCreateContext(display, visual, 0, GL_TRUE);
        if (!glx_context)
            fatalError("can't make an OpenGL context");

        // set the window title
        XTextProperty window_name;
        window_name.value = (unsigned char *)"Simulation";
        window_name.encoding = XA_STRING;
        window_name.format = 8;
        window_name.nitems = strlen((char *)window_name.value);
        XSetWMName(display, win, &window_name);

        // participate in the window manager 'delete yourself' protocol
        wm_protocols_atom = XInternAtom(display, "WM_PROTOCOLS", False);
        wm_delete_window_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (XSetWMProtocols(display, win, &wm_delete_window_atom, 1) == 0)
            fatalError("XSetWMProtocols() call failed");

        // pop up the window
        XMapWindow(display, win);
        XSync(display, win);
    }

    void destroyMainWindow()
    {
        glXDestroyContext(display, glx_context);
        XDestroyWindow(display, win);
        XSync(display, 0);
        XCloseDisplay(display);
        display = 0;
        win = 0;
        glx_context = 0;
    }

    void DrawstuffApp::handleEvent(XEvent &event, const dsFunctions *fn)
    {
        static int mx = 0, my = 0; // mouse position
        static int mode = 0;       // mouse button bits

        switch (event.type)
        {

        case ButtonPress:
        {
            if (event.xbutton.button == Button1)
                mode |= 1;
            if (event.xbutton.button == Button2)
                mode |= 2;
            if (event.xbutton.button == Button3)
                mode |= 4;
            mx = event.xbutton.x;
            my = event.xbutton.y;
        }
            return;

        case ButtonRelease:
        {
            if (event.xbutton.button == Button1)
                mode &= (~1);
            if (event.xbutton.button == Button2)
                mode &= (~2);
            if (event.xbutton.button == Button3)
                mode &= (~4);
            mx = event.xbutton.x;
            my = event.xbutton.x;
        }
            return;

        case MotionNotify:
        {
            if (event.xmotion.is_hint)
            {
                Window root, child;
                unsigned int mask;
                XQueryPointer(display, win, &root, &child, &event.xbutton.x_root,
                              &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y,
                              &mask);
            }
            motion(mode, event.xmotion.x - mx, event.xmotion.y - my);
            mx = event.xmotion.x;
            my = event.xmotion.y;
        }
            return;

        case KeyPress:
        {
            KeySym key;
            XLookupString(&event.xkey, NULL, 0, &key, 0);
            if ((event.xkey.state & ControlMask) == 0)
            {
                if (key >= ' ' && key <= 126 && fn->command)
                    fn->command(key);
            }
            else if (event.xkey.state & ControlMask)
            {
                switch (key)
                {
                case 't':
                case 'T':
                    use_textures = !use_textures;
                    break;
                case 's':
                case 'S':
                    use_shadows = !use_shadows;
                    break;
                case 'x':
                case 'X':
                    stopSimulation();
                    break;
                case 'p':
                case 'P':
                    pausemode ^= 1;
                    singlestep = 0;
                    break;
                case 'o':
                case 'O':
                    if (pausemode)
                        singlestep = 1;
                    break;
                case 'v':
                case 'V':
                {
                    float xyz[3], hpr[3];
                    getViewpoint(xyz, hpr);
                    printf("Viewpoint = (%.4f,%.4f,%.4f,%.4f,%.4f,%.4f)\n",
                           xyz[0], xyz[1], xyz[2], hpr[0], hpr[1], hpr[2]);
                    break;
                }
                case 'w':
                case 'W':
                    writeframes ^= 1;
                    if (writeframes)
                        printf("Now writing frames to PPM files\n");
                    break;
                }
            }
            last_key_pressed = key; // a kludgy place to put this...
        }
            return;

        case KeyRelease:
        {
            // hmmmm...
        }
            return;

        case ClientMessage:
            if (event.xclient.message_type == wm_protocols_atom &&
                event.xclient.format == 32 &&
                Atom(event.xclient.data.l[0]) == wm_delete_window_atom)
            {
                current_state = SIM_STATE_FINISHED;
                return;
            }
            return;

        case ConfigureNotify:
            width = event.xconfigure.width;
            height = event.xconfigure.height;
            return;
        }
    }

    void DrawstuffApp::captureFrame(const int num)
    {
        // ログ出力（stderr 相当）
        std::cerr << "capturing frame "
                  << std::setw(4) << std::setfill('0') << num << '\n';

        // ファイル名生成
        std::ostringstream oss;
        oss << "frame/frame"
            << std::setw(4) << std::setfill('0') << num
            << ".ppm";
        const std::string filename = oss.str();

        // バイナリ出力ストリーム
        std::ofstream out(filename, std::ios::binary);
        if (!out)
        {
            fatalError("can't open \"%s\" for writing", filename.c_str());
        }

        // PPM ヘッダ
        out << "P6\n"
            << width << ' ' << height << "\n255\n";

        // X11 からフレームバッファ取得
        XImage *image = XGetImage(display, win,
                                  0, 0,
                                  static_cast<unsigned int>(width),
                                  static_cast<unsigned int>(height),
                                  ~0UL, ZPixmap);
        if (!image)
        {
            fatalError("XGetImage failed");
        }

        // --- ここで highBitIndex + SHIFTL 相当を直接処理 ---

        auto computeShift = [](unsigned long mask) -> int
        {
            if (mask == 0)
                return 0;
            int bit = 0;
            while (mask >>= 1)
            {
                ++bit;
            }
            // 0..255 に正規化するために 7bit へ合わせる
            return 7 - bit;
        };

        const int rshift = computeShift(image->red_mask);
        const int gshift = computeShift(image->green_mask);
        const int bshift = computeShift(image->blue_mask);

        auto shiftWithSign = [](unsigned long value, int shift) -> std::uint8_t
        {
            if (shift >= 0)
                return static_cast<std::uint8_t>((value << shift) & 0xFFu);
            else
                return static_cast<std::uint8_t>((value >> (-shift)) & 0xFFu);
        };

        // 1 行分のバッファ
        std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 3);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const unsigned long pixel = XGetPixel(image, x, y);

                const std::uint8_t r = shiftWithSign(pixel & image->red_mask, rshift);
                const std::uint8_t g = shiftWithSign(pixel & image->green_mask, gshift);
                const std::uint8_t b = shiftWithSign(pixel & image->blue_mask, bshift);

                const std::size_t idx = static_cast<std::size_t>(x) * 3;
                row[idx + 0] = r;
                row[idx + 1] = g;
                row[idx + 2] = b;
            }

            out.write(reinterpret_cast<const char *>(row.data()),
                      static_cast<std::streamsize>(row.size()));
        }

        XDestroyImage(image);
    }

    void DrawstuffApp::processDrawFrame(int *frame, const dsFunctions *fn)
    {
        drawFrame(width, height, fn, pausemode && !singlestep);
        singlestep = 0;

        glFlush();
        glXSwapBuffers(display, win);
        XSync(display, 0);

        // capture frames if necessary
        if (pausemode == 0 && writeframes)
        {
            captureFrame(*frame);
            (*frame)++;
        }
    }

    void microsleep(int usecs)
    {
        if (usecs <= 0)
            return;
        std::this_thread::sleep_for(std::chrono::microseconds(usecs));
    }

    void DrawstuffApp::platformSimulationLoop(const int window_width, const int window_height, const dsFunctions *fn,
                                              const int initial_pause)
    {
        pausemode = initial_pause;
        createMainWindow(window_width, window_height);
        glXMakeCurrent(display, win, glx_context);

        startGraphics(window_width, window_height, fn);
        
        static bool firsttime = true;
        if (firsttime)
        {
            fprintf(
                stderr,
                "\n"
                "Simulation test environment (drawstuffCompat)\n"
                "   Ctrl-P : pause / unpause (or say `-pause' on command line).\n"
                "   Ctrl-O : single step when paused.\n"
                "   Ctrl-T : toggle textures (or say `-notex' on command line).\n"
                "   Ctrl-S : toggle shadows (or say `-noshadow' on command line).\n"
                "   Ctrl-V : print current viewpoint coordinates (x,y,z,h,p,r).\n"
                "   Ctrl-W : write frames to ppm files: frame/frameNNN.ppm\n"
                "   Ctrl-X : exit.\n"
                "\n"
                "Change the camera position by clicking + dragging in the window.\n"
                "   Left button - pan and tilt.\n"
                "   Right button - forward and sideways.\n"
                "   Left + Right button (or middle button) - sideways and up.\n"
                "\n");
            firsttime = false;
        }

        if (fn->start)
            fn->start();

        timeval tv;
        gettimeofday(&tv, 0);
        double prev = tv.tv_sec + (double)tv.tv_usec / 1000000.0;

        int frame = 1;
        while (current_state == SIM_STATE_RUNNING)
        {
            // read in and process all pending events for the main window
            XEvent event;
            while (current_state == SIM_STATE_RUNNING && XPending(display))
            {
                XNextEvent(display, &event);
                handleEvent(event, fn);
            }

            gettimeofday(&tv, 0);
            double curr = tv.tv_sec + (double)tv.tv_usec / 1000000.0;
            if (curr - prev >= 1.0 / 60.0)
            {
                prev = curr;
                processDrawFrame(&frame, fn);
            }
            else
                microsleep(1000);
        };

        if (fn->stop)
            fn->stop();
        stopGraphics();

        destroyMainWindow();
    }

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

    // ヘッダ or cpp のファイルスコープに置いてしまって良い（display list は不要）
    constexpr float ICX = 0.525731112119133606f;
    constexpr float ICZ = 0.850650808352039932f;

    static const GLfloat gSphereIcosaVerts[12][3] = {
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

    static const int gSphereIcosaFaces[20][3] = {
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

    static void appendSpherePatch(
        const glm::vec3 &p1,
        const glm::vec3 &p2,
        const glm::vec3 &p3,
        int level,
        std::vector<VertexPN> &vertices,
        std::vector<uint32_t> &indices)
    {
        if (level > 0)
        {
            glm::vec3 q1 = glm::normalize(0.5f * (p1 + p2));
            glm::vec3 q2 = glm::normalize(0.5f * (p2 + p3));
            glm::vec3 q3 = glm::normalize(0.5f * (p3 + p1));

            appendSpherePatch(p1, q1, q3, level - 1, vertices, indices);
            appendSpherePatch(q1, p2, q2, level - 1, vertices, indices);
            appendSpherePatch(q1, q2, q3, level - 1, vertices, indices);
            appendSpherePatch(q3, q2, p3, level - 1, vertices, indices);
        }
        else
        {
            uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({p1, glm::normalize(p1)});
            vertices.push_back({p2, glm::normalize(p2)});
            vertices.push_back({p3, glm::normalize(p3)});
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }
    }

    void DrawstuffApp::initSphereMeshForQuality(int quality, Mesh &dstMesh)
    {
        std::vector<VertexPN> vertices;
        std::vector<uint32_t> indices;

        int level = quality - 1; // quality=1 → 分割なし, 2→1回, 3→2回 … など

        for (int i = 0; i < 20; ++i)
        {
            // ★ drawPatch と同じ頂点順序を使うのが安全
            const GLfloat *pA = gSphereIcosaVerts[gSphereIcosaFaces[i][2]];
            const GLfloat *pB = gSphereIcosaVerts[gSphereIcosaFaces[i][1]];
            const GLfloat *pC = gSphereIcosaVerts[gSphereIcosaFaces[i][0]];

            glm::vec3 p1(pA[0], pA[1], pA[2]);
            glm::vec3 p2(pB[0], pB[1], pB[2]);
            glm::vec3 p3(pC[0], pC[1], pC[2]);

            appendSpherePatch(p1, p2, p3, level, vertices, indices);
        }
        
        // ここから VAO / VBO / EBO を作成（box と同様）

        // VAO
        glGenVertexArrays(1, &dstMesh.vao);
        glBindVertexArray(dstMesh.vao);

        // VBO
        glGenBuffers(1, &dstMesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, dstMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(VertexPN),
                     vertices.data(),
                     GL_STATIC_DRAW);

        // EBO
        glGenBuffers(1, &dstMesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dstMesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) vec3 aPos;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,                                                // location
            3,                                                // size
            GL_FLOAT,                                         // type
            GL_FALSE,                                         // normalized
            sizeof(VertexPN),                                 // stride
            reinterpret_cast<void *>(offsetof(VertexPN, pos)) // offset
        );

        // layout(location = 1) vec3 aNormal;
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,                                                   // location
            3,                                                   // size
            GL_FLOAT,                                            // type
            GL_FALSE,                                            // normalized
            sizeof(VertexPN),                                    // stride
            reinterpret_cast<void *>(offsetof(VertexPN, normal)) // offset
        );

        dstMesh.indexCount = static_cast<GLsizei>(indices.size());

        // 後片付け
        glBindVertexArray(0);
    }

    void DrawstuffApp::initCylinderMeshForQuality(int quality, Mesh &dstMesh)
    {
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
        const float a = 2.0f * static_cast<float>(M_PI) / static_cast<float>(n);

        // 2-1. 側面（サイド）
        //
        // 軸: Z
        // 円周: X-Y 平面
        //
        for (int i = 0; i <= n; ++i)
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
            uint32_t iTop0 = 2 * i;
            uint32_t iBot0 = 2 * i + 1;
            uint32_t iTop1 = 2 * (i + 1);
            uint32_t iBot1 = 2 * (i + 1) + 1;

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
        for (int i = 0; i <= n; ++i)
        {
            float theta = a * static_cast<float>(i);
            float x = std::cos(theta);
            float y = std::sin(theta);

            vertices.push_back({glm::vec3(r * x, r * y, +halfLen),
                                glm::vec3(0.0f, 0.0f, +1.0f)});
        }

        for (int i = 0; i < n; ++i)
        {
            uint32_t i0 = topCenterIndex;
            uint32_t i1 = topRingStart + i;
            uint32_t i2 = topRingStart + i + 1;

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
        for (int i = 0; i <= n; ++i)
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
            uint32_t i0 = bottomCenterIndex;
            uint32_t i1 = bottomRingStart + i + 1;
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
        using std::cos;
        using std::sin;

        // ========= パラメータ =========
        constexpr int capsule_quality = 3;  // 1〜3
        const int n = capsule_quality * 12; // 円周分割数（4の倍数）
        const float length = 2.0f;          // 平行部長さ
        const float radius = 1.0f;          // 半径

        const float l = length * 0.5f; // cylinder は z = ±l (=±1)
        const float r = radius;

        const float a = 2.0f * static_cast<float>(M_PI) / static_cast<float>(n);
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

    // 三角形メッシュ高速描画用ユーティリティ
    struct MeshPN
    {
        std::vector<VertexPN> vertices; // pos + normal
        std::vector<uint32_t> indices;  // 0-based index
    };

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

    void buildTrianglesMeshFromVertexPN(
        Mesh &outMesh,
        const std::vector<VertexPN> &verts,
        const std::vector<uint32_t> &indices)
    {
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

    void buildTrianglesMeshFromVerticesAndIndices(
        Mesh &outMesh,
        const std::vector<float> &vertices,   // x,y,z,...
        const std::vector<uint32_t> &indices) // 0-based index
    {
        // スムーズシェーディング版の VertexPN＋indices を構築
        // MeshPN meshPN = buildSmoothVertexPNFromVerticesAndIndices(vertices, indices);
        // 折り目付きシェーディング版の VertexPN＋indices を構築
        MeshPN meshPN = buildCreasedVertexPNFromVerticesAndIndices(vertices, indices, 60.0f);
        // OpenGL の VAO/VBO/EBO を構築
        buildTrianglesMeshFromVertexPN(outMesh, meshPN.vertices, meshPN.indices);
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
        glDisable(GL_FOG);

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
        drawShadowMesh(meshTriangle_, model);
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

    void DrawstuffApp::startGraphics(const int width, const int height, const dsFunctions *fn)
    {
        const char *prefix = DEFAULT_PATH_TO_TEXTURES;
        if (fn->version >= 2 && fn->path_to_textures)
            prefix = fn->path_to_textures;

        // GL 初期化
        gladLoadGL();

        initBasicProgram();

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
    }

    void DrawstuffApp::stopGraphics()
    {
        for (int i = 0; i < DS_NUMTEXTURES; i++)
        {
            texture[i].reset();
        }
    }
    void DrawstuffApp::drawFrame(const int width,
                                 const int height,
                                 const dsFunctions *fn,
                                 const int pause)
    {
        if (current_state == SIM_STATE_NOT_STARTED)
        {
            internalError("drawFrame: internal error; called before startGraphics()");
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

        current_state = SIM_STATE_RUNNING;
    }

    // TriMesh高速描画API
    // メッシュ登録
    MeshHandle DrawstuffApp::registerIndexedMesh(
        const std::vector<float> &vertices,
        const std::vector<unsigned int> &indices)
    {
        auto mesh = std::make_unique<Mesh>();
        buildTrianglesMeshFromVerticesAndIndices(
            *mesh, vertices, indices);
        meshRegistry_.push_back(std::move(mesh));

        return static_cast<MeshHandle>(meshRegistry_.size() - 1);
    }

    void DrawstuffApp::drawRegisteredMesh(
        MeshHandle h,
        const float pos[3], const float R[12], const bool solid)
    {
        Mesh *m = meshRegistry_[h].get();

        glm::mat4 model = buildModelMatrix(pos, R);

        applyMaterials(); // 色・ブレンドなど
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        drawMeshBasic(*m, model, current_color);
        if (!solid)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        if (use_shadows)
        {
            drawShadowMesh(*m, model);
        }
    }

} // namespace ds_internal
