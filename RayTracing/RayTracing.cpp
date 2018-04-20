#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

#include <ppl.h>

using namespace Xor;
using namespace Concurrency;

#define RAY_VERSION "Naive CosH RR Direct light"
// #define NAIVE_DIFFUSE

XOR_CONFIG_WINDOW(RTSettings, 5, 5)
{
    XOR_CONFIG_CHECKBOX(multithreaded, "Multithreaded", true);
    // XOR_CONFIG_SLIDER(float, exposure, "Exposure", 50.f, 1.f, 100.f);
} cfg_Settings;

int2 g_mouseCursor;

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

    float hit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        float3 CO = ray.origin - center;

        float a   = dot(ray.dir, ray.dir);
        float b   = 2 * dot(ray.dir, CO);
        float c   = dot(CO, CO) - radiusSqr;

        auto roots = Quadratic(a, b, c).solve();

        float closest = MaxFloat;

        for (int i = 0; i < roots.numRoots; ++i)
        {
            float t = roots.x[i];
            if (t >= minT && t <= maxT)
                closest = std::min(closest, t);
        }

        return closest;
    }
};

struct RayHit
{
    float3 p;
    float3 normal;
    float  t = MaxFloat;
    const Sphere   *object   = nullptr;

    const Material *material() const
    {
        return &object->material;
    }

    explicit operator bool() const { return !!object; }
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
    float3 throughput = 1.f;
    int2 pixel;

    Path() = default;
    Path(Ray ray, int depth = 0) : ray(ray), depth(depth) {}

    Path nextBounce(Ray r, float3 throughput) const
    {
        Path p(r, depth + 1);
        p.throughput = throughput;
        p.pixel = pixel;
        return p;
    }

    float largestThroughput() const { return largestElement(throughput); }

    bool isDebugPixel() const { return all(pixel == g_mouseCursor); }
};

float areaMeasureToSolidAngleMeasure(float r, float3 w, float3 n)
{
    // dw = dA * cos(theta) / r^2
    return dot(w, n) / (r * r);
}

struct UniformHemisphereDistribution
{
    float3 sample(float2 u) const
    {
        return uniformHemisphere(u);
    }

    float pdf(float3) const
    {
        constexpr float AreaOfHemisphere = 2 * Pi;
        constexpr float pdf = 1 / AreaOfHemisphere;
        return pdf;
    }
};

struct CosineWeightedHemisphereDistribution
{
    float3 sample(float2 u) const
    {
        return cosineWeightedHemisphere(u);
    }

    float pdf(float3 w) const
    {
        // p(theta) = cos(theta) / Pi
        // = dot(float3(0, 0, 1), w) / Pi
        // = w.z / Pi
        return w.z / Pi;
    }
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
            float t = o.hit(ray, minT, maxT);
            if (t != MaxFloat)
            {
                if (t < closest.t)
                {
                    closest.t      = t;
                    closest.object = &o;
                }
            }
        }

        if (closest)
        {
            closest.p      = ray.eval(closest.t);
            closest.normal = normalize(closest.p - closest.object->center);
        }

        return closest;
    }

    bool anyHit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        ++g_numRays;

        for (auto &o : objects)
        {
            if (o.hit(ray, minT, maxT) != MaxFloat)
                return true;
        }

        return false;
    }

    float3 trace(Path path,
                 float minT = 0,
                 float maxT = MaxFloat) const
    {
        constexpr int   MaxDepth           = 100;
        constexpr float BounceEpsilon      = .001f;
        constexpr bool DirectLightSampling = true;
        constexpr bool RussianRoulette     = true;

        if (path.depth > MaxDepth)
            return 0;

        auto &ray = path.ray;

        RayHit h = hit(ray, minT, maxT);

        // No hit, return background
        if (!h)
            return background;

        float3 N     = h.normal;

        // If we are using direct light sampling, consider
        // emissive of hit objects only on the first bounce.
        float3 color = 
            (!DirectLightSampling || path.depth == 0)
            ? h.material()->emissive
            : 0;
        float3 normalizedLambertian = h.material()->color * (1 / Pi);

        // Direct light sampling
        if (DirectLightSampling)
        {
            auto light           = randomLight();
            float3 pointInSphere = uniformSphereGen(g_gen);
            float3 pointInLight  = light->center + light->radius * pointInSphere;
            float3 wi            = pointInLight - h.p;
            float distance       = length(wi);
            float tFar           = 1 - (light->radius / distance);

            if (!anyHit(Ray(h.p, wi), BounceEpsilon, tFar - BounceEpsilon))
            {
                wi = normalize(wi);
                float cosTheta = dot(N, wi);
                float dAtodw = areaMeasureToSolidAngleMeasure(distance, -wi, pointInSphere);
                if (dAtodw > 0)
                {
                    float3 BRDF = normalizedLambertian * cosTheta;
                    color += BRDF * light->material.emissive * dAtodw * 4 * Pi * light->radiusSqr;
                }
            }
        }

        float continueP = RussianRoulette
            ? clamp(largestElement(path.throughput), 0.6f, .95f)
            : 1.f;

        // Uniform sampling of the outward facing hemisphere to keep things simple and correct.
        if (!RussianRoulette || rnd() < continueP)
        {
            CosineWeightedHemisphereDistribution dist;
            AxisAngleRotation outward = AxisAngleRotation::fromTo(float3(0, 0, 1), h.normal);
            float3 wi = dist.sample(rnd2());
            float pdf = dist.pdf(wi);
            wi = outward.rotate(wi);

            float cosTheta = dot(N, wi);
            float3 BRDF = normalizedLambertian * cosTheta;

            float3 L_i = trace(path.nextBounce(Ray(h.p, wi),
                                               path.throughput * BRDF),
                               BounceEpsilon);

            color += BRDF * L_i * (1 / pdf);
            color *= 1 / continueP;
        }

        return color;
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
        : Window { XOR_PROJECT_NAME " " RAY_VERSION, { 800, 450 } }
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
        scene.add(Sphere(float3( 3,     0,  -5), 1, Material(float3(.4f, .4f, 1))));
        scene.add(Sphere(float3( 0,     0,  -10), 2, Material(float3(0, 1, 0))));
        scene.add(Sphere(float3( 0, -1001,   0), 1000));

        scene.add(Sphere(float3( 5,     3,  -10), 1, Material(1, 20.f)));
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
        g_mouseCursor = device.debugMouseCursor();

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

                    Path p;
                    p.ray   = ray;
                    p.pixel = int2(coords);
                    hdrImage.pixel<float3>(coords) += scene.trace(p);
                    float3 color = hdrImage.pixel<float3>(coords);
                    // if (p.isDebugPixel()) print("(%f %f %f)\n", color.x, color.y, color.z);
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
