// drawstuff_core.hpp (仮称) – 内部専用ヘッダ
#pragma once

#include <memory>
#include <array>
#include <cmath>
#include <X11/Xlib.h>  // XEvent など
#include <X11/Xatom.h> // XA_STRING, XA_WM_NAME など
#include <GL/gl.h>
#include <GL/glu.h>

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

        // setters/getters
        // 球などの表示品質設定
        void setSphereQuality(const int n) { sphere_quality = n; }
        void setCapsuleQuality(const int n) { capsule_quality = n; }
        void setCappedCylinderQuality(const int n) { capped_cylinder_quality = n; }
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
            
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            if constexpr (std::is_same<T, float>::value)
            {
                glRotatef(90, 0, 0, 1);
                glRotatef(90, 0, 1, 0);
                glRotatef(r, 1, 0, 0);
                glRotatef(p, 0, 1, 0);
                glRotatef(-h, 0, 0, 1);
                glTranslatef(-x, -y, -z);
            }
            else
            {
                glRotated(90, 0, 0, 1);
                glRotated(90, 0, 1, 0);
                glRotated(r, 1, 0, 0);
                glRotated(p, 0, 1, 0);
                glRotated(-h, 0, 0, 1);
                glTranslated(-x, -y, -z);
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
            setupDrawingMode();
            glShadeModel(GL_FLAT);
            setTransform(pos, R);
            drawBoxCentered(sides);
            glPopMatrix();

            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();
                setTransform(pos, R);
                drawBoxCentered(sides);
                glPopMatrix();
                glPopMatrix();
                glDepthRange(0, 1);
            }
        }
        template <typename T>
        void drawSphere(const T pos[3], const T R[12],
                        const T radius)
        {
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glEnable(GL_NORMALIZE);
            glShadeModel(GL_SMOOTH);
            setTransform(pos, R);
            glScaled(radius, radius, radius);
            drawUnitSphere();
            glPopMatrix();
            glDisable(GL_NORMALIZE);

            // draw shadows
            // In the original code, shadows for spheres were drawn using a
            // specialized function drawSphereShadow().
            // if (use_shadows)
            // {
            //     glDisable(GL_LIGHTING);
            //     if (use_textures)
            //     {
            //         ground_texture->bind(1);
            //         glEnable(GL_TEXTURE_2D);
            //         glDisable(GL_TEXTURE_GEN_S);
            //         glDisable(GL_TEXTURE_GEN_T);
            //         glColor3f(SHADOW_INTENSITY, SHADOW_INTENSITY, SHADOW_INTENSITY);
            //     }
            //     else
            //     {
            //         glDisable(GL_TEXTURE_2D);
            //         glColor3f(GROUND_R * SHADOW_INTENSITY, GROUND_G * SHADOW_INTENSITY,
            //                   GROUND_B * SHADOW_INTENSITY);
            //     }
            //     glShadeModel(GL_FLAT);
            //     glDepthRange(0, 0.9999);
            //     drawSphereShadow(pos[0], pos[1], pos[2], radius);
            //     glDepthRange(0, 1);
            // }
            // Here, for better consistency
            // with other shapes, we use the same approach as for capsules and
            // cylinders, drawing the shadow as a flattened version of the object.
            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();
                setTransform(pos, R);
                glScaled(radius, radius, radius);
                drawUnitSphere();
                glPopMatrix();
                glPopMatrix();
                glDepthRange(0, 1);
            }
        }
        template <typename T>
        void drawCapsule(const T pos[3], const T R[12],
                         const T length, const T radius)
        {
            // 旧 dsDrawCapsule から OpenGL 呼び出し部分を移植
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glShadeModel(GL_SMOOTH);
            setTransform(pos, R);
            drawCapsuleCenteredX(length, radius);
            glPopMatrix();

            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();
                setTransform(pos, R);
                drawCapsuleCenteredX(length, radius);
                glPopMatrix();
                glPopMatrix();
                glDepthRange(0, 1);
            }
        }

        template <typename T>
        void drawCylinder(const T pos[3], const T R[12],
                                        const T length, const T radius)
        {
            // 旧 dsDrawCylinder から OpenGL 呼び出し部分を移植
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glShadeModel(GL_SMOOTH);
            setTransform(pos, R);
            drawCylinderCenteredX<T>(length, radius, 0);
            glPopMatrix();

            if (use_shadows)
            {
                setShadowDrawingMode();
                setShadowTransform();
                setTransform(pos, R);
                drawCylinderCenteredX<T>(length, radius, 0);
                glPopMatrix();
                glPopMatrix();
                glDepthRange(0, 1);
            }
        }
        template <typename T>
        void drawTriangle(const T pos[3], const T R[12],
                          const T *v0, const T *v1,
                          const T *v2, const int solid)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 旧 dsDrawTriangle から OpenGL 呼び出し部分を移植
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glShadeModel(GL_FLAT);
            setTransform(pos, R);
            drawTriangleWithAutoNormal<T>(v0, v1, v2, solid);
            glPopMatrix();
        }
        template <typename T>
        void drawTriangles(const T pos[3], const T R[12],
                           const T *v, const int n, const int solid)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 旧 dsDrawTriangles から OpenGL 呼び出し部分を移植
            if (current_state != SIM_STATE_DRAWING)
                fatalError("drawing function called outside simulation loop");
            setupDrawingMode();
            glShadeModel(GL_FLAT);
            setTransform(pos, R);
            int i;
            for (i = 0; i < n; ++i, v += 9)
                drawTriangleWithAutoNormal<T>(v, v + 3, v + 6, solid);
            glPopMatrix();
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

        // 旧グローバル変数をメンバに移していく
        SimulationState current_state;
        bool use_textures;
        bool use_shadows;
        bool writeframes;
        bool pausemode;

        std::array<float, 3> view_xyz;
        std::array<float, 3> view_hpr;

        std::array<float, 4> current_color;
        int texture_id;

        dsFunctions callbacks_storage_;       // 渡された dsFunctions を保持（実体）
        const dsFunctions *callbacks_ = nullptr;

        int sphere_quality = 3;
        int capsule_quality = 3;
        int capped_cylinder_quality = 3;
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
        void setupDrawingMode();
        void setShadowDrawingMode();
        void setShadowTransform();
        void drawSky(const float view_xyz[3]);
        void drawGround();
        void drawPyramidGrid();
        void drawUnitSphere();

        void motion(const int mode, const int deltax, const int deltay);
        void wrapCameraAngles();

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
        void drawBoxCentered(const T sides[3])
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // 原点を中心とし，座標軸に沿った直方体を描画
            T lx = sides[0] * 0.5f;
            T ly = sides[1] * 0.5f;
            T lz = sides[2] * 0.5f;

            // sides
            glBegin(GL_TRIANGLE_STRIP);
            if constexpr (std::is_same<T, float>::value)
            {
                glNormal3f(0, 0, 0);
                glNormal3f(-1, 0, 0);
                glVertex3f(-lx, -ly, -lz);
                glVertex3f(-lx, -ly, lz);
                glVertex3f(-lx, ly, -lz);
                glVertex3f(-lx, ly, lz);
                glNormal3f(0, 1, 0);
                glVertex3f(lx, ly, -lz);
                glVertex3f(lx, ly, lz);
                glNormal3f(1, 0, 0);
                glVertex3f(lx, -ly, -lz);
                glVertex3f(lx, -ly, lz);
                glNormal3f(0, -1, 0);
                glVertex3f(-lx, -ly, -lz);
                glVertex3f(-lx, -ly, lz);
            }
            else
            {
                glNormal3d(0, 0, 0);
                glNormal3d(-1, 0, 0);
                glVertex3d(-lx, -ly, -lz);
                glVertex3d(-lx, -ly, lz);
                glVertex3d(-lx, ly, -lz);
                glVertex3d(-lx, ly, lz);
                glNormal3d(0, 1, 0);
                glVertex3d(lx, ly, -lz);
                glVertex3d(lx, ly, lz);
                glNormal3d(1, 0, 0);
                glVertex3d(lx, -ly, -lz);
                glVertex3d(lx, -ly, lz);
                glNormal3d(0, -1, 0);
                glVertex3d(-lx, -ly, -lz);
                glVertex3d(-lx, -ly, lz);
            }
            glEnd();

            // top face
            glBegin(GL_TRIANGLE_FAN);
            if constexpr (std::is_same<T, float>::value)
            {
                glNormal3f(0, 0, 1);
                glVertex3f(-lx, -ly, lz);
                glVertex3f(lx, -ly, lz);
                glVertex3f(lx, ly, lz);
                glVertex3f(-lx, ly, lz);
            }
            else
            {
                glNormal3d(0, 0, 1);
                glVertex3d(-lx, -ly, lz);
                glVertex3d(lx, -ly, lz);
                glVertex3d(lx, ly, lz);
                glVertex3d(-lx, ly, lz);
            }
            glEnd();

            // bottom face
            glBegin(GL_TRIANGLE_FAN);
            if constexpr (std::is_same<T, float>::value)
            {
                glNormal3f(0, 0, -1);
                glVertex3f(-lx, -ly, -lz);
                glVertex3f(-lx, ly, -lz);
                glVertex3f(lx, ly, -lz);
                glVertex3f(lx, -ly, -lz);
            }
            else
            {
                glNormal3d(0, 0, -1);
                glVertex3d(-lx, -ly, -lz);
                glVertex3d(-lx, ly, -lz);
                glVertex3d(lx, ly, -lz);
                glVertex3d(lx, -ly, -lz);
            }
            glEnd();
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
        void drawCapsuleCenteredX(const T length, const T radius)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // ここにカプセル描画コードを移植
            int i, j;
            T tmp, nx, ny, nz, start_nx, start_ny, a, ca, sa;
            // number of sides to the cylinder (divisible by 4):
            const int n = capped_cylinder_quality * 4;

            const T l = length * 0.5f;
            const T r = radius;
            a = T(M_PI * 2.0) / T(n);
            sa = (T)sin(a);
            ca = (T)cos(a);

            // draw cylinder body
            ny = 1;
            nz = 0; // normal vector = (0,ny,nz)
            glBegin(GL_TRIANGLE_STRIP);
            for (i = 0; i <= n; i++)
            {
                if constexpr (std::is_same<T, float>::value)
                {
                    glNormal3f(ny, nz, 0);
                    glVertex3f(ny * r, nz * r, l);
                    glNormal3f(ny, nz, 0);
                    glVertex3f(ny * r, nz * r, -l);
                }
                else
                {
                    glNormal3d(ny, nz, 0);
                    glVertex3d(ny * r, nz * r, l);
                    glNormal3d(ny, nz, 0);
                    glVertex3d(ny * r, nz * r, -l);
                }
                // rotate ny,nz
                tmp = ca * ny - sa * nz;
                nz = sa * ny + ca * nz;
                ny = tmp;
            }
            glEnd();

            // draw first cylinder cap
            start_nx = 0;
            start_ny = 1;
            for (j = 0; j < (n / 4); j++)
            {
                // get start_n2 = rotated start_n
                T start_nx2 = ca * start_nx + sa * start_ny;
                T start_ny2 = -sa * start_nx + ca * start_ny;
                // get n=start_n and n2=start_n2
                nx = start_nx;
                ny = start_ny;
                nz = 0;
                T nx2 = start_nx2, ny2 = start_ny2, nz2 = 0;
                glBegin(GL_TRIANGLE_STRIP);
                for (i = 0; i <= n; i++)
                {
                    if constexpr (std::is_same<T, float>::value)
                    {
                        glNormal3f(ny2, nz2, nx2);
                        glVertex3f(ny2 * r, nz2 * r, l + nx2 * r);
                        glNormal3f(ny, nz, nx);
                        glVertex3f(ny * r, nz * r, l + nx * r);
                    }
                    else
                    {
                        glNormal3d(ny2, nz2, nx2);
                        glVertex3d(ny2 * r, nz2 * r, l + nx2 * r);
                        glNormal3d(ny, nz, nx);
                        glVertex3d(ny * r, nz * r, l + nx * r);
                    }
                    // rotate n,n2
                    tmp = ca * ny - sa * nz;
                    nz = sa * ny + ca * nz;
                    ny = tmp;
                    tmp = ca * ny2 - sa * nz2;
                    nz2 = sa * ny2 + ca * nz2;
                    ny2 = tmp;
                }
                glEnd();
                start_nx = start_nx2;
                start_ny = start_ny2;
            }

            // draw second cylinder cap
            start_nx = 0;
            start_ny = 1;
            for (j = 0; j < (n / 4); j++)
            {
                // get start_n2 = rotated start_n
                float start_nx2 = ca * start_nx - sa * start_ny;
                float start_ny2 = sa * start_nx + ca * start_ny;
                // get n=start_n and n2=start_n2
                nx = start_nx;
                ny = start_ny;
                nz = 0;
                float nx2 = start_nx2, ny2 = start_ny2, nz2 = 0;
                glBegin(GL_TRIANGLE_STRIP);
                for (i = 0; i <= n; i++)
                {
                    glNormal3d(ny, nz, nx);
                    glVertex3d(ny * r, nz * r, -l + nx * r);
                    glNormal3d(ny2, nz2, nx2);
                    glVertex3d(ny2 * r, nz2 * r, -l + nx2 * r);
                    // rotate n,n2
                    tmp = ca * ny - sa * nz;
                    nz = sa * ny + ca * nz;
                    ny = tmp;
                    tmp = ca * ny2 - sa * nz2;
                    nz2 = sa * ny2 + ca * nz2;
                    ny2 = tmp;
                }
                glEnd();
                start_nx = start_nx2;
                start_ny = start_ny2;
            }
        }
        template <typename T>
        void drawCylinderCenteredX(const T length, const T radius, const T zoffset)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // ここに円柱描画コードを移植
            int i;
            T tmp, ny, nz, a, ca, sa;
            const int n = 24; // number of sides to the cylinder (divisible by 4)

            const T l = length * 0.5;
            const T r = radius;
            a = T(M_PI * 2.0) / T(n);
            sa = T(sin(a));
            ca = T(cos(a));

            // draw cylinder body
            ny = 1;
            nz = 0; // normal vector = (0,ny,nz)
            glBegin(GL_TRIANGLE_STRIP);
            for (i = 0; i <= n; i++)
            {
                if constexpr (std::is_same<T, float>::value)
                {
                    glNormal3f(ny, nz, 0);
                    glVertex3f(ny * r, nz * r, l + zoffset);
                    glNormal3f(ny, nz, 0);
                    glVertex3f(ny * r, nz * r, -l + zoffset);
                }
                else
                {
                    glNormal3d(ny, nz, 0);
                    glVertex3d(ny * r, nz * r, l + zoffset);
                    glNormal3d(ny, nz, 0);
                    glVertex3d(ny * r, nz * r, -l + zoffset);
                }
                // rotate ny,nz
                tmp = ca * ny - sa * nz;
                nz = sa * ny + ca * nz;
                ny = tmp;
            }
            glEnd();

            // draw top cap
            glShadeModel(GL_FLAT);
            ny = 1;
            nz = 0; // normal vector = (0,ny,nz)
            glBegin(GL_TRIANGLE_FAN);
            if constexpr (std::is_same<T, float>::value)
            {
                glNormal3f(0, 0, 1);
                glVertex3f(0, 0, l + zoffset);
            }
            else
            {
                glNormal3d(0, 0, 1);
                glVertex3d(0, 0, l + zoffset);
            }
            for (i = 0; i <= n; i++)
            {
                if (i == 1 || i == n / 2 + 1)
                    setColor(current_color[0] * 0.75f, current_color[1] * 0.75f, current_color[2] * 0.75f, current_color[3]);
                if constexpr (std::is_same<T, float>::value)
                {
                    glNormal3f(0, 0, 1);
                    glVertex3f(ny * r, nz * r, l + zoffset);
                }
                else
                {
                    glNormal3d(0, 0, 1);
                    glVertex3d(ny * r, nz * r, l + zoffset);
                }
                if (i == 1 || i == n / 2 + 1)
                    setColor(current_color[0], current_color[1], current_color[2], current_color[3]);
                // rotate ny,nz
                tmp = ca * ny - sa * nz;
                nz = sa * ny + ca * nz;
                ny = tmp;
            }
            glEnd();

            // draw bottom cap
            ny = 1;
            nz = 0; // normal vector = (0,ny,nz)
            glBegin(GL_TRIANGLE_FAN);
            glNormal3d(0, 0, -1);
            glVertex3d(0, 0, -l + zoffset);
            for (i = 0; i <= n; i++)
            {
                if (i == 1 || i == n / 2 + 1)
                    setColor(current_color[0] * 0.75f, current_color[1] * 0.75f, current_color[2] * 0.75f, current_color[3]);
                if constexpr (std::is_same<T, float>::value)
                {
                    glNormal3f(0, 0, -1);
                    glVertex3f(0, 0, -l + zoffset);
                }
                else
                {
                    glNormal3d(0, 0, -1);
                    glVertex3d(0, 0, -l + zoffset);
                }
                if (i == 1 || i == n / 2 + 1)
                    setColor(current_color[0], current_color[1], current_color[2], current_color[3]);
                // rotate ny,nz
                tmp = ca * ny + sa * nz;
                nz = -sa * ny + ca * nz;
                ny = tmp;
            }
            glEnd();
        }
        template <typename T>
        void drawTriangleWithAutoNormal(const T *v0, const T *v1,
                                        const T *v2, const int solid)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            // ここに三角形描画コードを移植
            T u[3], v[3], normal[3];
            u[0] = v1[0] - v0[0];
            u[1] = v1[1] - v0[1];
            u[2] = v1[2] - v0[2];
            v[0] = v2[0] - v0[0];
            v[1] = v2[1] - v0[1];
            v[2] = v2[2] - v0[2];
            crossProduct3(normal, u, v);
            normalizeVector3(normal);

            glBegin(solid ? GL_TRIANGLES : GL_LINE_STRIP);

            if constexpr (std::is_same<T, float>::value)
            {
                glNormal3fv(normal);
                glVertex3fv(v0);
                glVertex3fv(v1);
                glVertex3fv(v2);
            }
            else if constexpr (std::is_same<T, double>::value)
            {
                glNormal3dv(normal);
                glVertex3dv(v0);
                glVertex3dv(v1);
                glVertex3dv(v2);
            }
            glEnd();
        }
        template <typename T>
        void drawUnitTriangles(const T pos[3], const T R[12],
                               const T *v, int n, int solid);
        template <typename T>
        void drawPatch(const T p1[3], const T p2[3], const T p3[3], const int level)
        {
            static_assert(
                std::is_same<T, float>::value || std::is_same<T, double>::value,
                "T must be float or double");

            int i;
            if (level > 0)
            {
                T q1[3], q2[3], q3[3]; // sub-vertices
                for (i = 0; i < 3; i++)
                {
                    q1[i] = 0.5f * (p1[i] + p2[i]);
                    q2[i] = 0.5f * (p2[i] + p3[i]);
                    q3[i] = 0.5f * (p3[i] + p1[i]);
                }
                T length1 = (T)(1.0 / sqrt(q1[0] * q1[0] + q1[1] * q1[1] + q1[2] * q1[2]));
                T length2 = (T)(1.0 / sqrt(q2[0] * q2[0] + q2[1] * q2[1] + q2[2] * q2[2]));
                T length3 = (T)(1.0 / sqrt(q3[0] * q3[0] + q3[1] * q3[1] + q3[2] * q3[2]));
                for (i = 0; i < 3; i++)
                {
                    q1[i] *= length1;
                    q2[i] *= length2;
                    q3[i] *= length3;
                }
                drawPatch(p1, q1, q3, level - 1);
                drawPatch(q1, p2, q2, level - 1);
                drawPatch(q1, q2, q3, level - 1);
                drawPatch(q3, q2, p3, level - 1);
            }
            else if constexpr (std::is_same<T, float>::value)
            {
                glNormal3f(p1[0], p1[1], p1[2]);
                glVertex3f(p1[0], p1[1], p1[2]);
                glNormal3f(p2[0], p2[1], p2[2]);
                glVertex3f(p2[0], p2[1], p2[2]);
                glNormal3f(p3[0], p3[1], p3[2]);
                glVertex3f(p3[0], p3[1], p3[2]);
            }
            else if constexpr (std::is_same<T, double>::value)
            {
                glNormal3d(p1[0], p1[1], p1[2]);
                glVertex3d(p1[0], p1[1], p1[2]);
                glNormal3d(p2[0], p2[1], p2[2]);
                glVertex3d(p2[0], p2[1], p2[2]);
                glNormal3d(p3[0], p3[1], p3[2]);
                glVertex3d(p3[0], p3[1], p3[2]);
            }
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
