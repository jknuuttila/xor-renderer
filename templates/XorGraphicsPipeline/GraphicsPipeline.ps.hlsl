#include "$safeitemname$.sig.h"

struct PSInput
{
    float4 svPos : SV_Position;
};

[RootSignature($safeitemname$_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
	return float4(1, 0, 0, 1);
}
