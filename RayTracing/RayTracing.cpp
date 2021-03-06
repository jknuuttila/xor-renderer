#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

#include <ppl.h>

using namespace Xor;
using namespace Concurrency;

#define RUSSIAN_ROULETTE
#define DIRECT_LIGHT_SAMPLING
// #define MULTIPLE_IMPORTANCE_SAMPLING

#ifdef RUSSIAN_ROULETTE
#define VERSION_RR " RR"
#else
#define VERSION_RR ""
#endif

#ifdef DIRECT_LIGHT_SAMPLING
#define VERSION_DLS " DLS"
#else
#define VERSION_DLS ""
#endif

#ifdef MULTIPLE_IMPORTANCE_SAMPLING
#define VERSION_MIS " MIS"
#else
#define VERSION_MIS ""
#endif

#define RAY_VERSION \
    VERSION_RR \
    VERSION_DLS \
    VERSION_MIS

XOR_CONFIG_WINDOW(RTSettings, 5, 5)
{
    XOR_CONFIG_CHECKBOX(multithreaded, "Multithreaded", true);
    XOR_CONFIG_SLIDER(float, exposure, "Exposure", 10.f, 0.1f, 10.f);
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

template <typename... Ts>
float balanceHeuristic(std::pair<int, float> np, const Ts &... ts)
{
    float numerator   = float(np.first) * np.second;
    float denominator = numerator;

    for (const auto &p : { ts... })
        denominator += float(p.first) * p.second;

    return numerator / denominator;
}

template <typename... Ts>
float balanceHeuristic(float p, const Ts &... ts)
{
    float denominator = p;

    for (float p_i : { ts... })
        denominator += p_i;

    return p / denominator;
}

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

    float pdf(float3 wo, float3 wi, float3 n) const
    {
        float cosTheta = clampedDot(wi, n);
        return cosTheta / Pi;
    }
};

struct BSDFBlinnPhong
{
    float exponent = 5.f;

    float3 eval(float3 wo, float3 wi, float3 n) const
    {
        float3 h = normalize((wo + wi) * 0.5f);
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
        float3 h = normalize((wo + wi) * 0.5f);
        float  D = GGX_Distribution(n, h, roughness);

        BSDFSample s;
        s.wi  = sampleGGXVNDF(wi, u);
        s.pdf = D;
        return s;
    }

    float3 eval(float3 wo, float3 wi, float3 n) const
    {
        float3 h = normalize((wo + wi) * 0.5f);

        float dotWoN = dot(wo, n);
        float dotHN  = dot(h, n);

        constexpr float Epsilon = .001f;

        if (dotWoN <= Epsilon) return 0;
        if (dotHN  <= Epsilon) return 0;
        if (all(h == 0.f)) return 0;

#if 0
        constexpr float Clamp   = .02f;
        dotWoN = std::max(Clamp, dotWoN);
        dotHN  = std::max(Clamp, dotHN);
#endif

        float  D = GGX_Distribution(n, h, roughness);
        float  G = GGX_PartialGeometryTerm(wo, n, h, roughness);
        float3 F = Fresnel_Schlick(clampedDot(wo, h));

        float3 f = F * (D * G / (4 * dotWoN * dotHN));
        return f;
    }

    float pdf(float3 wo, float3 wi, float3 n) const
    {
        float3 h = normalize((wo + wi) * 0.5f);
        float D  = GGX_Distribution(n, h, roughness);
        return D;
    }
};

struct BSDFDiffuseWithGGX
{
    BSDFLambertian diffuse;
    BSDFGlossyGGX specular;

    BSDFDiffuseWithGGX() = default;
    BSDFDiffuseWithGGX(float3 color, float3 F0, float roughness)
        : diffuse(color)
        , specular(F0, roughness)
    {}

    static BSDFDiffuseWithGGX dielectric(float3 color, float roughness)
    {
        return BSDFDiffuseWithGGX(color, BSDFGlossyGGX::DielectricF0, roughness);
    }

    static BSDFDiffuseWithGGX metal(float3 color, float roughness)
    {
        return BSDFDiffuseWithGGX(0, color, roughness);
    }

    BSDFSample sample(float2 u) const
    {
        return diffuse.sample(u);
    }

    BSDFSample sample(float3 u, float3 wo, float3 n) const
    {
#ifdef MULTIPLE_IMPORTANCE_SAMPLING
        constexpr float sampleSpecular = .5f;

        float c;
        float pdf = 0;
        float pdf_other;
        float3 wi;

        if (u.x < sampleSpecular)
        {
            c            = sampleSpecular;
            BSDFSample s = specular.sample(u.s_yz, wo, n);
            wi           = s.wi;
            pdf          = s.pdf;
            pdf_other    = diffuse.pdf(wi, wo, n);
        }

        if (pdf == 0.f)
        {
            c            = 1 - sampleSpecular;
            BSDFSample s = diffuse.sample(u.s_yz, wo, n);
            wi           = s.wi;
            pdf          = s.pdf;
            pdf_other    = specular.pdf(wi, wo, n);
        }

        float w = balanceHeuristic(pdf, pdf_other);

        if (pdf == 0.f)
        {
            print("wi: (%f %f %f) p0: %f p1: %f\n"
                  , wi.x
                  , wi.y
                  , wi.z
                  , pdf
                  , pdf_other
            );
        }

        BSDFSample s;
        s.wi  = wi;
        s.pdf = (c * pdf / w);
        return s;

#else
        return diffuse.sample(float2(u), wo, n);
#endif
    }
    
    float3 eval(float3 wo, float3 wi, float3 n) const
    {
        if (specular.roughness >= 100)
            return diffuse.eval(wo, wi, n);
        else
            return diffuse.eval(wo, wi, n) + specular.eval(wo, wi, n);
    }
};

struct Material
{
    float3 color;
    float3 emissive;
    float  roughness = .01f;

    Material(float3 color    = float3(.5f),
             float3 emissive = float3(0),
             float roughness = .1f)
        : color(color)
        , emissive(emissive)
        , roughness(roughness)
    {}

    auto bsdf() const
    {
        return BSDFDiffuseWithGGX::dielectric(color, roughness);
    }
};

struct Surfel
{
    float3 p;
    float3 n;
};

struct SampleBase
{
    float pdf = 0;

    explicit operator bool() const { return pdf > 0; }
};

struct AreaSample : Surfel, SampleBase {};

struct SolidAngleSample : SampleBase
{
    float3 w;
};

struct Sphere;

struct LightSample : SampleBase
{
    const Sphere *light = nullptr;
};

struct RayHit : Surfel
{
    float t              = MaxFloat;
    const Sphere *object = nullptr;

    explicit operator bool() const { return !!object; }

    void eval(Ray ray);
    const Material &material() const;
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

    RayHit rayHit(Ray ray, float t) const
    {
        RayHit h;
        h.object = this;
        h.p      = ray.eval(t);
        h.t      = t;
        h.n      = normalize(h.p - center);
        return h;
    }

    float area() const { return 4 * Pi * radiusSqr; }

    AreaSample sampleArea(float2 u) const
    {
        AreaSample s;
        s.p   = uniformSphere(u) * radius + center;
        s.n   = normalize(s.p - center);
        s.pdf = pdfArea(s.p);
        return s;
    }

    SolidAngleSample sampleSolidAngle(float2 u, float3 from) const
    {
        AreaSample sa = sampleArea(u);
        Ray ray = Ray::fromTo(from, sa.p);
        float t = hit(ray);
        if (t == MaxFloat)
            return SolidAngleSample();

        float3 p = from + t * ray.dir;
        float r2 = p.lengthSqr();
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

void RayHit::eval(Ray ray)
{
    if (*this)
        *this = object->rayHit(ray, t);
}

const Material &RayHit::material() const
{
    return object->material;
}

class RayScene
{
    std::vector<Sphere> objects;
    std::vector<Sphere> lights;
public:
    float3 background;

    RayScene() {}

    LightSample randomLight() const
    { 
        int numLights = int(lights.size());
        int i = std::uniform_int_distribution<int>(0, numLights - 1)(g_gen);

        LightSample s;
        s.light = &lights[i];
        s.pdf   = 1.f / float(numLights);
        return s;
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

        closest.eval(ray);

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
#if 0
        return 0;
#else
        return scene.background;
#endif
    }

    float3 trace(const RayScene &scene, float tNear = 0, float tFar = MaxFloat) const
    {
        int depth = 0;
        float3 throughput = 1.f;

        static constexpr int   MaxDepth           = 1000;

        static constexpr float RussianMinContinueP = .6f;
        static constexpr float RussianMaxContinueP = .95f;

        static constexpr float BounceEpsilon      = .001f;

        RayHit h = scene.hit(ray, tNear, tFar);

        // No hit, return background
        if (!h)
            return miss(scene, ray.dir);

        float3 color = h.material().emissive;

        for (int depth = 0; depth < MaxDepth; ++depth)
        {
            auto bsdf = h.material().bsdf();
            float3 wo = normalize(-ray.dir);
            float3 n  = h.n;

#ifdef DIRECT_LIGHT_SAMPLING
            {
                float3 F_light;
                float3 F_bsdf;
                float pdf_light;
                float pdf_bsdf;

                {
                    LightSample randomLight = scene.randomLight();
                    auto &light             = *randomLight.light;

                    AreaSample dA = light.sampleArea(rnd2());
                    float3 wi     = dA.p - h.p;

                    if (!scene.anyHit(Ray(h.p, wi), BounceEpsilon, 1 - BounceEpsilon))
                    {
                        float r2 = wi.lengthSqr();
                        wi = normalize(wi);
                        float3 wo = normalize(-ray.dir);
                        float cosTheta_i = clampedDot(n, wi);

                        float3 f = bsdf.eval(wo, wi, n) * cosTheta_i;

                        float3 n_L = dA.n;

                        float cosTheta_L       = absDot(-wi, n_L);
                        float areaToSolidAngle = cosTheta_L / r2;

                        pdf_light = dA.pdf / (areaToSolidAngle * light.area());

#ifdef MULTIPLE_IMPORTANCE_SAMPLING
                        F_light   = (areaToSolidAngle / pdf_light) * f * light.material.emissive;
#else
                        F_light   = areaToSolidAngle / dA.pdf * f * light.material.emissive;
#endif
                    }
                    else
                    {
                        pdf_light = 0;
                    }
                }

#ifdef MULTIPLE_IMPORTANCE_SAMPLING
                {
                    BSDFSample s = bsdf.diffuse.sample(rnd2(), wo, n);
                    float3 wi    = s.wi;
                    RayHit sh    = scene.hit(Ray(h.p, wi), BounceEpsilon);

                    pdf_bsdf = s.pdf;

                    float cosTheta = clampedDot(n, wi);

                    float3 f = bsdf.eval(wo, wi, n) * cosTheta;

                    if (sh)
                        F_bsdf = sh.material().emissive;
                    else
                        F_bsdf = miss(scene, s.wi);

                    F_bsdf *= f * (1.f / pdf_bsdf);
                }

                float w_light = balanceHeuristic(pdf_light, pdf_bsdf);
                float w_bsdf  = balanceHeuristic(pdf_bsdf, pdf_light);

                color += throughput *
                    (w_light * F_light +
                     w_bsdf  * F_bsdf);
#else
                color += throughput * F_light;
#endif
            }
#else
            {
                color += throughput * h.material().emissive;
            }
#endif

#ifdef RUSSIAN_ROULETTE
            float continueP = clamp(throughput.largest(), RussianMinContinueP, RussianMaxContinueP);

            // Uniform sampling of the outward facing hemisphere to keep things simple and correct.
            if (rnd() > continueP)
                break;
#else
            static constexpr float continueP = 1.f;
#endif

            float3 wi;
            float pdf;

            {
                BSDFSample s = bsdf.sample(rnd3(), wo, n);
                wi           = s.wi;
                pdf          = s.pdf;
            }

            float cosTheta = clampedDot(wi, n);
            float3 f       = bsdf.eval(wo, wi, n);     

#if 0
            if (!(pdf > 0))
            {
                    print("t: (%f %f %f) f: (%f %f %f) p: %f\n"
                          , throughput.x
                          , throughput.y
                          , throughput.z
                          , f.x
                          , f.y
                          , f.z
                          , pdf
                    );
                XOR_CHECK(pdf > 0, "halp");
            }
#endif

            throughput *= f * // BSDF term
                (cosTheta * // cosine term for incoming radiance
                (1 / pdf) * // PDF normalization for Monte Carlo
                (1 / continueP)); // bias normalization for Russian Roulette

            // Trace the next ray in the path
            h = scene.hit(Ray(h.p, wi), BounceEpsilon);

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

// BSDFSample sample(float2 u) const
// float pdf(float3 wo, float3 wi, float3 n) const

template <typename FPdf, typename FSample>
void testHemisphericalDistribution(const char *name, FPdf &&fPdf, FSample &&fSample)
{
    constexpr size_t NumSamples  = 30000000;
    constexpr int GridResolution = 40;
    constexpr int GridExtent     = GridResolution / 2;

    Timer t;

    using GridCell = uint32_t;
    using Samples  = std::unordered_map<GridCell, double>;

    Samples count;
    Samples expected;
    Samples fromDist;

    auto gridCell = [=](float3 v)
    {
        float3 gridCellF = max(float3(0), v * float3(float(GridExtent)) + float3(float(GridExtent)));
        int3   gridCell  = int3(gridCellF);

        GridCell id =
            (gridCell.x <<  0) | 
            (gridCell.y <<  8) | 
            (gridCell.z << 16);

        return id;
    };

    for (size_t i = 0; i < NumSamples; ++i)
    {
        float2 u      = rnd2();
        float3 wi     = uniformHemisphere(u);
        float pdf     = fPdf(wi);
        GridCell id   = gridCell(wi);

        expected[id] += pdf;
        count[id]    += 1;
    }

    for (auto &s : expected)
    {
        double &x = s.second;
        double  n = count[s.first];

        // Estimate the expected value of samples using the average PDF
        // of the cell.
        double cellPdf  = x / n;
        // For each non-empty cell, estimate its contained hemispherical
        // area by the corresponding proportion of uniform hemispherical samples
        // it contains.
        double cellArea = n / NumSamples * AreaOfUnitHemisphere;

        double expectedSamples = cellPdf * cellArea;
        
        x = expectedSamples;
    }

    for (size_t i = 0; i < NumSamples; ++i)
    {
        float2 u      = rnd2();
        float3 wi     = fSample(u);
        GridCell id   = gridCell(wi);
        fromDist[id] += 1;
    }

    int missingCells   = 0;
    double sumExpected = 0;
    double maxAbs      = 0;
    double maxRel      = 0;
    double maxAbsN     = 0;
    double maxRelN     = 0;

    constexpr double NThreshold = 5;

    for (auto &s : fromDist)
    {
        GridCell id  = s.first;
        double n     = s.second;

        auto it = expected.find(id);
        if (n >= NThreshold && it != expected.end())
        {
            sumExpected += it->second;
            double E    = it->second * NumSamples;
            double dAbs = abs(E - n);
            double dRel = std::max(E, n) / std::min(E, n);

            if (dAbs > maxAbs)
            {
                maxAbs = dAbs;
                maxAbsN = n;
            }

            if (dRel > maxRel)
            {
                maxRel = dRel;
                maxRelN = n;
            }
        }
        else
        {
            ++missingCells;
        }
    }

    print("Tested distribution \"%s\" in %.2f ms\n"
          "             Non-empty cells: %zu\n"
          "      Sum of expected values: %f\n"
          " Largest absolute cell delta: %f (%.1f in cell, %.2f%% of cell, %.2f%% of N)\n"
          " Largest relative cell delta: %f (%.1f in cell)\n"
          "Cells without expected value: %d\n",
          name, t.milliseconds(),
          fromDist.size(),
          sumExpected,
          maxAbs,
          maxAbsN,
          100.0 / maxAbsN * maxAbs,
          100.0 / NumSamples * maxAbs,
          maxRel, maxRelN,
          missingCells);
}

template <typename BSDFDist>
void testBSDFDistribution(const char *name, BSDFDist &&dist,
                          float3 wo = float3(0, 0, 1),
                          float3  n = float3(0, 0, 1))
{
    testHemisphericalDistribution(name,
                                  [&] (float3 wi) { return dist.pdf(wo, wi, n); },
                                  [&] (float2  u) { return dist.sample(u).wi; });
}

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
        scene.add(Sphere(float3( 0, -1001,   0), 1000, Material(0.5f, 0, 100.f)));

        scene.add(Sphere(float3( 5,     3,  -10), 1, Material(1, 20.f)));

        testHemisphericalDistribution("Uniform",
                                      [] (float3 wi) { return 1.f / AreaOfUnitHemisphere; },
                                      [] (float2 u)  { return uniformHemisphere(u); });
        testBSDFDistribution("Lambertian", BSDFLambertian());
        testBSDFDistribution("GGX", BSDFGlossyGGX());
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

    ColorUnorm toneMapReinhard(float3 linearHDRColor) const
    {
        float largest  = linearHDRColor.largest();
        float reinhard = largest / (1 + largest);
        return ColorUnorm(sqrtVec(reinhard * linearHDRColor).s_xyz1.vec4());
    }

    ColorUnorm toneMapReinhardMod(float3 linearHDRColor) const
    {
        float largest     = linearHDRColor.largest();
        float k           = 1.f + largest / cfg_Settings.exposure;
        float reinhardMod = largest * k / (1 + largest);
        return ColorUnorm(sqrtVec(reinhardMod * linearHDRColor).s_xyz1.vec4());
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
                if (cfg_Settings.exposure.isMax())
                {
                    for (uint x = 0; x < sz.x; ++x)
                    {
                        uint2 coords(x, y);
                        ldrImage.pixel<ColorUnorm>(coords) = toneMapReinhard(hdrImage.pixel<float3>(coords) / float(numSamples));
                    }
                }
                else
                {
                    for (uint x = 0; x < sz.x; ++x)
                    {
                        uint2 coords(x, y);
                        ldrImage.pixel<ColorUnorm>(coords) = toneMapReinhardMod(hdrImage.pixel<float3>(coords) / float(numSamples));
                    }
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

