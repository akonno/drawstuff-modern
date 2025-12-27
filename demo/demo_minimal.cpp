// demo_minimal.cpp
// Minimal drawstuff-style demo for drawstuff-modern.
//
// Draws one box, one sphere, one cylinder (disk-like), and one capsule.

#include <cmath>
#include <cstdio>

#include <drawstuff/drawstuff.h>

int object_quality = 3; // default quality

static void start()
{
    // Optional: camera viewpoint
    float xyz[3] = {2.0f, -3.0f, 1.5f};
    float hpr[3] = {90.0f, -10.0f, 0.0f}; // heading, pitch, roll (deg)
    dsSetViewpoint(xyz, hpr);

    dsSetTextures(true);
    dsSetShadows(true);
}

static void simLoop(int /*pause*/)
{
    // World frame transforms
    float R[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0};

    const float height = 1.0f; // lift objects a bit above ground
    // --- Box ---
    {
        float pos[3] = {-0.8f, 0.0f, 0.3f + height};
        float sides[3] = {0.5f, 0.4f, 0.6f};
        dsSetColor(0.8f, 0.2f, 0.2f);
        dsDrawBox(pos, R, sides);
    }

    // --- Sphere ---
    {
        float pos[3] = {0.0f, 0.0f, 0.35f + height};
        float radius = 0.35f;
        dsSetColor(0.2f, 0.6f, 0.9f);
        dsDrawSphere(pos, R, radius);
    }

    // --- Cylinder (disk-like if length is small) ---
    {
        float pos[3] = {0.8f, 0.0f, 0.15f + height};
        float length = 0.08f; // thin -> disk-like
        float radius = 0.35f;
        dsSetColor(0.2f, 0.9f, 0.4f);
        dsDrawCylinder(pos, R, length, radius);
    }

    // --- Capsule ---
    {
        float pos[3] = {0.0f, 0.9f, 0.35f + height};
        float length = 0.6f;
        float radius = 0.18f;
        dsSetColor(0.9f, 0.8f, 0.2f);
        dsDrawCapsule(pos, R, length, radius);
    }

    // Optional: show a ground texture
    // dsSetTexture(DS_WOOD);
}

static void command(int cmd)
{
    // Minimal keyboard handling (optional)
    if (cmd == DS_CMD_TOGGLE_PAUSE)
    {
        std::printf("Toggle pause\n");
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
}

int main(int argc, char **argv)
{
    dsFunctions fn;
    fn.version = DS_VERSION;
    fn.start = &start;
    fn.step = &simLoop;
    fn.command = &command;
    fn.stop = nullptr;
    fn.path_to_textures = "../textures"; // adjust if needed

    // Window size: tweak as you like
    dsSetSphereQuality(object_quality);
    dsSetCapsuleQuality(object_quality);
    dsSimulationLoop(argc, argv, 1024, 768, &fn);
    return 0;
}
