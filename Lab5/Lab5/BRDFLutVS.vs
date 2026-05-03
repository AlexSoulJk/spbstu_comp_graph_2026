struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float2 BuildFullscreenTriangleClipPos(uint vertexId)
{
    float2 clipPos;
    clipPos.x = (vertexId == 2u) ? 3.0f : -1.0f;
    clipPos.y = (vertexId == 1u) ? 3.0f : -1.0f;
    return clipPos;
}

float2 ClipToUV(float2 clipPos)
{
    return float2((clipPos.x + 1.0f) * 0.5f, 1.0f - (clipPos.y + 1.0f) * 0.5f);
}

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
    VS_OUTPUT output;
    float2 clipPos = BuildFullscreenTriangleClipPos(vertexId);
    output.Pos = float4(clipPos, 0.0f, 1.0f);
    output.TexCoord = ClipToUV(clipPos);
    return output;
}
