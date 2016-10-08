#include "$safeitemname$.sig.h"

struct VSInput
{
	float3 pos : POSITION;
};

struct VSOutput
{
    float4 pos : SV_Position;
};

[RootSignature($safeitemname$_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;
	o.pos = 0;
    return o;
}
