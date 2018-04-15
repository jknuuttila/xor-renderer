#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

using namespace Xor;

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

class RayScene
{
    std::vector<Sphere> objects;
    std::vector<Sphere> lights;

    std::uniform_real_distribution<float> fDist =
        std::uniform_real_distribution<float>(0, AlmostOne);

    mutable size_t numRays = 0;

public:
    mutable std::mt19937 gen;

    RayScene(uint32_t seed = 32598257)
        : gen(seed)
    {}

    float   rnd() const { return fDist(gen); }
    float2 rnd2() const { return float2(fDist(gen), fDist(gen)); }
    float3 rnd3() const { return float3(fDist(gen), fDist(gen), fDist(gen)); }
    float4 rnd4() const { return float4(fDist(gen), fDist(gen), fDist(gen), fDist(gen)); }

    const Sphere *randomLight() const
    { 
        int i = std::uniform_int_distribution<int>(0, int(lights.size()) - 1)(gen);
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
        ++numRays;
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
        ++numRays;

        for (auto &o : objects)
        {
            if (o.hit(ray, minT, maxT))
                return true;
        }

        return false;
    }

    float3 trace(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
        constexpr float BounceEpsilon = .001f;
        constexpr float TerminateProb = .2f;

        RayHit h = hit(ray, minT, maxT);

        // No hit, return background
        if (!h)
            return float3(0);

        float3 color  = 0;
        float3 albedo = h.material->color;

        // Direct light sampling
        {
            auto light           = randomLight();
            float3 pointInSphere = uniformSphereGen(gen);
            float3 pointInLight  = light->center + light->radius * pointInSphere;
            float3 L             = pointInLight - h.p;

            if (!anyHit(Ray(h.p, L), BounceEpsilon, 1 - BounceEpsilon))
                color += albedo * light->material.emissive;
        }

        if (rnd() > TerminateProb)
        {
            float3 newDirTangentSpace      = cosineWeightedHemisphereGen(gen);
            AxisAngleRotation tangentSpace = AxisAngleRotation::fromTo(float3(0, 0, 1),
                                                                       h.normal);
            float3 newDir = tangentSpace.rotate(newDirTangentSpace);
            float3 bounce = trace(Ray(h.p, newDir), BounceEpsilon);
            color += albedo * bounce;
        }

        return color;
    }

    void resetNumRays() { numRays = 0; }
    size_t numRaysTraced() const { return numRays; }
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

        scene.add(Sphere(float3(-3,     0,  -5), 1, Material(float3(1, 0, 0))));
        scene.add(Sphere(float3( 0,     0,  -10), 1, Material(float3(0, 1, 0))));
        scene.add(Sphere(float3( 3,     0,  -5), 1, Material(float3(.4f, .4f, 1))));
        scene.add(Sphere(float3( 0, -1001,   0), 1000));
        scene.add(Sphere(float3( 5,     3, -10), 1, Material(.5f, 2.f)));
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
        return ColorUnorm(linearHDRColor.s_xyz1.vec4());
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
        size_t raysBefore = scene.numRaysTraced();

        for (uint y = 0; y < sz.y; ++y)
        {
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
        }

        double tracingMs = t.milliseconds();
        size_t raysAfter = scene.numRaysTraced();
        numSamples += Spp;

        t.reset();
        {
            for (uint y = 0; y < sz.y; ++y)
            {
                for (uint x = 0; x < sz.x; ++x)
                {
                    uint2 coords(x, y);
                    ldrImage.pixel<ColorUnorm>(coords) = toneMap(hdrImage.pixel<float3>(coords) / float(numSamples));
                }
            }

            Texture backbufferTex = backbuffer.texture();
            cmd.updateTexture(backbufferTex, ldrImage);
        }

        double toneMapAndCopyMs = t.milliseconds();

        log("RayTracing", "Frame #%zu, traced %zu rays in %.2f ms, accumulated %zu samples per pixel, tonemapped in %.2f ms\n",
            size_t(device.frameNumber()),
            raysAfter - raysBefore,
            tracingMs,
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
