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
#include <GL/gl.h>
#include <GL/glu.h>
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

namespace ds_internal
{
    enum SimulationState
    {
        SIM_STATE_NOT_STARTED = 0,
        SIM_STATE_RUNNING = 1,
        SIM_STATE_DRAWING = 2
    };
    // GL 型特性
    template <typename T>
    struct GLType;

    template <>
    struct GLType<float>
    {
        using type = GLfloat;
        static constexpr auto multMatrix = &glMultMatrixf;
    };

    template <>
    struct GLType<double>
    {
        using type = GLdouble;
        static constexpr auto multMatrix = &glMultMatrixd;
    };

    enum class GLBackend
    {
        Legacy, // 今までどおり glBegin なども使える
        Core33, // シェーダ + VAO/VBO + 自前行列 のみ
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
        GLBackend backend_ = GLBackend::Legacy; // 最初は既存どおり

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

            // 互換用に legacy パスを残すなら：
            if (backend_ == GLBackend::Legacy)
            {
                glMatrixMode(GL_MODELVIEW);
                glLoadMatrixf(glm::value_ptr(view_));
            }
        }

        template <typename T>
        void drawBox(const T pos[3], const T R[12],
                                   const T sides[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode(); // ライティングやカリングなど、既存の状態設定

            glm::mat4 model = buildModelMatrix(pos, R, sides);

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
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode(); // ライティングやカリングなど、既存の状態設定

            const T sides[3] = { radius, radius, radius };
            glm::mat4 model = buildModelMatrix(pos, R, sides);

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
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode();

            float l = static_cast<float>(length);
            float r = static_cast<float>(radius);

            // 1) 半径スケール
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(r));

            // 2) 平行部の不足分
            float delta = 0.5f * (l - 2.0f * r);

            glm::mat4 T_up = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, +delta));
            glm::mat4 T_down = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -delta));

            // 3) ODE の姿勢・位置
            const T scaleXYZ[3] = { 1.0f, 1.0f, 1.0f }; // スケーリングは上でやる
            glm::mat4 W = buildModelMatrix(pos, R, scaleXYZ);

            // 4) 上半分・下半分を別々に描画
            glm::mat4 M_up = W * T_up * S;
            glm::mat4 M_down = W * T_down * S;

            drawMeshBasic(meshCapsule_, M_up, current_color);
            drawMeshBasic(meshCapsule_, M_down, current_color);

            if (use_shadows)
            {
                drawShadowMesh(meshCapsule_, M_up);
                drawShadowMesh(meshCapsule_, M_down);
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
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode(); // ライティングやカリングなど、既存の状態設定

            const T scaleXYZ[3] = {radius, radius, length};
            glm::mat4 model = buildModelMatrix(pos, R, scaleXYZ);

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
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode();

            // 1つの drawTriangles 呼び出しの中では pos,R は共通
            const T scaleXYZ[3] = {1.0f, 1.0f, 1.0f};
            glm::mat4 model = buildModelMatrix(pos, R, scaleXYZ);

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
                fatalError("drawing function called outside simulation loop");

            setupDrawingMode();

            const T scaleXYZ[3] = {1.0f, 1.0f, 1.0f};
            glm::mat4 model = buildModelMatrix(pos, R, scaleXYZ);
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

            // 旧 dsDrawConvex から OpenGL 呼び出し部分を移植
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glShadeModel(GL_FLAT);
            setTransform(pos, R);
            drawIndexedConvexPolyhedron(_planes, _planecount, _points, _pointcount, _polygons);
            glPopMatrix();
            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();
                setTransform(pos, R);
                drawIndexedConvexPolyhedron(_planes, _planecount, _points, _pointcount, _polygons);
                glPopMatrix();
                glPopMatrix();
                glDepthRange(0, 1);
            }
        }
        template <typename T>
        void drawLine(const T pos1[3], const T pos2[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 旧 dsDrawLine から OpenGL 呼び出し部分を移植
            setupDrawingMode();
            glColor4f(static_cast<float>(current_color[0]), static_cast<float>(current_color[1]), static_cast<float>(current_color[2]), static_cast<float>(current_color[3]));
            drawLineLocalImmediate<T>(pos1, pos2);

            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();

                drawLineLocalImmediate<T>(pos1, pos2);

                glPopMatrix();
                glDepthRange(0, 1);
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
        Mesh meshCapsule_; // カプセル用メッシュ
        Mesh meshTriangle_;
        Mesh meshTrianglesBatch_;

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

        // 影用：ライト方向と投影行列
        glm::vec3 shadowLightDir_; // ライトの方向（ワールド座標）
        glm::mat4 shadowProject_;  // 地面 z=0 への投影行列

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
        void buildSphereMeshForQuality(int quality, Mesh &dstMesh);
        void buildCylinderMeshForQuality(int quality, Mesh &dstMesh);
        void initTriangleMesh();
        void initTrianglesBatchMesh();
        void createPrimitiveMeshes();
        void setupDrawingMode();
        void setShadowDrawingMode();
        void setShadowTransform();
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

        // テンプレート関数群
        template <typename T>
        void normalizeVector3(T v[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            T len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (len <= 0.0f)
            {
                v[0] = 1;
                v[1] = 0;
                v[2] = 0;
            }
            else
            {
                len = 1.0f / (float)sqrt(len);
                v[0] *= len;
                v[1] *= len;
                v[2] *= len;
            }
        }
        template <typename T>
        void crossProduct3(T res[3], const T a[3], const T b[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            const T res_0 = a[1] * b[2] - a[2] * b[1];
            const T res_1 = a[2] * b[0] - a[0] * b[2];
            const T res_2 = a[0] * b[1] - a[1] * b[0];
            // Only assign after all the calculations are over to avoid incurring memory aliasing
            res[0] = res_0;
            res[1] = res_1;
            res[2] = res_2;
        }
        template <typename T>
        void setTransform(const T pos[3], const T R[12])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // ここで変換行列をセットする処理を実装
            using GLT = typename GLType<T>::type;
            GLT matrix[16];
            matrix[0] = R[0];
            matrix[1] = R[4];
            matrix[2] = R[8];
            matrix[3] = 0;
            matrix[4] = R[1];
            matrix[5] = R[5];
            matrix[6] = R[9];
            matrix[7] = 0;
            matrix[8] = R[2];
            matrix[9] = R[6];
            matrix[10] = R[10];
            matrix[11] = 0;
            matrix[12] = pos[0];
            matrix[13] = pos[1];
            matrix[14] = pos[2];
            matrix[15] = 1;
            glPushMatrix();
            if constexpr (std::is_same<T, float>::value)
                glMultMatrixf(matrix);
            else
                glMultMatrixd(matrix);
        }

        template <typename T>
        glm::mat4 buildModelMatrix(
            const T pos[3], const T R[12], const T sides[3])
        {
            glm::mat4 model(1.0f);

            // ODE R[12] → glm::mat4
            glm::mat4 rot(1.0f);
            rot[0][0] = R[0];
            rot[0][1] = R[1];
            rot[0][2] = R[2];
            rot[1][0] = R[4];
            rot[1][1] = R[5];
            rot[1][2] = R[6];
            rot[2][0] = R[8];
            rot[2][1] = R[9];
            rot[2][2] = R[10];

            model = glm::translate(model,
                                   glm::vec3((float)pos[0], (float)pos[1], (float)pos[2]));
            model *= rot;

            // 単位ボックスが [-0.5, 0.5]^3 なので 0.5 を掛ける
            model = glm::scale(model,
                               glm::vec3((float)sides[0],
                                         (float)sides[1],
                                         (float)sides[2]));

            return model;
        }

        template <typename T>
        void drawIndexedConvexPolyhedron(const T *_planes, const unsigned int _planecount,
                                         const T *_points, const unsigned int _pointcount,
                                         const unsigned int *_polygons)
        {
            unsigned int polyindex = 0;
            for (unsigned int i = 0; i < _planecount; ++i)
            {
                unsigned int pointcount = _polygons[polyindex];
                polyindex++;
                glBegin(GL_POLYGON);
                if constexpr (std::is_same<T, float>::value)
                {
                    glNormal3f(_planes[(i * 4) + 0],
                               _planes[(i * 4) + 1],
                               _planes[(i * 4) + 2]);
                }
                else
                {
                    glNormal3d(_planes[(i * 4) + 0],
                               _planes[(i * 4) + 1],
                               _planes[(i * 4) + 2]);
                }
                for (unsigned int j = 0; j < pointcount; ++j)
                {
                    if constexpr (std::is_same<T, float>::value)
                    {
                        glVertex3f(_points[_polygons[polyindex] * 3],
                                   _points[(_polygons[polyindex] * 3) + 1],
                                   _points[(_polygons[polyindex] * 3) + 2]);
                    }
                    else
                    {
                        glVertex3d(_points[_polygons[polyindex] * 3],
                                   _points[(_polygons[polyindex] * 3) + 1],
                                   _points[(_polygons[polyindex] * 3) + 2]);
                    }
                    polyindex++;
                }
                glEnd();
            }
        }
        template <typename T>
        void drawSphereShadow(const T px, const T py, const T pz,
                              const T radius)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // ここに球の影描画コードを移植
            // calculate shadow constants based on light vector
            static int init = 0;
            static T len2, len1, scale;
            if (!init)
            {
                len2 = LIGHTX * LIGHTX + LIGHTY * LIGHTY;
                len1 = 1.0f / (T)sqrt(len2);
                scale = (T)sqrt(len2 + 1);
                init = 1;
            }

            // map sphere center to ground plane based on light vector
            const T cx = px - LIGHTX * pz;
            const T cy = py - LIGHTY * pz;

            const T kx = 0.96592582628907f;
            const T ky = 0.25881904510252f;
            T x = radius, y = 0;

            glBegin(GL_TRIANGLE_FAN);
            for (int i = 0; i < 24; i++)
            {
                // for all points on circle, scale to elongated rotated shadow and draw
                T x2 = (LIGHTX * x * scale - LIGHTY * y) * len1 + cx;
                T y2 = (LIGHTY * x * scale + LIGHTX * y) * len1 + cy;
                if constexpr (std::is_same<T, float>::value)
                {
                    glTexCoord2f(x2 * ground_scale + ground_ofsx, y2 * ground_scale + ground_ofsy);
                    glVertex3f(x2, y2, 0);
                }
                else
                {
                    glTexCoord2d(x2 * ground_scale + ground_ofsx, y2 * ground_scale + ground_ofsy);
                    glVertex3d(x2, y2, 0);
                }

                // rotate [x,y] vector
                T xtmp = kx * x - ky * y;
                y = ky * x + kx * y;
                x = xtmp;
            }
            glEnd();
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
        void drawLineLocalImmediate(const T pos1[3], const T pos2[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 旧 drawLine を移植
            glDisable(GL_LIGHTING);
            glLineWidth(2);
            glShadeModel(GL_FLAT);
            glBegin(GL_LINES);
            if constexpr (std::is_same<T, float>::value)
            {
                glVertex3f(pos1[0], pos1[1], pos1[2]);
                glVertex3f(pos2[0], pos2[1], pos2[2]);
            }
            else if constexpr (std::is_same<T, double>::value)
            {
                glVertex3d(pos1[0], pos1[1], pos1[2]);
                glVertex3d(pos2[0], pos2[1], pos2[2]);
            }
            glEnd();
        }
    };

} // namespace ds_internal
