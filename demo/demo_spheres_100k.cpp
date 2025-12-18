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

// -----------------------------
// Simple deterministic RNG (xorshift32)
// -----------------------------
struct XorShift32
{
    uint32_t x;
    explicit XorShift32(uint32_t seed = 2463534242u) : x(seed ? seed : 2463534242u) {}
    uint32_t next_u32()
    {
        uint32_t y = x;
        y ^= y << 13;
        y ^= y >> 17;
        y ^= y << 5;
        x = y;
        return y;
    }
    // [0,1)
    float next_f01()
    {
        // 24-bit mantissa style
        return (next_u32() >> 8) * (1.0f / 16777216.0f);
    }
    // [a,b]
    float uniform(float a, float b)
    {
        return a + (b - a) * next_f01();
    }
};

struct Sphere
{
    float pos[3];
    float r;
    float color[3];
};

static std::vector<Sphere> g_spheres;

// Grid sizes
static constexpr int NX = 100;
static constexpr int NY = 100;
static constexpr int NZ = 10;

// Spacing between sphere centers (world units)
static constexpr float PITCH = 0.25f;

// Margin to guarantee non-overlap even with float error
static constexpr float MARGIN = 0.003f;

// Radius range (will be clamped by maxR)
static constexpr float R_MIN = 0.03f;

// Computed max radius that guarantees no overlap with immediate neighbors
static constexpr float maxRadiusSafe(float pitch, float margin)
{
    // If all spheres satisfy r <= pitch/2 - margin,
    // then for neighbor distance == pitch:
    // r_i + r_j <= pitch - 2*margin < pitch  => no overlap
    return (pitch * 0.5f) - margin;
}

// Build grid once
static void buildSpheres()
{
    const float maxR = maxRadiusSafe(PITCH, MARGIN);
    const float rMax = std::max(R_MIN, maxR);

    g_spheres.clear();
    g_spheres.reserve(static_cast<size_t>(NX) * NY * NZ);

    XorShift32 rng(123456789u);

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

                Sphere s{};
                s.pos[0] = x0 + i * PITCH;
                s.pos[1] = y0 + j * PITCH;
                s.pos[2] = z0 + k * PITCH;

                // Radius: random but safe
                s.r = rng.uniform(R_MIN, rMax);

                // Color: random but not too dark
                s.color[0] = rng.uniform(0.25f, 1.0f);
                s.color[1] = rng.uniform(0.25f, 1.0f);
                s.color[2] = rng.uniform(0.25f, 1.0f);

                g_spheres.push_back(s);
            }
        }
    }
}

static void start()
{
    // Camera: back and above
    float xyz[3] = {0.0f, -18.0f, 8.0f};
    float hpr[3] = {90.0f, -18.0f, 0.0f};
    dsSetViewpoint(xyz, hpr);

    dsSetTextures(true); // textures on for visual quality (set false if you want better performance)
    dsSetShadows(true);   // shadows on to stress shadow instancing path

    dsSetSphereQuality(1); // keep tessellation low-ish for speed (0/1/2... depends on your impl)
}

static void simLoop(int /*pause*/)
{
    // Identity rotation for spheres (ignored, but keep signature consistent)
    float R[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0};

    // Draw all spheres
    // (If your library batches/instances spheres, it should be fast.)
    for (const auto &s : g_spheres)
    {
        dsSetColor(s.color[0], s.color[1], s.color[2]);
        dsDrawSphere(s.pos, R, s.r);
    }
}

static void command(int cmd)
{
    // Optional quick controls
    if (cmd == DS_CMD_TOGGLE_PAUSE)
    {
        // no-op
    }
}

int main(int argc, char **argv)
{
    buildSpheres();

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
