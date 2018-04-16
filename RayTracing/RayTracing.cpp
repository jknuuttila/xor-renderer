#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

#include <ppl.h>

using namespace Xor;
using namespace Concurrency;

#define NAIVE_DIFFUSE

XOR_CONFIG_WINDOW(RTSettings, 5, 5)
{
    XOR_CONFIG_CHECKBOX(multithreaded, "Multithreaded", true);
    // XOR_CONFIG_SLIDER(float, exposure, "Exposure", 50.f, 1.f, 100.f);
} cfg_Settings;

struct Ray
{
    float3 origin;
    float3 dir;

    Ray() = default;
    Ray(float3 origin, float3 dir) : origin(origin), dir(dir) {}

    static Ray fromTo(float3 a, float3 b)
    {
        return Ray(a, b - a);
    }

    float3 eval(float t) const
    {
        return origin + dir * t;
    }
};

struct Material
{
    float3 color;
    float3 emissive;

    Material(float3 color = float3(.5f),
             float3 emissive = float3(0))
        : color(color)
        , emissive(emissive)
    {}
};

struct RayHit
{
    float3 p;
    float3 normal;
    float  t = MaxFloat;
    const Material *material = nullptr;
    bool hit = false;

    explicit operator bool() const { return hit; }
};

struct Sphere
{
    float3 center;
    Material material;
    float  radius    = 0;
    float  radiusSqr = 0;

    Sphere() = default;
    Sphere(float3 center,
           float radius,
           Material mat = Material())
        : center(center)
        , material(mat)
        , radius(radius)
        , radiusSqr(radius * radius) {}

    RayHit hit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        float3 CO = ray.origin - center;

        float a   = dot(ray.dir, ray.dir);
        float b   = 2 * dot(ray.dir, CO);
        float c   = dot(CO, CO) - radiusSqr;

        auto roots = Quadratic(a, b, c).solve();

        RayHit h;

        for (int i = 0; i < roots.numRoots; ++i)
        {
            float t = roots.x[i];
            if (t >= minT && t <= maxT)
            {
                h.t   = std::min(h.t, t);
                h.hit = true;
            }
        }

        if (h)
        {
            h.p        = ray.eval(h.t);
            h.normal   = normalize(h.p - center);
            h.material = &material;
        }

        return h;
    }
};

struct RayCamera
{
    float2 invResolution;
    float3 position;
    float3 o;
    float3 u;
    float3 v;

    RayCamera() = default;
    RayCamera(Matrix viewProj,
              float3 position, 
              uint2 size)
        : position(position)
    {
        invResolution = 1.f / float2(size);

        Matrix invViewProj = viewProj.inverse();

        float3 origin { -1,  1,  1 };
        float3 xEnd   {  1,  1,  1 };
        float3 yEnd   { -1, -1,  1 };

        origin = invViewProj.transformAndProject(origin);
        xEnd   = invViewProj.transformAndProject(xEnd);
        yEnd   = invViewProj.transformAndProject(yEnd);

        o = origin;
        u = xEnd - origin;
        v = yEnd - origin;
    }

    Ray rayThroughUV(float2 uv) const
    {
        return Ray::fromTo(position, o + uv.x * u + uv.y * v);
    }

    Ray rayThroughPixel(float2 pixelCoords) const
    {
        return rayThroughUV(pixelCoords * invResolution);
    }
};

thread_local Random g_gen     = Random::nonDeterministicSeed();
thread_local size_t g_numRays = 0;

struct Path
{
    Ray ray;
    int depth = 0;

    Path() = default;
    Path(Ray ray, int depth = 0) : ray(ray), depth(depth) {}
};

class RayScene
{
    std::vector<Sphere> objects;
    std::vector<Sphere> lights;

    std::uniform_real_distribution<float> fDist =
        std::uniform_real_distribution<float>(0, AlmostOne);
public:
    float3 background;

    RayScene() {}

    float   rnd() const { return fDist(g_gen); }
    float2 rnd2() const { return float2(fDist(g_gen), fDist(g_gen)); }
    float3 rnd3() const { return float3(fDist(g_gen), fDist(g_gen), fDist(g_gen)); }
    float4 rnd4() const { return float4(fDist(g_gen), fDist(g_gen), fDist(g_gen), fDist(g_gen)); }

    const Sphere *randomLight() const
    { 
        int i = std::uniform_int_distribution<int>(0, int(lights.size()) - 1)(g_gen);
        return &lights[i];
    }

    void add(Sphere sph)
    {
        objects.emplace_back(sph);

        if (any(sph.material.emissive > float3(0)))
            lights.emplace_back(sph);
    }

    RayHit hit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        ++g_numRays;

        RayHit closest;

        for (auto &o : objects)
        {
            if (RayHit h = o.hit(ray, minT, maxT))
            {
                if (h.t < closest.t)
                    closest = h;
            }
        }

        return closest;
    }

    bool anyHit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        ++g_numRays;

        for (auto &o : objects)
        {
            if (o.hit(ray, minT, maxT))
                return true;
        }

        return false;
    }

    float3 trace(Path path,
                 float minT = 0,
                 float maxT = MaxFloat) const
    {
        constexpr int MaxDepth = 5;
        constexpr float BounceEpsilon = .001f;

        if (path.depth > MaxDepth)
            return background;

        auto &ray = path.ray;

        RayHit h = hit(ray, minT, maxT);

        // No hit, return background
        if (!h)
            return background;

#if defined(NAIVE_DIFFUSE)
        float3 N     = h.normal;
        float3 color = h.material->emissive;

        // Rejection sample from the outward facing hemisphere to be paranoid about correctness
        float3 wi;
        std::uniform_real_distribution<float> unitBoxDist(-1, 1);
        for (;;)
        {
            float3 v(unitBoxDist(g_gen),
                     unitBoxDist(g_gen),
                     unitBoxDist(g_gen));

            // Points inside the impact?
            if (dot(wi, h.normal) < 0)
                continue;

            float len = length(v);

            // Outside the sphere?
            if (len > 1)
                continue;

            wi = v * (1/len);
            break;
        }

        constexpr float AreaOfHemisphere = 2*Pi;
        constexpr float p                = 1 / AreaOfHemisphere;

        float cosTheta = dot(N, wi);

        float3 normalizedLambertian = h.material->color * (1/Pi);

        float3 L_i = trace(Path(Ray(h.p, wi), path.depth + 1),
                           BounceEpsilon);

        color += normalizedLambertian * L_i * cosTheta * (1/p);

        return color;
#else
        float3 color  = 0;
        throughput   *= h.material->color;

        // On the first bounce, get emissive from anything we hit
        bool firstBounce = all(throughput == 1.f);
        if (firstBounce)
            color += h.material->emissive;

        // Direct light sampling
        {
            auto light           = randomLight();
            float3 pointInSphere = uniformSphereGen(g_gen);
            float3 pointInLight  = light->center + light->radius * pointInSphere;
            float3 wo            = pointInLight - h.p;

            if (!anyHit(Ray(h.p, wo), BounceEpsilon, 1 - BounceEpsilon))
            {
                wo = normalize(wo);
                color += light->material.emissive * dot(wo, h.normal);
            }
        }

        float largestThroughput  = largestElement(throughput);

        if (rnd() > largestThroughput)
        {
            float3 newDirTangentSpace      = cosineWeightedHemisphereGen(g_gen);
            AxisAngleRotation tangentSpace = AxisAngleRotation::fromTo(float3(0, 0, 1), h.normal);
            float3 newDir                  = tangentSpace.rotate(newDirTangentSpace);
            float3 bounce                  = trace(Ray(h.p, newDir),
                                                   throughput / largestThroughput,
                                                   BounceEpsilon);

            color += bounce;
        }

        return throughput * color;
#endif
    }
};

class RayTracing : public Window
{
    XorLibrary xorLib;
    Device device;
    SwapChain swapChain;
    FPSCamera camera;
    RWImageData hdrImage;
    RWImageData ldrImage;
    RayScene scene;
    size_t numSamples = 0;
    std::atomic<size_t> numRays;

public:
    RayTracing()
        : Window { XOR_PROJECT_NAME, { 800, 450 } }
    {
        xorLib.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);
#if 1
        device      = xorLib.defaultDevice();
#else
        device      = xorLib.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);

        hdrImage = RWImageData(size(), DXGI_FORMAT_R32G32B32_FLOAT);
        ldrImage = RWImageData(size(), DXGI_FORMAT_R8G8B8A8_UNORM);

        camera.speed *= .1f;

        scene.background = float3(.6f);

        scene.add(Sphere(float3(-3,     0,  -5), 1, Material(float3(1, 0, 0))));
        scene.add(Sphere(float3(-2,     0,  -7), .7f, Material(float3(.9f))));
        scene.add(Sphere(float3( 0,     0,  -10), 2, Material(float3(0, 1, 0))));
        scene.add(Sphere(float3( 3,     0,  -5), 1, Material(float3(.4f, .4f, 1))));
        scene.add(Sphere(float3( 0, -1001,   0), 1000));

        scene.add(Sphere(float3( 5,     3,  -10), 1, Material(1, 10.f)));
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }

    ColorUnorm toneMap(float3 linearHDRColor) const
    {
        float largest  = largestElement(linearHDRColor);
        float reinhard = largest / (1 + largest);
        return ColorUnorm(sqrtVec(reinhard * linearHDRColor).s_xyz1.vec4());
    }

    void reset()
    {
        Timer t;

        uint2 sz = size();
        for (uint y = 0; y < sz.y; ++y)
        {
            auto row = hdrImage.scanline<float>(y);
            memset(row.data(), 0, row.sizeBytes());
        }

        numSamples = 0;

        log("RayTracing", "Tracing reset in %.2f ms\n", t.milliseconds());
    }

    template <typename F>
    void forLoop(uint begin, uint end, F &&f)
    {
        if (cfg_Settings.multithreaded)
        {
            parallel_for(begin, end, 1u, std::forward<F>(f));
        }
        else
        {
            for (uint i = begin; i < end; ++i)
                f(i);
        }
    }

    void mainLoop(double deltaTime) override
    {
        if (camera.update(*this))
            numSamples = 0;

        if (isKeyHeld(VK_SPACE))
            numSamples =  0;

        if (!numSamples)
            reset();

        Matrix viewProj =
            Matrix::projectionPerspective(size()) *
            camera.viewMatrix();

        auto cmd        = device.graphicsCommandList("Frame");

        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime, ProfilingDisplay::Disabled);

        uint2 sz = size();
        RayCamera rayCam(viewProj, camera.position, sz);

        int Spp = numSamples == 0 ? 1 : 4;

        Timer t;
        numRays = 0;

        forLoop(0, sz.y, [&](uint y)
        {
            size_t raysBefore = g_numRays;

            for (uint x = 0; x < sz.x; ++x)
            {
                uint2 coords(x, y);

                for (int i = 0; i < Spp; ++i)
                {
                    float2 jitter = scene.rnd2();
                    Ray ray = rayCam.rayThroughPixel(float2(coords) + jitter);
                    hdrImage.pixel<float3>(coords) += scene.trace(ray);
                }
            }

            size_t raysAfter = g_numRays;

            numRays.fetch_add(raysAfter - raysBefore);
        });

        double tracingSec = t.seconds();
        numSamples += Spp;

        t.reset();
        {
            forLoop(0, sz.y, [&](uint y)
            {
                for (uint x = 0; x < sz.x; ++x)
                {
                    uint2 coords(x, y);
                    ldrImage.pixel<ColorUnorm>(coords) = toneMap(hdrImage.pixel<float3>(coords) / float(numSamples));
                }
            });

            Texture backbufferTex = backbuffer.texture();
            cmd.updateTexture(backbufferTex, ldrImage);
        }

        double toneMapAndCopyMs = t.milliseconds();

        size_t rays        = numRays.load();
        double mraysPerSec = double(rays) / 1e6 / tracingSec;
        double tracingMs   = tracingSec * 1000.0;

        log("RayTracing", "Frame #%zu, traced %zu rays in %.2f ms (%.3f Mrays / sec), accumulated %zu samples per pixel, tonemapped in %.2f ms\n",
            size_t(device.frameNumber()),
            rays,
            tracingMs,
            mraysPerSec,
            numSamples,
            toneMapAndCopyMs);

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main()
{
    return RayTracing().run();
}
