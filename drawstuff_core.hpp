// drawstuff_core.hpp (仮称) – 内部専用ヘッダ
#pragma once

#include <array>
#include <iostream>
#include <memory>
#include <vector>
#include <cmath>
#include <X11/Xlib.h>  // XEvent など
#include <X11/Xatom.h> // XA_STRING, XA_WM_NAME など
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // lookAt, perspective, rotate, translate
#include <glm/gtc/type_ptr.hpp>         // value_ptr

// お好みで（度→ラジアン強制）
#define GLM_FORCE_RADIANS

#undef Convex // X11 のヘッダと odesim のヘッダが競合するため

constexpr int DS_POLYFILL = 0;
constexpr int DS_WIREFRAME = 1;

/* texture numbers */
enum DS_TEXTURE_NUMBER
{
    DS_NONE = 0, /* uses the current color instead of a texture */
    DS_WOOD,
    DS_CHECKERED,
    DS_GROUND,
    DS_SKY,
};

/**
 * @brief Functions for controlling the simulation.
 *
 * This struct must be filled in by the application and passed to
 * dsSimulationLoop().  In C++14 向けに、関数ポインタは引数リストを
 * 明示した形に修正してある。
 */
typedef struct dsFunctions
{
    int version; /* put DS_VERSION here */

    /* version 1 data */
    void (*start)(void);      /* called before sim loop starts */
    void (*step)(int pause);  /* called before every frame */
    void (*command)(int cmd); /* called if a command key is pressed */
    void (*stop)(void);       /* called after sim loop exits */

    /* version 2 data */
    const char *path_to_textures; /* if nonzero, path to texture files */
} dsFunctions;

namespace
{

    // ライト方向から「z=0 への影投影行列」を作る
    glm::mat4 makeShadowProjectMatrix(const glm::vec3 &lightDir)
    {
        // 念のため正規化しておく
        glm::vec3 L = glm::normalize(lightDir);

        // L.z が 0 に近いとまずいので、簡易ガード
        if (std::abs(L.z) < 1e-4f)
        {
            // 真横からの光は許容しない：少しだけ下向きに傾ける
            L.z = (L.z >= 0.0f ? 1e-4f : -1e-4f);
        }

        const float kx = L.x / L.z;
        const float ky = L.y / L.z;

        // 列優先（column-major）で指定
        return glm::mat4(
            // column 0
            1.0f, 0.0f, 0.0f, 0.0f,
            // column 1
            0.0f, 1.0f, 0.0f, 0.0f,
            // column 2
            -kx, -ky, 0.0f, 0.0f,
            // column 3
            0.0f, 0.0f, 0.0f, 1.0f);
    }

} // anonymous namespace

// TriMesh 用高速描画 API 用
using MeshHandle = unsigned int;

namespace ds_internal
{
    enum SimulationState
    {
        SIM_STATE_NOT_STARTED = 0,
        SIM_STATE_RUNNING = 1,
        SIM_STATE_DRAWING = 2,
        SIM_STATE_FINISHED = 3
    };

    struct VertexPN
    {
        glm::vec3 pos;
        glm::vec3 normal;
    };

    struct Mesh
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0; // インデックスを使うなら
        GLsizei indexCount = 0;
        GLenum primitive = GL_TRIANGLES;
    };

    // constants to convert degrees to radians and the reverse
    constexpr float RAD_TO_DEG = 180.0 / M_PI;
    constexpr float DEG_TO_RAD = M_PI / 180.0;

    // light vector. LIGHTZ is implicitly 1
    constexpr float LIGHTX = 1.0f;
    constexpr float LIGHTY = 0.4f;

    // ground and sky
    constexpr float SHADOW_INTENSITY = 0.65f;
    constexpr float GROUND_R = 0.5f; // ground color for when there's no texture
    constexpr float GROUND_G = 0.5f;
    constexpr float GROUND_B = 0.3f;

    constexpr float ground_scale = 1.0f / 1.0f; // ground texture scale (1/size)
    constexpr float ground_ofsx = 0.5f;         // offset of ground texture
    constexpr float ground_ofsy = 0.5f;
    constexpr float sky_scale = 1.0f / 4.0f; // sky texture scale (1/size)
    constexpr float sky_height = 1.0f;       // sky height above viewpoint

    extern void fatalError(const char *msg, ...);
    extern void internalError(const char *msg, ...);

    class DrawstuffApp
    {
    public:
        static DrawstuffApp &instance();

        // C API から呼ばれる入り口
        int runSimulation(const int argc, const char *const argv[],
                          const int window_width, const int window_height,
                          const dsFunctions *fn);
        void stopSimulation();
        // C API ラッパから呼ぶメソッド
        void storeColor(float r, float g, float b, float a = 1.0f);
        void setColor(float r, float g, float b, float a = 1.0f);
        void setTexture(int texnum);
        void getViewpoint(float xyz[3], float hpr[3]);
        void setViewpoint(const float xyz[3], const float hpr[3]);

        void startGraphics(const int width, const int height, const dsFunctions *fn);
        void stopGraphics();

        void drawFrame(const int width, const int height, const dsFunctions *fn, const int pause);
        void drawTriangleCore(const glm::vec3 p[3],
                              const glm::vec3 &N,
                              const glm::mat4 &model,
                              const bool solid = true);
        void drawTrianglesBatch(
            const std::vector<VertexPN> &verts,
            const glm::mat4 &model,
            const bool solid = true);
        // setters/getters
        // 球などの表示品質設定
        void setSphereQuality(const int n) { sphere_quality = n; }
        void setCylinderQuality(const int n) { cylinder_quality = n; }
        void setDrawMode(const int mode) { draw_mode = mode; }
        void setWriteFrames(bool wf) { writeframes = wf; }
        void toggleWriteFrames() { writeframes = !writeframes; }
        void setPauseMode(bool pm) { pausemode = pm; }
        void togglePauseMode() { pausemode = !pausemode; }
        // shadow and texture mode setup
        bool getUseTextures() const { return use_textures; }
        void setUseTextures(const bool ut) { use_textures = ut; }
        bool getUseShadows() const { return use_shadows; }
        void setUseShadows(const bool us) { use_shadows = us; }

        // 状態チェック用
        bool isInsideSimulationLoop() const { return current_state == SIM_STATE_RUNNING || current_state == SIM_STATE_DRAWING; }

        // TriMesh 用高速描画 API
        MeshHandle registerIndexedMesh(
            const std::vector<float> &vertices,
            const std::vector<unsigned int> &indices);

        void drawRegisteredMesh(
            const MeshHandle h,
            const float pos[3], const float R[12], const bool solid = true);

        // テンプレート関数群
        template <typename T>
        void setCamera(const T x, const T y, const T z,
                       const T h, const T p, const T r) {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            const float xf = static_cast<float>(x);
            const float yf = static_cast<float>(y);
            const float zf = static_cast<float>(z);
            const float hf = static_cast<float>(h);
            const float pf = static_cast<float>(p);
            const float rf = static_cast<float>(r);

            // いままでの glRotatef/Translatef の順序をそのまま GLM に移植
            glm::mat4 view(1.0f);
            view = glm::rotate(view, glm::radians(90.0f), glm::vec3(0, 0, 1));
            view = glm::rotate(view, glm::radians(90.0f), glm::vec3(0, 1, 0));
            view = glm::rotate(view, glm::radians(rf), glm::vec3(1, 0, 0));
            view = glm::rotate(view, glm::radians(pf), glm::vec3(0, 1, 0));
            view = glm::rotate(view, glm::radians(-hf), glm::vec3(0, 0, 1));
            view = glm::translate(view, glm::vec3(-xf, -yf, -zf));

            view_ = view;

            cam_x_ = xf;
            cam_y_ = yf;
            cam_z_ = zf;
            cam_h_ = hf;
            cam_p_ = pf;
            cam_r_ = rf;
        }

        template <typename T>
        void drawBox(const T pos[3], const T R[12],
                                   const T sides[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING) {
                std::string s = "drawBox: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }
            applyMaterials(); // ライティングやカリングなど、既存の状態設定

            glm::mat4 model = buildModelMatrix(pos, R, {sides[0], sides[1], sides[2]});

            // 本体（Core パス）
            drawMeshBasic(meshBox_, model, current_color);

            // 影（Core パス）
            drawShadowMesh(meshBox_, model);
        }

        template <typename T>
        void drawSphere(const T pos[3], const T R[12],
                     const T radius)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawSphere: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            applyMaterials(); // ライティングやカリングなど、既存の状態設定

            glm::mat4 model = buildModelMatrix(pos, R, {radius, radius, radius});

            // 本体（Core パス）
            drawMeshBasic(meshSphere_[sphere_quality], model, current_color);

            // 影（Core パス）
            drawShadowMesh(meshSphere_[sphere_quality], model);
        }

        //=====================================================================
        // OpenGL 3.3 core 版 drawCapsule
        //=====================================================================
        template <typename T>
        void drawCapsule(const T pos[3], const T R[12],
                         const T length, const T radius)
        {
            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawCapsule: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            applyMaterials();

            float l = static_cast<float>(length); // 平行部の長さ
            float r = static_cast<float>(radius); // 半径

            // ODE の姿勢・位置（スケールはここでは使わない）
            glm::mat4 W = buildModelMatrix(pos, R);

            // ---- 円筒部 ----
            // unit cylinder: 半径1, z∈[-1,1]
            // target: 半径 r, z∈[-l/2, +l/2]
            float halfCyl = 0.5f * l;
            glm::mat4 S_body = glm::scale(glm::mat4(1.0f),
                                          glm::vec3(r, r, halfCyl));
            glm::mat4 M_body = W * S_body;

            drawMeshBasic(meshCapsuleBody_, M_body, current_color);

            // ---- 上キャップ ----
            // unit: center (0,0,1), radius 1
            // scale → center (0,0,r)、さらに z=(l/2) にしたい
            float tz_top = halfCyl - r; // z方向の補正
            glm::mat4 S_cap = glm::scale(glm::mat4(1.0f),
                                         glm::vec3(r, r, r));
            glm::mat4 T_capTop = glm::translate(glm::mat4(1.0f),
                                                glm::vec3(0.0f, 0.0f, tz_top));
            glm::mat4 M_capTop = W * T_capTop * S_cap;

            drawMeshBasic(meshCapsuleCapTop_, M_capTop, current_color);

            // ---- 下キャップ ----
            // unit: center (0,0,-1) → scale後 center (0,0,-r)
            // target: center (0,0,-l/2)
            float tz_bottom = -halfCyl + r;
            glm::mat4 T_capBottom = glm::translate(glm::mat4(1.0f),
                                                   glm::vec3(0.0f, 0.0f, tz_bottom));
            glm::mat4 M_capBottom = W * T_capBottom * S_cap;

            drawMeshBasic(meshCapsuleCapBottom_, M_capBottom, current_color);

            if (use_shadows)
            {
                drawShadowMesh(meshCapsuleBody_, M_body);
                drawShadowMesh(meshCapsuleCapTop_, M_capTop);
                drawShadowMesh(meshCapsuleCapBottom_, M_capBottom);
            }
        }

        template <typename T>
        void drawCylinder(const T pos[3], const T R[12],
                        const T length, const T radius)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawCylinder: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            applyMaterials(); // ライティングやカリングなど、既存の状態設定

            glm::mat4 model = buildModelMatrix(pos, R, {radius, radius, length});

            // 本体（Core パス）
            drawMeshBasic(meshCylinder_[cylinder_quality], model, current_color);
            // 影（Core パス）
            drawShadowMesh(meshCylinder_[cylinder_quality], model);
        }

        template <typename T>
        void drawTriangles(const T pos[3], const T R[12],
                           const T *v, int n, const bool solid = true)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawTriangles: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            applyMaterials();

            // 1つの drawTriangles 呼び出しの中では pos,R は共通
            glm::mat4 model = buildModelMatrix(pos, R);

            // ---- ここからバッチ処理 ----

            std::vector<VertexPN> verts;
            verts.reserve(static_cast<std::size_t>(n) * 3);

            const T *p = v;
            for (int i = 0; i < n; ++i, p += 9)
            {
                glm::vec3 p0(
                    static_cast<float>(p[0]),
                    static_cast<float>(p[1]),
                    static_cast<float>(p[2]));

                glm::vec3 p1(
                    static_cast<float>(p[3]),
                    static_cast<float>(p[4]),
                    static_cast<float>(p[5]));

                glm::vec3 p2(
                    static_cast<float>(p[6]),
                    static_cast<float>(p[7]),
                    static_cast<float>(p[8]));

                glm::vec3 U = p1 - p0;
                glm::vec3 V = p2 - p0;
                glm::vec3 N = glm::normalize(glm::cross(U, V));

                verts.push_back({p0, N});
                verts.push_back({p1, N});
                verts.push_back({p2, N});
            }

            drawTrianglesBatch(verts, model, solid);
        }

        template <typename T>
        void drawTriangle(const T pos[3], const T R[12],
                          const T *v0, const T *v1, const T *v2,
                          const bool solid = true)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawTriangle: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            applyMaterials();

            glm::mat4 model = buildModelMatrix(pos, R);
            glm::mat4 mvp = proj_ * view_ * model;

            glUseProgram(programBasic_);
            glUniformMatrix4fv(uMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(model));
            glUniform4fv(uColor_, 1, glm::value_ptr(current_color));

            drawTriangleWithAutoNormal(v0, v1, v2, model, solid);
        }
        template <typename T>
        void drawConvex(const T pos[3], const T R[12],
                        const T *_planes, unsigned int _planecount,
                        const T *_points, unsigned int _pointcount,
                        const unsigned int *_polygons)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawConvex: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            // ライティング・テクスチャ等の共通設定
            applyMaterials();

            // Convex はローカル座標で定義されているので scale=1
            glm::mat4 model = buildModelMatrix(pos, R);

            // ---- Convex → 三角形バッチへの変換 ----

            std::vector<VertexPN> verts;

            // 大雑把に確保（顔数×平均頂点数を3〜4と見積もっておく）
            if (_planecount > 0)
            {
                verts.reserve(static_cast<std::size_t>(_planecount) * 6u);
            }

            auto getPoint = [&](unsigned int idx) -> glm::vec3
            {
                // 安全のため範囲チェックするなら assert 等を入れてもよい
                const T *p = _points + static_cast<std::size_t>(idx) * 3u;
                return glm::vec3(
                    static_cast<float>(p[0]),
                    static_cast<float>(p[1]),
                    static_cast<float>(p[2]));
            };

            unsigned int polyindex = 0;

            for (unsigned int i = 0; i < _planecount; ++i)
            {
                if (polyindex >= std::numeric_limits<unsigned int>::max())
                    break; // 万が一の安全弁

                const unsigned int pointcount = _polygons[polyindex++];
                if (pointcount < 3)
                {
                    // 面にならないのでスキップ（インデックスだけ進める）
                    polyindex += pointcount;
                    continue;
                }

                // この面の法線（_planes の i 番目）
                glm::vec3 N(
                    static_cast<float>(_planes[i * 4 + 0]),
                    static_cast<float>(_planes[i * 4 + 1]),
                    static_cast<float>(_planes[i * 4 + 2]));
                N = glm::normalize(N);

                // この面の頂点インデックス列は _polygons[polyindex ... polyindex+pointcount-1]
                const unsigned int idx0 = _polygons[polyindex];
                const glm::vec3 p0 = getPoint(idx0);

                // Convex なので三角形ファンで安全に分割できる：
                // (p0, p_j, p_{j+1})  j = 1 .. pointcount-2
                for (unsigned int j = 1; j + 1 < pointcount; ++j)
                {
                    const unsigned int idx1 = _polygons[polyindex + j];
                    const unsigned int idx2 = _polygons[polyindex + j + 1];

                    const glm::vec3 p1 = getPoint(idx1);
                    const glm::vec3 p2 = getPoint(idx2);

                    verts.push_back(VertexPN{p0, N});
                    verts.push_back(VertexPN{p1, N});
                    verts.push_back(VertexPN{p2, N});
                }

                // この面の頂点インデックスをすべて消費
                polyindex += pointcount;
            }

            // ---- 実際の描画（塗り＋影） ----

            // convex は基本「塗り潰し」想定なので solid=true
            const bool solid = true;
            drawTrianglesBatch(verts, model, solid);

            // drawTrianglesBatch 内で
            //   - drawMeshBasic(meshTrianglesBatch_, model, current_color)
            //   - if (use_shadows && solid) drawShadowMesh(...)
            // を呼ぶ想定なので、ここで影パスを二重に呼ぶ必要はありません。
        }

        template <typename T>
        void drawLine(const T pos1[3], const T pos2[3])
        {
            static_assert(
                std::is_same_v<T, float> || std::is_same_v<T, double>,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
            {
                std::string s = "drawLine: drawing function called outside simulation loop";
                s += " (current_state=" + std::to_string(current_state) + ")";
                fatalError(s.c_str());
            }

            // 共通の描画状態（programBasic_, uColor, ライティング etc.）
            applyMaterials();

            // ライン用メッシュに頂点を流し込む
            drawLineCore(pos1, pos2);

            // モデル行列：pos1/pos2 そのものをワールド座標として使うので単位行列
            glm::mat4 model(1.0f);

            // 線幅（対応状況は GPU 依存だが、指定するだけしておく）
            glLineWidth(2.0f);

            // 本体描画（他のプリミティブと同じパイプライン）
            drawMeshBasic(meshLine_, model, current_color);

            // 影（他のプリミティブと同じく programShadow_ に統一）
            if (use_shadows)
            {
                drawShadowMesh(meshLine_, model);
            }
        }

    private:
        DrawstuffApp();
        ~DrawstuffApp();

        DrawstuffApp(const DrawstuffApp &) = delete;
        DrawstuffApp &operator=(const DrawstuffApp &) = delete;

        enum PrimitiveType
        {
            PRIMITIVE_SPHERE,
            PRIMITIVE_BOX,
            PRIMITIVE_CAPSULE,
            PRIMITIVE_CYLINDER,
            PRIMITIVE_CONVEX,
            PRIMITIVE_TRIMESH,
            // 他のプリミティブタイプもここに追加
        };

        Mesh meshBox_;
        Mesh meshSphere_[4];
        Mesh meshCylinder_[4];
        Mesh meshCapsuleBody_, meshCapsuleCapTop_, meshCapsuleCapBottom_; // カプセル用メッシュ
        Mesh meshTriangle_;
        Mesh meshTrianglesBatch_;
        Mesh meshLine_;
        Mesh meshPyramid_;

        std::size_t trianglesBatchCapacity_ = 0;

        // matrices
        // カメラ・投影
        glm::mat4 view_{1.0f};
        glm::mat4 proj_{1.0f};
        
        // 基本シェーダプログラム
        GLuint programBasic_ = 0;
        // uniform location
        GLint uMVP_ = -1;
        GLint uModel_ = -1;
        GLint uColor_ = -1;
        GLint uUseTex_ = -1;
        GLint uTex_ = -1;
        GLfloat uTexScale_ = -1;
        GLint uLightDir_ = -1;

        // ピラミッド用 VAO/VBO
        GLuint vaoPyramid_ = 0;
        GLuint vboPyramid_ = 0;

        // 初期化ヘルパ
        void initBasicProgram();

        // ground 用 VAO/VBO
        GLuint vaoGround_ = 0;
        GLuint vboGround_ = 0;
        GLuint programGround_ = 0;
        GLint uGroundMVP_ = -1;
        GLint uGroundModel_ = -1;
        GLint uGroundColor_ = -1;
        GLint uGroundTex_ = -1;
        GLint uGroundScale_ = -1;
        GLint uGroundOffset_ = -1;
        GLint uGroundUseTex_ = -1;

        void initGroundProgram();
        void initGroundMesh();

        // sky 用 VAO/VBO
        GLuint programSky_ = 0;
        GLint uSkyMVP_ = -1;
        GLint uSkyColor_ = -1;
        GLint uSkyTex_ = -1;
        GLint uSkyScale_ = -1;
        GLint uSkyOffset_ = -1;
        GLint uSkyUseTex_ = -1;

        GLuint vaoSky_ = 0;
        GLuint vboSky_ = 0;

        void initSkyProgram();
        void initSkyMesh();

        // 光源方向（平行光）
        const glm::vec3 lightDir_ = glm::normalize(glm::vec3(LIGHTX, LIGHTY, 1.0f)); // ライトの方向（ワールド座標）

        // 影用投影行列（地面 z=0 への投影行列）
        const glm::mat4 shadowProject_ = makeShadowProjectMatrix(lightDir_);

        // 影用シェーダ
        GLuint programShadow_ = 0;
        GLint uShadowMVP_ = -1;
        GLint uShadowColor_ = -1;
        GLint uShadowIntensity_ = -1;
        GLint uShadowModel_ = -1;
        GLint uShadowUseTex_ = -1;

        // 影用の初期化ヘルパ
        void initShadowProjection();
        void initShadowProgram();

        // 影描画ヘルパ（後で中身を実装）
        // void drawShadowPrimitive(PrimitiveType type, const glm::mat4 &model);

        void initLineMesh();

        void initPyramidMesh();

        // 必要ならカメラパラメータも保存
        float cam_x_ = 0.0f, cam_y_ = 0.0f, cam_z_ = 0.0f;
        float cam_h_ = 0.0f, cam_p_ = 0.0f, cam_r_ = 0.0f;

        // 旧グローバル変数をメンバに移していく
        SimulationState current_state;
        bool use_textures;
        bool use_shadows;
        bool writeframes;
        bool pausemode;

        std::array<float, 3> view_xyz;
        std::array<float, 3> view_hpr;

        glm::vec4 current_color;
        int texture_id;
        int currentBoundTextureId_ = -1;

        dsFunctions callbacks_storage_;       // 渡された dsFunctions を保持（実体）
        const dsFunctions *callbacks_ = nullptr;

        int sphere_quality = 3;
        int cylinder_quality = 3;
        int draw_mode = DS_POLYFILL;
        
        // 内部ヘルパー
        void handleEvent(XEvent &event, const dsFunctions *fn);
        void processDrawFrame(int *frame, const dsFunctions *fn);
        void platformSimulationLoop(const int window_width, const int window_height, const dsFunctions *fn,
                                    const int initial_pause);
        void createMainWindow(const int width, const int height);
        void captureFrame(const int num);
        void initMotionModel();
        void applyViewpointToGL();
        void initSphereMeshForQuality(int quality, Mesh &dstMesh);
        void initCylinderMeshForQuality(int quality, Mesh &dstMesh);
        void initTriangleMesh();
        void initTrianglesBatchMesh();
        void createPrimitiveMeshes();
        void applyMaterials();
        void drawSky(const float view_xyz[3]);
        void drawGround();
        void drawPyramidGrid();
        void drawUnitSphere();

        void motion(const int mode, const int deltax, const int deltay);
        void wrapCameraAngles();

        void drawMeshBasic(
            const Mesh &mesh,
            const glm::mat4 &model,
            const glm::vec4 &color);

        void drawShadowMesh(
            const Mesh &mesh,
            const glm::mat4 &model);

        void bindTextureUnit0(const int texId);

        // テンプレート関数群
        template <typename T>
        glm::mat4 buildModelMatrix(
            const T pos[3], const T R[12],
            glm::vec3 sides = glm::vec3(1.0f))
        {
            glm::mat4 model(1.0f);

            glm::mat4 rot(1.0f);

            // 旧 setTransform と同じ 3x3 になるように詰める
            // 行0: [R0 R1 R2]
            rot[0][0] = (float)R[0]; // col0, row0
            rot[1][0] = (float)R[1]; // col1, row0
            rot[2][0] = (float)R[2]; // col2, row0

            // 行1: [R4 R5 R6]
            rot[0][1] = (float)R[4]; // col0, row1
            rot[1][1] = (float)R[5]; // col1, row1
            rot[2][1] = (float)R[6]; // col2, row1

            // 行2: [R8 R9 R10]
            rot[0][2] = (float)R[8];  // col0, row2
            rot[1][2] = (float)R[9];  // col1, row2
            rot[2][2] = (float)R[10]; // col2, row2

            // 平行移動 → 回転 → スケールの順（旧コードと整合）
            model = glm::translate(model,
                                   glm::vec3((float)pos[0],
                                             (float)pos[1],
                                             (float)pos[2]));
            model *= rot;

            model = glm::scale(model,
                               glm::vec3((float)sides[0],
                                         (float)sides[1],
                                         (float)sides[2]));

            return model;
        }

        template <typename T>
        void drawTriangleWithAutoNormal(const T *v0, const T *v1,
                                        const T *v2,
                                        const glm::mat4 &model,
                                        const bool solid = true)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 1. T配列 → glm::vec3 に変換
            glm::vec3 p[3] = {
                glm::vec3(static_cast<float>(v0[0]),
                          static_cast<float>(v0[1]),
                          static_cast<float>(v0[2])),
                glm::vec3(static_cast<float>(v1[0]),
                          static_cast<float>(v1[1]),
                          static_cast<float>(v1[2])),
                glm::vec3(static_cast<float>(v2[0]),
                          static_cast<float>(v2[1]),
                          static_cast<float>(v2[2]))};

            // 2. 法線計算は glm だけで完結
            glm::vec3 U = p[1] - p[0];
            glm::vec3 V = p[2] - p[0];
            glm::vec3 N = glm::normalize(glm::cross(U, V));

            // 3. 実際の描画は非テンプレートのプライベート関数に丸投げ
            drawTriangleCore(p, N, model, solid);
        }

        template <typename T>
        void drawLineCore(const T pos1[3], const T pos2[3])
        {
            static_assert(
                std::is_same_v<T, float> || std::is_same_v<T, double>,
                "T must be float or double");

            initLineMesh(); // VAO/VBO 初期化。meshLine_.primitive = GL_LINES 前提。

            VertexPN verts[2];

            // 位置（ワールド座標）
            verts[0].pos = glm::vec3(
                static_cast<float>(pos1[0]),
                static_cast<float>(pos1[1]),
                static_cast<float>(pos1[2]));
            verts[1].pos = glm::vec3(
                static_cast<float>(pos2[0]),
                static_cast<float>(pos2[1]),
                static_cast<float>(pos2[2]));

            // 法線：適当に線方向を入れておく（A項でそれなりに見える）
            glm::vec3 dir = verts[1].pos - verts[0].pos;
            glm::vec3 N = glm::length(dir) > 0.0f
                              ? glm::normalize(dir)
                              : glm::vec3(0.0f, 1.0f, 0.0f);

            verts[0].normal = N;
            verts[1].normal = N;

            // VBO に書き込み
            glBindBuffer(GL_ARRAY_BUFFER, meshLine_.vbo);
            glBufferSubData(GL_ARRAY_BUFFER,
                            0,
                            sizeof(verts),
                            verts);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            // 念のため：2 頂点
            meshLine_.indexCount = 2;
        }

        // TriMesh 用高速描画 API 実装
        std::vector<std::unique_ptr<Mesh>> meshRegistry_;
    };
} // namespace ds_internal
