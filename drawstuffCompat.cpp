// drawstuffCompat.h - drawstuff-compatible graphics functions
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

/*

simple graphics.

the following command line flags can be used (typically under unix)
  -notex              Do not use any textures
  -noshadow[s]        Do not draw any shadows
  -pause              Start the simulation paused
  -texturepath <path> Inform an alternative textures path

TODO
----

manage openGL state changes better

*/

//***************************************************************************
// motion model

// call this to update the current camera position. the bits in 'mode' say
// if the left (1), middle (2) or right (4) mouse button is pressed, and
// (deltax,deltay) is the amount by which the mouse pointer has moved.

//***************************************************************************
// drawing loop stuff

// the current state:
//    0 = uninitialized
//    1 = dsSimulationLoop() called
//    2 = dsDrawFrame() called

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include "drawstuffCompat.hpp"


// textures and shadows
static int use_textures = 1; // 1 if textures to be drawn
static int use_shadows = 1;  // 1 if shadows to be drawn

#if !defined(macintosh) || defined(ODE_PLATFORM_OSX)

void dsStartGraphics(const int width, const int height, const dsFunctions *fn)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.startGraphics(width, height, fn);
}

#else // macintosh

void dsStartGraphics(int width, int height, dsFunctions *fn)
{

    // All examples build into the same dir
    char *prefix = "::::drawstuff:textures";
    char *s = (char *)alloca(strlen(prefix) + 20);

    strcpy(s, prefix);
    strcat(s, ":sky.ppm");
    sky_texture = new Texture(s);

    strcpy(s, prefix);
    strcat(s, ":ground.ppm");
    ground_texture = new Texture(s);

    strcpy(s, prefix);
    strcat(s, ":wood.ppm");
    wood_texture = new Texture(s);
}

#endif

void dsStopGraphics()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.stopGraphics();
}

void dsDrawFrame(const int width, const int height, const dsFunctions *fn, const int pause)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawFrame(width, height, fn, pause);
}

int dsGetShadows()
{
    return use_shadows;
}

void dsSetShadows(int a)
{
    use_shadows = (a != 0);
}

int dsGetTextures()
{
    return use_textures;
}

void dsSetTextures(int a)
{
    use_textures = (a != 0);
}

//***************************************************************************
// C interface

extern "C" void dsSimulationLoop(const int argc, const char *const argv[],
                                 const int window_width, const int window_height,
                                 const dsFunctions *fn)
{
    ds_internal::DrawstuffApp::instance()
        .runSimulation(argc, argv, window_width, window_height, fn);
}

extern "C" void dsSetViewpoint(const float xyz[3], const float hpr[3])
{
    ds_internal::DrawstuffApp::instance()
        .setViewpoint(xyz, hpr);
}

extern "C" void dsGetViewpoint(float xyz[3], float hpr[3])
{
    ds_internal::DrawstuffApp::instance()
        .getViewpoint(xyz, hpr);
}

extern "C" void dsStop()
{
    ds_internal::DrawstuffApp::instance().stopSimulation();
}

extern "C" void dsSetTexture(int texture_number)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setTexture(texture_number);
}

extern "C" void dsSetColor(const float red, const float green, const float blue)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.storeColor(red, green, blue, 1.0f);
}

extern "C" void dsSetColorAlpha(const float red, const float green, const float blue,
                                const float alpha)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.storeColor(red, green, blue, alpha);
}

// ---------- Box ----------
extern "C" void dsDrawBox(const float pos[3], const float R[12],
                          const float sides[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawBox<float>(pos, R, sides);
}

extern "C" void dsDrawBoxD(const double pos[3], const double R[12],
                           const double sides[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawBox<double>(pos, R, sides);
}

// ---------- Sphere ----------
extern "C" void dsDrawSphere(const float pos[3], const float R[12],
                             const float radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawSphere<float>(pos, R, radius);
}

void dsDrawSphereD(const double pos[3], const double R[12], const double radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawSphere<double>(pos, R, radius);
}

// ---------- Convex ----------
extern "C" void dsDrawConvex(const float pos[3], const float R[12],
                             const float *_planes, unsigned int _planecount,
                             const float *_points, unsigned int _pointcount,
                             const unsigned int *_polygons)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawConvex<float>(pos, R, _planes, _planecount, _points, _pointcount, _polygons);
}

extern "C" void dsDrawConvexD(const double pos[3], const double R[12],
                              const double *_planes, const unsigned int _planecount,
                              const double *_points, const unsigned int _pointcount,
                              const unsigned int *_polygons)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawConvex<double>(pos, R, _planes, _planecount, _points, _pointcount, _polygons);
}

// ---------- Triangle ----------
extern "C" void dsDrawTriangle(const float pos[3], const float R[12],
                               const float *v0, const float *v1,
                               const float *v2, const int solid)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawTriangle<float>(pos, R, v0, v1, v2, solid);
}

extern "C" void dsDrawTriangleD(const double pos[3], const double R[12],
                     const double *v0, const double *v1,
                     const double *v2, int solid)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawTriangle<double>(pos, R, v0, v1, v2, solid);
}

extern "C" void dsDrawTriangles(const float pos[3], const float R[12],
                                const float *v, const int n, const int solid)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawTriangles<float>(pos, R, v, n, solid);
}

extern "C" void dsDrawTrianglesD(const double pos[3], const double R[12],
                                 const double *v, int n, int solid)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawTriangles<double>(pos, R, v, n, solid);
}

// ---------- Cylinder / Capsule ----------
extern "C" void dsDrawCylinder(const float pos[3], const float R[12],
                               const float length, const float radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawCylinder<float>(pos, R, length, radius);
}

extern "C" void dsDrawCylinderD(const double pos[3], const double R[12],
                     const double length, const double radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawCylinder<double>(pos, R, length, radius);
}

extern "C" void dsDrawCapsule(const float pos[3], const float R[12],
                              const float length, const float radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawCapsule<float>(pos, R, length, radius);
}

extern "C" void dsDrawCapsuleD(const double pos[3], const double R[12],
                                const double length, const double radius)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawCapsule<double>(pos, R, length, radius);
}

// ---------- Line ----------
extern "C" void dsDrawLine(const float pos1[3], const float pos2[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawLine<float>(pos1, pos2);
}

extern "C" void dsDrawLineD(const double _pos1[3], const double _pos2[3])
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.drawLine<double>(_pos1, _pos2);
}

void dsSetSphereQuality(const int n)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setSphereQuality(n);
}

void dsSetCapsuleQuality(const int n)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setCapsuleQuality(n);
}

void dsSetDrawMode(const int mode)
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
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
    exit(1);
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
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setWriteFrames(true);
}

extern "C" void dsStopCaptureFrames()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setWriteFrames(false);
}

extern "C" void dsFlipCaptureFrames()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.toggleWriteFrames();
}

extern "C" void dsPause()
{
    auto &app = ds_internal::DrawstuffApp::instance();
    if (!app.isInsideSimulationLoop())
    {
        dsError("drawing function called outside simulation loop");
        return;
    }
    app.setPauseMode(true);
}
