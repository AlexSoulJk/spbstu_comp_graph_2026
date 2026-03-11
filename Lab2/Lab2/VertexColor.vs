cbuffer MatrixBuffer : register(b0) { matrix Model; };

cbuffer CameraBuffer : register(b1)
{
    matrix vp;
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Pos, 1.0f), Model);
    output.WorldPos = worldPos.xyz;
    output.Pos = mul(worldPos, vp);
    output.NormalW = mul(input.Normal, (float3x3)Model);
    return output;
}