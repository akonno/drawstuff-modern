// drawstuffCompat.hpp - drawstuff-compatible graphics functions
//
// This file is derived from the drawstuff public API definitions
// distributed with the Open Dynamics Engine (ODE).
//
// Original drawstuff API and interface design:
//   Copyright (c) 2001–2007 Russell L. Smith.
//
// Modifications and extensions for drawstuff-modern:
//   Copyright (c) 2025 Akihisa Konno.
//
// This file is released under the BSD 3-Clause License.
// See the LICENSE file for details.

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
    DS_API void dsSetSphereQuality(const int n);

    /**
     * @brief Set capsule tesselation quality.
     * @ingroup drawstuff
     * @param n quality level
     */
    DS_API void dsSetCapsuleQuality(const int n);

    /**
     * @brief Set drawing mode (filled or wireframe).
     * @ingroup drawstuff
     * @param mode DS_POLYFILL or DS_WIREFRAME
     */
    DS_API void dsSetDrawMode(const int mode);

    /* Frame capture control (appended by KONNO Akihisa) */
    DS_API void dsStartCaptureFrames(void);
    DS_API void dsStopCaptureFrames(void);
    DS_API void dsFlipCaptureFrames(void);

    /* Pause the simulation (appended by KONNO Akihisa <konno@researchers.jp>) */
    DS_API void dsPause(void);

    // ========== Additional functions for fast drawing of trimeshes ===========
    // These functions are not part of the original drawstuff API.
    // They are provided for efficiency when drawing large numbers of
    // triangles, e.g., from a trimesh collision object.
    // ========================================================================
    struct dsMeshHandle
    {
        GLuint id = 0; // 0 = invalid
        bool isValid() const { return id != 0; }
    };

    // Register a mesh given vertices and triangle indices.
    // The vertices array is a flat array of x,y,z coordinates.
    // The indices array contains triangle vertex indices (3 per triangle).
    // Returns a handle that can be used to draw the mesh later.
    dsMeshHandle dsRegisterIndexedMesh(
        const std::vector<float> &vertices, const std::vector<unsigned int> &indices);

    void dsDrawRegisteredMesh(
        dsMeshHandle handle,
        const float pos[3], const float R[12], const bool solid = true);
    
/* closing bracket for extern "C" */
#ifdef __cplusplus
}
#endif

#endif /* __DRAWSTUFF_H__ */
