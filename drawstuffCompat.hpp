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

/** @defgroup drawstuff DrawStuff
 *
 * DrawStuff is a library for rendering simple 3D objects in a virtual
 * environment, for the purposes of demonstrating the features of ODE.
 * It is provided for demonstration purposes and is no way meant to compete
 * with industrial strength 3D rendering engines.
 *
 * A number of premade models are rendered, with images of the models
 * textured onto the 3D objects. If texture support is not available on
 * your system, plain colors are used instead. The texture option is offered
 * for debugging purposes, so you can watch the objects rotate as they move.
 *
 * Drawstuff is completely independent of ODE, except for a few places where
 * it was just convenient to have DMatrix3 defined.
 *
 * Using this library should be fairly simple:
 * (1) You will need the GLUT and SDL packages in order to compile it,
 *     but these are installed with most Linux distributions or can be
 *     downloaded from the web.
 * (2) On MS-Windows systems you will need the GLUT and SDL DLLs, as
 *     well as a compiler that supports OpenGL.
 * (3) To start drawing, call dsSimulationLoop().  This routine will run
 *     your simulation and repeatedly call your draw function to redraw the
 *     scenes.
 * (4) Your simulation and draw functions will call the dBodyGet*() and
 *     dGeomGet*() primitives to discover the positions of the objects.
 * (5) For each object, call one of the dsDrawXXX() functions to draw it.
 * (6) Keystrokes in the main window and especially in the 3D drawing
 *     window cause character codes to be sent back to the command loop.
 *
 * Usage instructions:
 * - A command key function can be supplied by modifying the dsFunctions
 *   structure and passing it to dsSimulationLoop().
 * - Use the mouse to move the camera; various button combinations are
 *   recognized.
 *   - Left button: rotate camera
 *   - Middle button: move camera in XY
 *   - Right button: move camera in Z
 * - The camera can be moved with the numeric keypad:
 *   - 8: move forward
 *   - 2: move backward
 *   - 4: move left
 *   - 6: move right
 *   - 9: move up
 *   - 3: move down
 *
 * @{
 */

#ifndef __DRAWSTUFF_H__
#define __DRAWSTUFF_H__

#include "drawstuff_core.hpp" // dsFunctions などを使うため

/* Define a DLL export symbol for those platforms that need it */
#if defined(ODE_PLATFORM_WINDOWS)
#if defined(DS_DLL)
#define DS_API __declspec(dllexport)
#elif !defined(DS_LIB)
#define DS_DLL_API __declspec(dllimport)
#endif
#endif

#if !defined(DS_API)
#define DS_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // This code is intended to be compatible with drawstuff version 0.2.
    constexpr int DS_VERSION = 0x0002;

    /* key command codes, used by command() function */
    enum DS_COMMAND_CODE
    {
        DS_CMD_NONE = 0, /* no command */
        DS_CMD_LEFT,
        DS_CMD_RIGHT,
        DS_CMD_FORWARD,
        DS_CMD_BACKWARD,
        DS_CMD_UP,
        DS_CMD_DOWN,
        DS_CMD_TOGGLE_PAUSE
    };

    /**
     * @brief Does the complete simulation.
     * @ingroup drawstuff
     * This function starts running the simulation, and only exits when the simulation is done.
     * @param argc argument count
     * @param argv argument values
     * @param window_width window width
     * @param window_height window height
     * @param fn pointer to dsFunctions struct
     */
    DS_API void dsSimulationLoop(const int argc, const char *const argv[],
                                 const int window_width, const int window_height,
                                 const struct dsFunctions *fn);

    /**
     * @brief exit with error message.
     * @ingroup drawstuff
     * This function displays an error message then exit.
     * @param msg format string, like printf, without the newline character.
     */
    DS_API void dsError(const char *msg, ...);

    /**
     * @brief exit with error message and core dump.
     * @ingroup drawstuff
     * This function displays an error message, then aborts and produces a core dump.
     * @param msg format string, like printf, without the newline character.
     */
    DS_API void dsDebug(const char *msg, ...);

    /**
     * @brief Sets the viewpoint
     * @ingroup drawstuff
     * @param xyz camera position.
     * @param hpr contains heading, pitch and roll numbers in degrees. heading=0
     * points along the x axis, pitch=0 is looking towards the horizon, and
     * roll 0 is "unrotated".
     */
    DS_API void dsSetViewpoint(const float xyz[3], const float hpr[3]);

    /**
     * @brief Gets the viewpoint
     * @ingroup drawstuff
     * @param xyz position
     * @param hpr heading,pitch,roll.
     */
    DS_API void dsGetViewpoint(float xyz[3], float hpr[3]);

    /**
     * @brief Stop the simulation loop.
     * @ingroup drawstuff
     * Calling this from within dsSimulationLoop()
     * will cause it to exit and return to the caller. it is the same as if the
     * user used the exit command. using this outside the loop will have no
     * effect.
     */
    DS_API void dsStop();

    /**
     * @brief Create a new texture by reading a PPM file.
     * @ingroup drawstuff
     * The texture number returned can be used as a parameter for dsSetTexture().
     * @param filename PPM filename
     * @return texture number
     */
    DS_API int dsLoadTexture(const char *filename);

    /**
     * @brief Get current shadow drawing state.
     * @ingroup drawstuff
     * @return 1 if drawing of shadows is enabled, 0 otherwise.
     */
    DS_API bool dsGetShadows(void);

    /**
     * @brief Toggle the rendering of shadows.
     * @ingroup drawstuff
     * @param a 1 to enable shadows, 0 to disable.
     */
    DS_API void dsSetShadows(const bool use_shadows_);

    /**
     * @brief Get current texture usage state.
     * @ingroup drawstuff
     * @return 1 if textures are enabled, 0 otherwise.
     */
    DS_API bool dsGetTextures(void);

    /**
     * @brief Toggle the rendering of textures.
     * @ingroup drawstuff
     * @param a 1 to enable textures, 0 to disable.
     */
    DS_API void dsSetTextures(const bool use_textures_);

    /**
     * @brief Set the texture for subsequent drawing.
     * @ingroup drawstuff
     * It changes the way objects are drawn: these changes will apply to all further
     * dsDrawXXX() functions.
     * @param texture_number must be a DS_xxx texture constant.
     * The current texture is colored according to the current color.
     * At the start of each frame, the texture is reset to none and the color is
     * reset to white.
     */
    DS_API void dsSetTexture(const int texture_number);

    /**
     * @brief Set the color with which geometry is drawn.
     * @ingroup drawstuff
     * @param red R
     * @param green G
     * @param blue B
     */
    DS_API void dsSetColor(const float red, const float green, const float blue);

    /**
     * @brief Set the color and alpha with which geometry is drawn.
     * @ingroup drawstuff
     * @param red R
     * @param green G
     * @param blue B
     * @param alpha transparency (0=transparent, 1=opaque)
     */
    DS_API void dsSetColorAlpha(const float red, const float green, const float blue, const float alpha);

    /**
     * @brief Draw a box.
     * @ingroup drawstuff
     * @param pos position of the center of the box
     * @param R 3x4 rotation matrix
     * @param sides side lengths (x,y,z)
     */
    DS_API void dsDrawBox(const float pos[3], const float R[12],
                          const float sides[3]);

    /**
     * @brief Draw a sphere.
     * @ingroup drawstuff
     * @param pos position of the center of the sphere
     * @param R 3x4 rotation matrix (ignored for spheres)
     * @param radius radius of the sphere
     */
    DS_API void dsDrawSphere(const float pos[3], const float R[12],
                             const float radius);

    /**
     * @brief Draw a capped cylinder (capsule).
     * @ingroup drawstuff
     * @param pos position of the capsule center
     * @param R 3x4 rotation matrix
     * @param length length of the cylinder part
     * @param radius radius of the spherical caps and cylinder
     */
    DS_API void dsDrawCapsule(const float pos[3], const float R[12],
                              const float length, const float radius);

    /**
     * @brief Draw a cylinder.
     * @ingroup drawstuff
     * @param pos position of the cylinder center
     * @param R 3x4 rotation matrix
     * @param length length
     * @param radius radius
     */
    DS_API void dsDrawCylinder(const float pos[3], const float R[12],
                               const float length, const float radius);

    /**
     * @brief Draw a triangle.
     * @ingroup drawstuff
     * @param pos position of the object frame
     * @param R 3x4 rotation matrix
     * @param v0 first vertex
     * @param v1 second vertex
     * @param v2 third vertex
     * @param solid 1 for solid, 0 for wireframe
     */
    DS_API void dsDrawTriangle(const float pos[3], const float R[12],
                               const float *v0, const float *v1,
                               const float *v2, const int solid);

    /**
     * @brief Draw many triangles.
     * @ingroup drawstuff
     * @param pos position of the object frame
     * @param R 3x4 rotation matrix
     * @param v pointer to array of 3*3*n floats (n triangles)
     * @param n number of triangles
     * @param solid 1 for solid, 0 for wireframe
     */
    DS_API void dsDrawTriangles(const float pos[3], const float R[12],
                                const float *v, const int n, const int solid);

    /**
     * @brief Draw a line segment.
     * @ingroup drawstuff
     * @param pos1 start point
     * @param pos2 end point
     */
    DS_API void dsDrawLine(const float pos1[3], const float pos2[3]);

    /**
     * @brief Draw a convex shape.
     * @ingroup drawstuff
     */
    DS_API void dsDrawConvex(const float pos[3], const float R[12],
                             const float *_planes,
                             const unsigned int _planecount,
                             const float *_points,
                             unsigned int _pointcount,
                             const unsigned int *_polygons);

    /* double-precision variants */

    DS_API void dsDrawBoxD(const double pos[3], const double R[12],
                           const double sides[3]);
    DS_API void dsDrawSphereD(const double pos[3], const double R[12],
                              const double radius);
    DS_API void dsDrawCapsuleD(const double pos[3], const double R[12],
                               const double length, const double radius);
    DS_API void dsDrawCylinderD(const double pos[3], const double R[12],
                                const double length, const double radius);
    DS_API void dsDrawTriangleD(const double pos[3], const double R[12],
                                const double *v0, const double *v1,
                                const double *v2, const int solid);
    DS_API void dsDrawTrianglesD(const double pos[3], const double R[12],
                                 const double *v, const int n, const int solid);
    DS_API void dsDrawLineD(const double pos1[3], const double pos2[3]);
    DS_API void dsDrawConvexD(const double pos[3], const double R[12],
                              const double *_planes,
                              const unsigned int _planecount,
                              const double *_points,
                              const unsigned int _pointcount,
                              const unsigned int *_polygons);

    /**
     * @brief Set sphere tesselation quality.
     * @ingroup drawstuff
     * @param n quality level
     */
    DS_API void dsSetSphereQuality(int n);

    /**
     * @brief Set capsule tesselation quality.
     * @ingroup drawstuff
     * @param n quality level
     */
    DS_API void dsSetCapsuleQuality(int n);

    /**
     * @brief Set drawing mode (filled or wireframe).
     * @ingroup drawstuff
     * @param mode DS_POLYFILL or DS_WIREFRAME
     */
    DS_API void dsSetDrawMode(int mode);

    /* Frame capture control (appended by KONNO Akihisa) */
    DS_API void dsStartCaptureFrames(void);
    DS_API void dsStopCaptureFrames(void);
    DS_API void dsFlipCaptureFrames(void);

    /* Pause the simulation (appended by KONNO Akihisa <konno@researchers.jp>) */
    DS_API void dsPause(void);

/* closing bracket for extern "C" */
#ifdef __cplusplus
}
#endif

#endif /* __DRAWSTUFF_H__ */
