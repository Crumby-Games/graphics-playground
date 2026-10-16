cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 vOutputColor;
}

TextureCube envMap : register(t0);

SamplerState samLinear : register(s0);

struct VS_INPUT
{
    float4 Pos : POSITION;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
};

PS_INPUT VS(VS_INPUT IN)
{
    PS_INPUT output = (PS_INPUT) 0;
    float4x4 viewRotation = View;
    viewRotation._m03_m13_m23 = float3(0, 0, 0); // remove translation from matrix
    
    float4 pos = mul(Projection * viewRotation, float4(IN.Pos.xyz, 1.0));
    output.Pos = pos.xyww; // Perform swizzling
    return output;
}

float4 PS_SKYBOX(PS_INPUT IN) : SV_Target
{
    float3 color = envMap.Sample(samLinear, IN.Pos.xyz);
    return float4(color, 1);
}
