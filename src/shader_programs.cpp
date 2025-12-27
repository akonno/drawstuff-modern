// ==============================================================
// drawstuff: shader_programs.cpp
// This file is part of drawstuff-modern, a modern reimplementation inspired by
// the drawstuff library distributed with the Open Dynamics Engine (ODE).
// The original drawstuff was developed by Russell L. Smith.
// This implementation has been substantially rewritten and redesigned.
// ============================================================================
// Copyright (c) 2025 Akihisa Konno
// Released under the BSD 3-Clause License.
// See the LICENSE file for details.

#include "drawstuff_core.hpp"
#include <glm/gtx/string_cast.hpp>

// ==============================================================
// Shader compilation and linking utilities
// =============================================================
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

namespace ds_internal {
    // =================================================
    // シェーダプログラム群
    // =================================================
    // 基本シェーダプログラム
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
    void DrawstuffApp::initBasicInstancedProgram()
    {
        // すでに作ってあれば何もしない
        if (programBasicInstanced_ != 0)
            return;

        // ライティング付き・インスタンシング対応シェーダ
        static const char *vsSrc = R"GLSL(
// basic_instanced.vs
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

// インスタンスごとのモデル行列＆色
layout(location = 2) in mat4 iModel; // 2,3,4,5 を占有
layout(location = 6) in vec4 iColor;

uniform mat4 uProj;
uniform mat4 uView;

out vec3 vLocalPos;
out vec3 vLocalNormal;
out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec4 vColor;

void main()
{
    vLocalPos    = aPos;
    vLocalNormal = aNormal;

    vec4 worldPos4 = iModel * vec4(aPos, 1.0);
    vWorldPos      = worldPos4.xyz;

    // 非一様スケールがきつい場合は本当は逆転置行列が必要だが、
    // 今回は簡易版として mat3(iModel) を使用
    vWorldNormal = mat3(iModel) * aNormal;

    vColor = iColor;

    gl_Position = uProj * uView * worldPos4;
}
    )GLSL";

        static const char *fsSrc = R"GLSL(
// basic_instanced.fs
#version 330 core

in vec3 vLocalPos;
in vec3 vLocalNormal;
in vec3 vWorldNormal;
in vec3 vWorldPos;
in vec4 vColor;

uniform sampler2D uTex;
uniform bool      uUseTex;
uniform float     uTexScale;   // 例: 0.5f など

uniform vec3 uLightDir; // 光源方向（光源→頂点）

out vec4 FragColor;

void main()
{
    vec3 N_tex = normalize(vLocalNormal);

    // ベース色はインスタンスごとの色
    vec3 base = vColor.rgb;

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

    const float A = 1.0/3.0;  // 陰側
    const float B = 2.0/3.0;  // 光源側とのコントラスト

    float diff = max(dot(N_lit, L), 0.0);
    float lightFactor = A + B * diff;  // 陰側 ≒0.33, 光側=1.0

    vec3 rgb = base * lightFactor;

    FragColor = vec4(rgb, vColor.a);
}
    )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs)
        {
            internalError("Failed to compile basic instanced shaders");
        }

        programBasicInstanced_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programBasicInstanced_)
        {
            internalError("Failed to link basic instanced shader program");
        }

        // uniform ロケーションを取得
        uProjInst_ = glGetUniformLocation(programBasicInstanced_, "uProj");
        uViewInst_ = glGetUniformLocation(programBasicInstanced_, "uView");
        uLightDirInst_ = glGetUniformLocation(programBasicInstanced_, "uLightDir");
        uUseTexInst_ = glGetUniformLocation(programBasicInstanced_, "uUseTex");
        uTexInst_ = glGetUniformLocation(programBasicInstanced_, "uTex");
        uTexScaleInst_ = glGetUniformLocation(programBasicInstanced_, "uTexScale");
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
    void DrawstuffApp::initShadowInstancedProgram()
    {
        if (programShadowInstanced_ != 0)
            return;

        // インスタンス版 shadow.vs
        static const char *shadow_vs_instanced_src = R"GLSL(
// shadow_instanced.vs
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 2) in mat4 iModel; // インスタンスごとのモデル行列

// world → shadow 平面への変換（旧 uShadowModel 相当だが、modelは含まない）
uniform mat4 uShadowModel;   

// 画面への変換用: proj * view * uShadowModel
uniform mat4 uShadowMVP;     

uniform vec2 uGroundScale;
uniform vec2 uGroundOffset;

out vec2 vTex;

void main()
{
    // まず通常どおりワールド座標を作る
    vec4 worldPos = iModel * vec4(aPos, 1.0);

    // 影として地面上に投影された座標（world → shadow平面）
    vec4 shadowWorld = uShadowModel * worldPos;

    // ground と同じ定義: (x,y) にスケール＋オフセット
    vTex = shadowWorld.xy * uGroundScale + uGroundOffset;

    // 位置も同じ shadowWorld 由来の uShadowMVP を使う
    // （CPU側で uShadowMVP = proj * view * uShadowModel としておく前提）
    gl_Position = uShadowMVP * worldPos;
}
)GLSL";

        // FS は既存 shadow.fs と同じでOK
        static const char *shadow_fs_src = R"GLSL(
// shadow.fs (インスタンス兼用)
#version 330 core

in vec2 vTex;
out vec4 FragColor;

uniform sampler2D uGroundTex;
uniform float uShadowIntensity;  // 例: 0.5f
uniform bool  uUseTex;           // テクスチャを使うか
uniform vec3  uGroundColor;      // テクスチャ無し時の地面色 (GROUND_R,G,B)

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

        GLuint vs = compileShader(GL_VERTEX_SHADER, shadow_vs_instanced_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, shadow_fs_src);
        if (!vs || !fs)
        {
            internalError("Failed to compile shadow instanced shaders");
        }

        programShadowInstanced_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programShadowInstanced_)
        {
            internalError("Failed to link shadow instanced shader program");
        }

        // uniform ロケーション（Inst 用）
        uShadowMVPInst_ = glGetUniformLocation(programShadowInstanced_, "uShadowMVP");
        uShadowModelInst_ = glGetUniformLocation(programShadowInstanced_, "uShadowModel");
        uGroundScaleInst_ = glGetUniformLocation(programShadowInstanced_, "uGroundScale");
        uGroundOffsetInst_ = glGetUniformLocation(programShadowInstanced_, "uGroundOffset");
        uGroundTexInst_ = glGetUniformLocation(programShadowInstanced_, "uGroundTex");
        uShadowIntensityInst_ = glGetUniformLocation(programShadowInstanced_, "uShadowIntensity");
        uShadowUseTexInst_ = glGetUniformLocation(programShadowInstanced_, "uUseTex");
        uGroundColorInst_ = glGetUniformLocation(programShadowInstanced_, "uGroundColor");
    }
} // namespace ds_internal