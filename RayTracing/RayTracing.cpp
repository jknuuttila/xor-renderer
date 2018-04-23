#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

#include <ppl.h>

using namespace Xor;
using namespace Concurrency;

#define RAY_VERSION "Loop CosH RR Direct light"
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

float  rnd()  { return fastUniformFloat(g_gen); }
float2 rnd2() { return float2(rnd(), rnd()); }
float3 rnd3() { return float3(rnd(), rnd(), rnd()); }
float4 rnd4() { return float4(rnd(), rnd(), rnd(), rnd()); }

struct BSDFSample
{
    float3 wi;
    float pdf = 0;

    BSDFSample() = default;
    BSDFSample(float3 wi, float pdf) : wi(wi), pdf(pdf) {}
};

struct BSDFLambertian
{
    float3 color = 1.f;

    BSDFLambertian() = default;
    BSDFLambertian(float3 color) : color(color) {}

    BSDFSample sample(float2 u) const
    {
        float3 w = cosineWeightedHemisphere(u);

        BSDFSample s;
        s.wi  = w;
        // p(theta) = cos(theta) / Pi
        // = dot(float3(0, 0, 1), w) / Pi
        // = w.z / Pi
        s.pdf = w.z / Pi;
        return s;
    }

    BSDFSample sample(float2 u, float3 wo, float3 n) const
    {
        BSDFSample s = sample(u);
        s.wi = AxisAngleRotation::fromTo(float3(0, 0, 1), n).rotate(s.wi);
        return s;
    }

    float3 eval(float3 wo, float3 wi, float3 n) const
    {
        // Normalized to conserve energy
        return color * (1/Pi);
    }
};

struct BSDFGlossyGGX
{
    static constexpr float DielectricF0 = 0.04f;

    float3 F0       = DielectricF0;
    float roughness = .1f;

    BSDFGlossyGGX() = default;
    BSDFGlossyGGX(float roughness) : roughness(roughness) {}
    BSDFGlossyGGX(float3 F0, float roughness)
        : F0(F0)
        , roughness(roughness)
    {}

    // GGX formulas from http://www.codinglabs.net/article_physically_based_rendering_cook_torrance.aspx
    float3 Fresnel_Schlick(float cosT) const
    {
        return F0 + (1 - F0) * pow(1 - cosT, 5);
    }

    static float chiGGX(float v)
    {
        return v > 0 ? 1.f : 0.f;
    }

    static float GGX_Distribution(float3 n, float3 h, float alpha)
    {
        float NoH    = dot(n,h);
        float alpha2 = alpha * alpha;
        float NoH2   = NoH * NoH;
        float den    = NoH2 * alpha2 + (1 - NoH2);
        return (chiGGX(NoH) * alpha2) / ( Pi * den * den );
    }

    static float GGX_PartialGeometryTerm(float3 v, float3 n, float3 h, float alpha)
    {
        float VoH2 = saturate(dot(v,h));
        float chi  = chiGGX( VoH2 / saturate(dot(v,n)) );
        VoH2       = VoH2 * VoH2;
        float tan2 = ( 1 - VoH2 ) / VoH2;
        return (chi * 2) / ( 1 + sqrt( 1 + alpha * alpha * tan2 ) );
    }

    // From "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals",
    // Eric Heitz
    // https://hal.archives-ouvertes.fr/hal-01509746/document
    float3 sampleGGXVNDF(float3 V_, float2 u) const
    {
        float U1      = u.x;
        float U2      = u.y;
        float alpha_x = roughness;
        float alpha_y = roughness;

        // stretch view
        float3 V = normalize(float3(alpha_x * V_.x, alpha_y * V_.y, V_.z));
        // orthonormal basis
        float3 T1 = (V.z < 0.9999) ? normalize(cross(V, float3(0, 0, 1))) : float3(1, 0, 0);
        float3 T2 = cross(T1, V);
        // sample point with polar coordinates (r, phi)
        float a = 1.0f / (1.0f + V.z);
        float r = sqrt(U1);
        float phi = (U2 < a) ? U2 / a * Pi : Pi + (U2 - a) / (1.0f - a) * Pi;
        float P1 = r * cos(phi);
        float P2 = r * sin(phi)*((U2 < a) ? 1.0f : V.z);
        // compute normal
        float3 N = P1 * T1 + P2 * T2 + sqrt(std::max(0.0f, 1.0f - P1 * P1 - P2 * P2))*V;
        // unstretch
        N = normalize(float3(alpha_x*N.x, alpha_y*N.y, std::max(0.0f, N.z)));
        return N;
    }

    BSDFSample sample(float2 u) const
    {
        BSDFSample s;
        s.wi  = sampleGGXVNDF(float3(0, 0, 1), u);
        s.pdf = 0;
        return s;
    }

    BSDFSample sample(float2 u, float3 wo, float3 n) const
    {
        float3 wi = reflect(wo, n);
        float3 h = (wo + wi) * 0.5f;
        float  D = GGX_Distribution(n, h, roughness);

        BSDFSample s;
        s.wi  = sampleGGXVNDF(wi, u);
        s.pdf = D;
        return s;
    }

    float3 eval(float3 wo, float3 wi, float3 n) const
    {
        float3 h = (wo + wi) * 0.5f;

        float dotWoN = dot(wo, n);
        float dotHN  = dot(h, n);

        if (dotWoN <= 0.f) return 0;
        if (dotHN <= 0.f)  return 0;
        if (all(h == 0.f)) return 0;

        float  D = GGX_Distribution(n, h, roughness);
        float  G = GGX_PartialGeometryTerm(wo, n, h, roughness);
        float3 F = Fresnel_Schlick(clampedDot(wo, h));

        float3 f = F * (D * G / (4 * dot(wo, n) * dot(h, n)));
        return f;
    }
};

struct BSDFDiffuseWithGGX
{
    BSDFLambertian diffuse;
    BSDFGlossyGGX specular;

    BSDFDiffuseWithGGX() = default;
    BSDFDiffuseWithGGX(float3 color, float roughness)
        : diffuse(color)
        , specular(roughness)
    {}
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

    auto glossy() const
    {
        return BSDFGlossyGGX();
    }

    auto diffuse() const
    {
        return BSDFLambertian(color);
    }

    auto bsdf() const
    {
        return diffuse();
    }
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

    float hit(Ray ray, float tNear = 0, float tFar = MaxFloat) const
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
            if (t >= tNear && t <= tFar)
                closest = std::min(closest, t);
        }

        return closest;
    }

    float area() const { return 4 * Pi * radiusSqr; }

    float3 sample(float2 u) const
    {
        return uniformSphere(u) * radius + center;
    }

    float pdfArea(float3) const
    {
        return 1 / area();
    }

    float pdfSolidAngle(float3 from, float3 wi) const
    {
        float t = hit(Ray(from, wi));
        if (t == MaxFloat)
            return 0;

        float3 p = from + t * wi;
        float3 n = normalize(p - center);
        float r2 = wi.lengthSqr();
        float r  = sqrt(r2);

        float cosTheta = dot(-wi, n) * (1/r);

        // p(dw) = p(A) * r2 / cos(theta)
        float pdfSA = pdfArea(p) * r2 / std::abs(cosTheta);

        return pdfSA;
    }
};

struct RayHit
{
    float3 p;
    float3 normal;
    float  t = MaxFloat;
    const Sphere *object = nullptr;

    const Material *material() const
    {
        return &object->material;
    }

    explicit operator bool() const { return !!object; }
};

class RayScene
{
    std::vector<Sphere> objects;
    std::vector<Sphere> lights;
public:
    float3 background;

    RayScene() {}

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

    RayHit hit(Ray ray, float tNear = 0, float tFar = MaxFloat) const
    {
        ++g_numRays;

        RayHit closest;

        for (auto &o : objects)
        {
            float t = o.hit(ray, tNear, tFar);
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

    bool anyHit(Ray ray, float tNear = 0, float tFar = MaxFloat) const
    {
        ++g_numRays;

        for (auto &o : objects)
        {
            if (o.hit(ray, tNear, tFar) != MaxFloat)
                return true;
        }

        return false;
    }
};

struct Path
{
    Ray ray;
    int2 pixel;

    Path() = default;
    Path(Ray ray, int2 pixel = 0) : ray(ray), pixel(pixel) {}

    bool isDebugPixel() const { return all(pixel == g_mouseCursor); }

    float3 miss(const RayScene &scene, float3 wi) const
    {
#if 1
        return 0;
#else
        return scene.background;
#endif
    }

    float3 trace(const RayScene &scene, float tNear = 0, float tFar = MaxFloat) const
    {
        int depth = 0;
        float3 throughput = 1.f;

        constexpr int   MaxDepth           = 1000;
        constexpr float BounceEpsilon      = .001f;
        constexpr bool DirectLightSampling = true;
        constexpr bool RussianRoulette     = true;

        RayHit h = scene.hit(ray, tNear, tFar);

        // No hit, return background
        if (!h)
            return miss(scene, ray.dir);

        float3 color = h.material()->emissive;

        for (int depth = 0; depth < MaxDepth; ++depth)
        {
            auto bsdf = h.material()->bsdf();
            float3 wo = normalize(-ray.dir);
            float3 n = h.normal;

            // Direct light sampling
            // NOTE: The direct light integral is evaluated with the area
            // measure, unlike the indirect light integral which is with
            // the solid angle measure.
            if (DirectLightSampling)
            {
                auto   light        = scene.randomLight();
                float3 pointOnLight = light->sample(rnd2());
                float3 wi           = pointOnLight - h.p;

                if (!scene.anyHit(Ray(h.p, wi), BounceEpsilon, 1 - BounceEpsilon))
                {
                    float r2         = wi.lengthSqr();
                    float pdf        = light->pdfArea(pointOnLight);
                    wi               = normalize(wi);
                    float3 wo        = normalize(-ray.dir);
                    float cosTheta_i = clampedDot(n, wi);

                    float3 BRDF      =
                        (h.material()->diffuse().eval(wo, wi, n) +
                         h.material()->glossy().eval(wo, wi, n))
                        * cosTheta_i;

                    float3 N_L       = normalize(pointOnLight - light->center);

                    float cosTheta_L       = clampedDot(-wi, N_L);
                    float areaToSolidAngle = cosTheta_L / r2;

                    float3 L_i = light->material.emissive * areaToSolidAngle / pdf;

                    color += throughput * BRDF * L_i;
                }
            }
            else
            {
                color += throughput * h.material()->emissive;
            }

            float continueP = RussianRoulette
                ? clamp(largestElement(throughput), 0.6f, .95f)
                : 1.f;

            // Uniform sampling of the outward facing hemisphere to keep things simple and correct.
            if (RussianRoulette && rnd() > continueP)
                break;

            BSDFSample s = bsdf.sample(rnd2(), wo, n);

            float cosTheta = clampedDot(s.wi, n);

            throughput *= bsdf.eval(wo, s.wi, n)
                * (cosTheta * (1 / s.pdf) * (1 / continueP));

            // Trace the next ray in the path
            h = scene.hit(Ray(h.p, s.wi), BounceEpsilon);

            // If it missed, add the background and bail out
            if (!h)
            {
                color += throughput * miss(scene, ray.dir);
                break;
            }
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
                    float2 jitter = rnd2();
                    Ray ray = rayCam.rayThroughPixel(float2(coords) + jitter);

                    Path path(ray, int2(coords));
                    hdrImage.pixel<float3>(coords) += path.trace(scene);
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
