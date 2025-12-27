// demo_spheres_100k.cpp
// 100k spheres grid demo for drawstuff-modern.
//
// Layout: 100 (X) x 100 (Y) x 10 (Z) = 100,000 spheres
// Radii and colors are random, but radii are clamped so that neighboring spheres never overlap.

#include <drawstuff/drawstuff.h>

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>

struct Object
{
    float pos[3];
    float r;
    float l;
    float color[3];
};

enum ObjectType
{
    SPHERE,
    CYLINDER,
    CAPSULE,
    BOX
};

static std::vector<Object> g_objects;

// Grid sizes
static constexpr int NX = 100;
static constexpr int NY = 100;
static constexpr int NZ = 10;

// Spacing between object centers (world units)
static constexpr float PITCH = 0.25f;

// Margin to guarantee non-overlap even with float error
static constexpr float MARGIN = 0.003f;

// Radius range (will be clamped by maxR)
static constexpr float R_MIN = 0.03f;
static constexpr float L_MIN = 0.03f;

// Computed max radius that guarantees no overlap with immediate neighbors
static constexpr float maxRadiusSafe(const float pitch, const float margin)
{
    // If all objects satisfy r <= pitch/2 - margin,
    // then for neighbor distance == pitch:
    // r_i + r_j <= pitch - 2*margin < pitch  => no overlap
    return (pitch * 0.5f) - margin;
}

// Computed max length (thickness) for cylinder that guarantees no overlap with immediate neighbors
static constexpr float maxCylinderLengthSafe(const float pitch, const float margin)
{
    // If all objects satisfy l <= pitch - 2*margin,
    // then for neighbor distance == pitch:
    // l_i + l_j <= 2*l <= 2*(pitch - 2*margin) < pitch  => no overlap
    return pitch - 2.0f * margin;
}

// Build grid once
static void buildObjects()
{
    const float maxR = maxRadiusSafe(PITCH, MARGIN);
    const float rMax = std::max(R_MIN, maxR);
    const float maxL = maxCylinderLengthSafe(PITCH, MARGIN);
    const float lMax = std::max(L_MIN, maxL);

    g_objects.clear();
    g_objects.reserve(static_cast<size_t>(NX) * NY * NZ);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_real_distribution<float> radius_dist(R_MIN, rMax);
    std::uniform_real_distribution<float> length_dist(L_MIN, lMax);
    std::uniform_real_distribution<float> color_dist(0.25f, 1.0f);

    // Center grid around origin-ish
    const float x0 = -(NX - 1) * 0.5f * PITCH;
    const float y0 = -(NY - 1) * 0.5f * PITCH;
    const float z0 = 0.10f; // lift from ground a bit

    for (int k = 0; k < NZ; ++k)
    {
        for (int j = 0; j < NY; ++j)
        {
            for (int i = 0; i < NX; ++i)
            {

                Object s{};
                s.pos[0] = x0 + i * PITCH;
                s.pos[1] = y0 + j * PITCH;
                s.pos[2] = z0 + k * PITCH;

                // Radius: random but safe
                s.r = radius_dist(rng);
                // Length (if this object is a cylinder; ignored for spheres)
                s.l = length_dist(rng);

                // Color: random but not too dark
                s.color[0] = color_dist(rng);
                s.color[1] = color_dist(rng);
                s.color[2] = color_dist(rng);

                g_objects.push_back(s);
            }
        }
    }
}

ObjectType object_type = SPHERE;
int object_quality = 3; // default quality for spheres and cylinders

static void start()
{
    // Camera: back and above
    float xyz[3] = {0.0f, -18.0f, 8.0f};
    float hpr[3] = {90.0f, -18.0f, 0.0f};
    dsSetViewpoint(xyz, hpr);

    dsSetTextures(true); // textures on for visual quality (set false if you want better performance)
    dsSetShadows(true);   // shadows on to stress shadow instancing path

    dsSetSphereQuality(object_quality); // keep tessellation low-ish for speed (0/1/2... depends on your impl)
    dsSetCapsuleQuality(object_quality);
}

static void simLoop(int /*pause*/)
{
    // Identity rotation for objects
    float R[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0};

    // Draw all spheres
    // (If your library batches/instances spheres, it should be fast.)
    for (const auto &s : g_objects)
    {
        dsSetColor(s.color[0], s.color[1], s.color[2]);
        if (object_type == CYLINDER)
        {
            dsDrawCylinder(s.pos, R, s.l, s.r);
        }
        else if (object_type == SPHERE)
        {
            dsDrawSphere(s.pos, R, s.r);
        }
        else if (object_type == CAPSULE)
        {
            // Capsule must fit in pitch; length is calculated from pitch and radius.
            const float L_capsule = PITCH - 2.0f * s.r - 0.5f * MARGIN;
            if (L_capsule < 0.0f)
            {
                std::cerr << "Warning: Capsule length negative; clamping to zero." << std::endl;
                // Degenerate to sphere
                // (This should not happen with the current random generation parameters.)
                dsDrawSphere(s.pos, R, s.r);
                continue;
            }
            dsDrawCapsule(s.pos, R, L_capsule, s.r);
        }
        else // BOX
        {
            float sides[3] = {s.r * 2.0f, s.r * 2.0f, s.l};
            dsDrawBox(s.pos, R, sides);
        }
    }
}

static void command(int cmd)
{
    // Optional quick controls
    if (cmd == ' ')
    {
        std::string type_name;
        if (object_type == SPHERE)
        {
            object_type = CYLINDER;
            type_name = "CYLINDER";
        }
        else if (object_type == CYLINDER)
        {
            object_type = CAPSULE;
            type_name = "CAPSULE";
        }
        else if (object_type == CAPSULE)
        {
            object_type = BOX;
            type_name = "BOX";
        }
        else
        {
            object_type = SPHERE;
            type_name = "SPHERE";
        }
        
        std::cerr << "Switched to " << type_name << " mode." << std::endl;
    }
    else if (cmd == 's' || cmd == 'S')
    {
        object_type = SPHERE;
        std::cerr << "Switched to SPHERE mode." << std::endl;
    }
    else if (cmd == 'y' || cmd == 'Y')
    {
        object_type = CYLINDER;
        std::cerr << "Switched to CYLINDER mode." << std::endl;
    }
    else if (cmd == 'c' || cmd == 'C')
    {
        object_type = CAPSULE;
        std::cerr << "Switched to CAPSULE mode." << std::endl;
    }
    else if (cmd == 'b' || cmd == 'B')
    {
        object_type = BOX;
        std::cerr << "Switched to BOX mode." << std::endl;
    }
    else if (cmd == '+')
    {
        if (object_quality < 3)
            object_quality++;
        std::cerr << "Increasing quality to " << object_quality << std::endl;
        dsSetSphereQuality(object_quality);
        dsSetCapsuleQuality(object_quality);
    }
    else if (cmd == '-')
    {
        if (object_quality > 0)
            object_quality--;
        std::cerr << "Decreasing quality to " << object_quality << std::endl;
        dsSetSphereQuality(object_quality);
        dsSetCapsuleQuality(object_quality);
    }
    // Direct quality selection via number keys
    else if (cmd == '1')
    {
        object_quality = 1;
        std::cerr << "Setting sphere quality to " << object_quality << std::endl;
        dsSetSphereQuality(object_quality);
        dsSetCapsuleQuality(object_quality);
    }
    else if (cmd == '2')
    {
        object_quality = 2;
        std::cerr << "Setting sphere quality to " << object_quality << std::endl;
        dsSetSphereQuality(object_quality);
        dsSetCapsuleQuality(object_quality);
    }
    else if (cmd == '3')
    {
        object_quality = 3;
        std::cerr << "Setting sphere quality to " << object_quality << std::endl;
        dsSetSphereQuality(object_quality);
        dsSetCapsuleQuality(object_quality);
    }
    else if (cmd == 'r' || cmd == 'R')
    {
        buildObjects();
        std::cerr << "Rebuilt objects." << std::endl;
    }
}

int main(int argc, char **argv)
{
    buildObjects();

    dsFunctions fn;
    fn.version = DS_VERSION;
    fn.start = &start;
    fn.step = &simLoop;
    fn.command = &command;
    fn.stop = nullptr;
    fn.path_to_textures = "../textures";

    dsSimulationLoop(argc, argv, 1280, 720, &fn);
    return 0;
}
