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

struct RayHit
{
    float3 p;
    float3 normal;
    float  t = MaxFloat;
    bool hit = false;

    explicit operator bool() const { return hit; }
};

struct Sphere
{
    float3 center;
    float  radiusSqr = 0;

    Sphere() = default;
    Sphere(float3 center, float radius) : center(center), radiusSqr(radius * radius) {}

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
            h.p      = ray.eval(h.t);
            h.normal = normalize(h.p - center);
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
public:

    RayScene() = default;

    void add(Sphere sph)
    {
        objects.emplace_back(sph);
    }

    RayHit hit(Ray ray, float minT = 0, float maxT = MaxFloat) const
    {
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
        for (auto &o : objects)
        {
            if (o.hit(ray, minT, maxT))
                return true;
        }

        return false;
    }
};

class RayTracing : public Window
{
    XorLibrary xorLib;
    Device device;
    SwapChain swapChain;
    FPSCamera camera;
    RWImageData image;
    RayScene scene;

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

        image = RWImageData(size(), DXGI_FORMAT_R8G8B8A8_UNORM);

        camera.speed *= .1f;

        scene.add(Sphere(float3(-3, 0, -5), 1));
        scene.add(Sphere(float3( 3, 0, -5), 1));
        scene.add(Sphere(float3(0, -1001, 0), 1000));
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    ColorUnorm toneMap(float3 linearHDRColor) const
    {
        return ColorUnorm(linearHDRColor.s_xyz1.vec4());
    }

    void mainLoop(double deltaTime) override
    {
        camera.update(*this);

        Matrix viewProj =
            Matrix::projectionPerspective(size()) *
            camera.viewMatrix();

        auto cmd        = device.graphicsCommandList("Frame");

        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime, ProfilingDisplay::Disabled);

        uint2 sz = size();
        RayCamera rayCam(viewProj, camera.position, sz);
        std::mt19937 gen(3298471);
        std::uniform_real_distribution<float> jitterDist(-.5f, .5f);

        constexpr int Spp = 1;

        for (uint y = 0; y < sz.y; ++y)
        {
            for (uint x = 0; x < sz.x; ++x)
            {
                float3 color;

                for (int i = 0; i < Spp; ++i)
                {
                    float2 jitter = i ? jitterDist(gen) : 0;

                    Ray ray = rayCam.rayThroughPixel(float2(uint2(x, y))
                                                     + float2(0.5f)
                                                     + jitter);

                    if (auto h = scene.hit(ray))
                        color += h.normal;

                }

                image.pixel<ColorUnorm>(uint2(x, y)) = toneMap(color * (1.f / Spp));
            }
        }

        Texture backbufferTex = backbuffer.texture();
        cmd.updateTexture(backbufferTex, image);

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main()
{
    return RayTracing().run();
}
