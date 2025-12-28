// drawstuffCompat.cpp - drawstuff-compatible graphics functions
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

/*
simple graphics.

the following command line flags can be used (typically under unix)
  -notex              Do not use any textures
  -noshadow[s]        Do not draw any shadows
  -pause              Start the simulation paused
  -texturepath <path> Inform an alternative textures path
*/

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include "drawstuffCompat.hpp"

namespace
{
    // 戻り値あり版
    template <typename Func, typename R>
    R with_app_or_default(Func &&func, R default_value)
    {
        ds_internal::DrawstuffApp &app = ds_internal::DrawstuffApp::instance();
        if (!app.isInsideSimulationLoop())
        {
            dsError("drawing function called outside simulation loop");
            return default_value;
        }
        return func(app);
    }

    // 戻り値なし版
    template <typename Func>
    void with_app(Func &&func)
    {
        ds_internal::DrawstuffApp &app = ds_internal::DrawstuffApp::instance();
        if (!app.isInsideSimulationLoop())
        {
            dsError("drawing function called outside simulation loop");
            return;
        }
        func(app);
    }
} // anonymous namespace

void dsStartGraphics(const int width, const int height, const dsFunctions *fn)
{
    with_app(
        [width, height, fn](ds_internal::DrawstuffApp &app)
        {
            app.startGraphics(width, height, fn);
        });
}

void dsStopGraphics()
{
    with_app(
        [](ds_internal::DrawstuffApp &app)
        {
            app.stopGraphics();
        });
}

void dsDrawFrame(const int width, const int height, const dsFunctions *fn, const int pause)
{
    with_app(
        [width, height, fn, pause](ds_internal::DrawstuffApp &app)
        {
            app.renderFrame(width, height, fn, pause);
        });
}

bool dsGetShadows()
{
    return with_app_or_default(
        [](ds_internal::DrawstuffApp &app)
        { return app.getUseShadows(); },
        false // ループ外のときのデフォルト値
    );
}

void dsSetShadows(const bool use_shadows_)
{
    with_app(
        [use_shadows_](ds_internal::DrawstuffApp &app)
        { app.setUseShadows(use_shadows_); });
}

bool dsGetTextures()
{
    return with_app_or_default(
        [](ds_internal::DrawstuffApp &app)
        { return app.getUseTextures(); },
        false // ループ外のときのデフォルト値
    );
}
void dsSetTextures(const bool use_textures_)
{
    with_app(
        [use_textures_](ds_internal::DrawstuffApp &app)
        { app.setUseTextures(use_textures_); });
}

//***************************************************************************
// C interface

extern "C" void dsSimulationLoop(const int argc, const char *const argv[],
                                 const int window_width, const int window_height,
                                 const dsFunctions *fn)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.runSimulation(argc, argv, window_width, window_height, fn);
}

extern "C" void dsSetViewpoint(const float xyz[3], const float hpr[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setViewpoint(xyz, hpr);
}

extern "C" void dsGetViewpoint(float xyz[3], float hpr[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.getViewpoint(xyz, hpr);
}

extern "C" void dsStop()
{
    with_app(
        [](ds_internal::DrawstuffApp &app)
        {
            app.stopSimulation();
        });
}

extern "C" void dsSetTexture(int texture_number)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setTexture(texture_number);
}

extern "C" void dsSetColor(const float red, const float green, const float blue)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.storeColor(red, green, blue, 1.0f);
}

extern "C" void dsSetColorAlpha(const float red, const float green, const float blue,
                                const float alpha)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.storeColor(red, green, blue, alpha);
}

// ---------- Box ----------
extern "C" void dsDrawBox(const float pos[3], const float R[12],
                          const float sides[3])
{
    with_app(
        [pos, R, sides](ds_internal::DrawstuffApp &app)
        {
            app.drawBox<float>(pos, R, sides);
        });
}

extern "C" void dsDrawBoxD(const double pos[3], const double R[12],
                           const double sides[3])
{
    with_app(
        [pos, R, sides](ds_internal::DrawstuffApp &app)
        {
            app.drawBox<double>(pos, R, sides);
        });
}

// ---------- Sphere ----------
extern "C" void dsDrawSphere(const float pos[3], const float R[12],
                             const float radius)
{
    with_app(
        [pos, R, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawSphere<float>(pos, R, radius);
        });
}

void dsDrawSphereD(const double pos[3], const double R[12], const double radius)
{
    with_app(
        [pos, R, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawSphere<double>(pos, R, radius);
        });
}

// ---------- Convex ----------
extern "C" void dsDrawConvex(const float pos[3], const float R[12],
                             const float *_planes, unsigned int _planecount,
                             const float *_points, unsigned int _pointcount,
                             const unsigned int *_polygons)
{
    with_app(
        [pos, R, _planes, _planecount, _points, _pointcount, _polygons](ds_internal::DrawstuffApp &app)
        {
            app.drawConvex<float>(pos, R, _planes, _planecount, _points, _pointcount, _polygons);
        });
}

extern "C" void dsDrawConvexD(const double pos[3], const double R[12],
                              const double *_planes, const unsigned int _planecount,
                              const double *_points, const unsigned int _pointcount,
                              const unsigned int *_polygons)
{
    with_app(
        [pos, R, _planes, _planecount, _points, _pointcount, _polygons](ds_internal::DrawstuffApp &app)
        {
            app.drawConvex<double>(pos, R, _planes, _planecount, _points, _pointcount, _polygons);
        });
}

// ---------- Triangle ----------
extern "C" void dsDrawTriangle(const float pos[3], const float R[12],
                               const float *v0, const float *v1,
                               const float *v2, const int solid)
{
    with_app(
        [pos, R, v0, v1, v2, solid](ds_internal::DrawstuffApp &app)
        {
            app.drawTriangle<float>(pos, R, v0, v1, v2, solid);
        });
}

extern "C" void dsDrawTriangleD(const double pos[3], const double R[12],
                     const double *v0, const double *v1,
                     const double *v2, int solid)
{
    with_app(
        [pos, R, v0, v1, v2, solid](ds_internal::DrawstuffApp &app)
        {
            app.drawTriangle<double>(pos, R, v0, v1, v2, solid);
        });
}

extern "C" void dsDrawTriangles(const float pos[3], const float R[12],
                                const float *v, const int n, const int solid)
{
    with_app(
        [pos, R, v, n, solid](ds_internal::DrawstuffApp &app)
        {
            app.drawTriangles<float>(pos, R, v, n, solid);
        });
}

extern "C" void dsDrawTrianglesD(const double pos[3], const double R[12],
                                 const double *v, int n, int solid)
{
    with_app(
        [pos, R, v, n, solid](ds_internal::DrawstuffApp &app)
        {
            app.drawTriangles<double>(pos, R, v, n, solid);
        });
}

// ---------- Cylinder / Capsule ----------
extern "C" void dsDrawCylinder(const float pos[3], const float R[12],
                               const float length, const float radius)
{
    with_app(
        [pos, R, length, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawCylinder<float>(pos, R, length, radius);
        });
}

extern "C" void dsDrawCylinderD(const double pos[3], const double R[12],
                     const double length, const double radius)
{
    with_app(
        [pos, R, length, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawCylinder<double>(pos, R, length, radius);
        });
}

extern "C" void dsDrawCapsule(const float pos[3], const float R[12],
                              const float length, const float radius)
{
    with_app(
        [pos, R, length, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawCapsule<float>(pos, R, length, radius);
        });
}

extern "C" void dsDrawCapsuleD(const double pos[3], const double R[12],
                                const double length, const double radius)
{
    with_app(
        [pos, R, length, radius](ds_internal::DrawstuffApp &app)
        {
            app.drawCapsule<double>(pos, R, length, radius);
        });
}

// ---------- Line ----------
extern "C" void dsDrawLine(const float pos1[3], const float pos2[3])
{
    with_app(
        [pos1, pos2](ds_internal::DrawstuffApp &app)
        {
            app.drawLine<float>(pos1, pos2);
        });
}

extern "C" void dsDrawLineD(const double _pos1[3], const double _pos2[3])
{
    with_app(
        [_pos1, _pos2](ds_internal::DrawstuffApp &app)
        {
            app.drawLine<double>(_pos1, _pos2);
        });
}

void dsSetSphereQuality(const int n)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setSphereQuality(n);
}

void dsSetCapsuleQuality(const int n)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setCapsuleQuality(n);
}

void dsSetDrawMode(const int mode)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setDrawMode(mode);
}

// migrated from x11.cpp

static void printMessage(const char *msg1, const char *msg2, va_list ap)
{
    fflush(stderr);
    fflush(stdout);
    fprintf(stderr, "\n%s: ", msg1);
    vfprintf(stderr, msg2, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}

extern "C" void dsError(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    printMessage("Error", msg, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

extern "C" void dsDebug(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    printMessage("INTERNAL ERROR", msg, ap);
    va_end(ap);
    // *((char *)0) = 0;	 ... commit SEGVicide ?
    abort();
}

extern "C" void dsStartCaptureFrames()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setWriteFrames(true);
}

extern "C" void dsStopCaptureFrames()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.setWriteFrames(false);
}

extern "C" void dsFlipCaptureFrames()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.toggleWriteFrames();
}

extern "C" void dsPause()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    app.togglePauseMode();
}

// ========== Additional functions for fast drawing of trimeshes ===========
// These functions are not part of the original drawstuff API.
// They are provided for efficiency when drawing large numbers of
// triangles, e.g., from a trimesh collision object.
// ========================================================================
extern "C" dsMeshHandle dsRegisterIndexedMesh(
    const std::vector<float> &vertices, const std::vector<unsigned int> &indices)
{
    dsMeshHandle h;
    
    // フラット配列 → std::vector に詰め直し
    std::vector<float> v(vertices);
    std::vector<unsigned int> idx(indices);

    auto &app = ds_internal::DrawstuffApp::instance();
    h.id = app.registerIndexedMesh(v, idx);
    return h;
}

extern "C" void dsDrawRegisteredMesh(
    const dsMeshHandle handle,
    const float pos[3], const float R[12],
    const bool solid)
{
    with_app(
        [handle, pos, R, solid](ds_internal::DrawstuffApp &app)
        {
            app.drawRegisteredMesh(handle.id, pos, R, solid);
        });
}

// ========================================================================
