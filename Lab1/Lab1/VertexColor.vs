cbuffer MatrixBuffer : register(b0)
{
    matrix m;
};

cbuffer CameraBuffer : register(b1)
{
    matrix vp;
};

struct VSInput
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 color : COLOR;
};

VSOutput main(VSInput vertex)
{
    VSOutput output;
    float4 pos = float4(vertex.pos, 1.0);
    output.pos = mul(pos, m);
    output.pos = mul(output.pos, vp);
    output.color = vertex.color;
    return output;
}