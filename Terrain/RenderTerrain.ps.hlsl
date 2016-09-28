#include "RenderTerrain.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 svPos    : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float h = (i.worldPos.y - heightMin) / (heightMax - heightMin);
#ifdef WIREFRAME
    float3 color = 1;
#else
    float3 color = wireframe ? 1 : h;
#endif
	return float4(color, 1);
}
