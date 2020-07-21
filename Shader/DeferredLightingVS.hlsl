#include "VertexType.hlsl"

StandardQuadPSInput DeferredLightingVSMain(StandardQuadVSInput vin)
{
	StandardQuadPSInput vout ;
	vout.positionH = float4(vin.positionH,1.f);
	vout.texcoord = vin.texcoord;
	return vout;
}