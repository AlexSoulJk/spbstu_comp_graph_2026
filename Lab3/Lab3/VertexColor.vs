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
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitangentW : TEXCOORD4;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Pos, 1.0f), Model);
    output.WorldPos = worldPos.xyz;
    output.Pos = mul(worldPos, vp);
    float3 N = normalize(mul(input.Normal, (float3x3)Model));
    output.NormalW = N;
    float3 tangent;
    if (abs(N.z) > 0.999f)
    {
        tangent = float3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        tangent = normalize(cross(N, float3(0.0f, 0.0f, 1.0f)));
    }
    float3 bitangent = normalize(cross(N, tangent));
    output.TangentW = tangent;
    output.BitangentW = bitangent;
    output.TexCoord = input.TexCoord;
    return output;
}
