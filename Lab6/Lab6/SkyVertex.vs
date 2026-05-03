cbuffer MatrixBuffer : register(b0)
{
	matrix Model;
};

cbuffer CameraBuffer : register(b1)
{
	matrix vp;
};

struct VS_INPUT
{
	float3 Pos : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 Dir : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT o;
	float keep = input.Normal.x + input.Normal.y + input.Normal.z + input.TexCoord.x + input.TexCoord.y;
	float4 worldPos = mul(float4(input.Pos, 1.0f), Model);
	o.Pos = mul(worldPos, vp);
	float3 dir = mul(input.Pos, (float3x3)Model);
	o.Dir = normalize(dir + keep * 0.0f);
	return o;
}
