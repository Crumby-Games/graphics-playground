//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
cbuffer ConstantBuffer : register( b0 )
{
	matrix World;
	matrix View;
	matrix Projection;
	float4 vOutputColor;
}

cbuffer CustomConstantBuffer : register(b2)
{
    float gTime;
	
}

Texture2D albedoMap : register(t0);
Texture2D metallicMap : register(t1);
Texture2D roughnessMap : register(t2);
TextureCube iblDiffuseMap : register(t3);
TextureCube iblSpecularMap : register(t4);
SamplerState samLinear : register(s0);

static const float PI = 3.14159265f;
static const float maxReflectionLod = 10;

#define MAX_LIGHTS 1
// Light types.
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

struct Light
{
	float4      Position;               // 16 bytes
										//----------------------------------- (16 byte boundary)
	float4      Direction;              // 16 bytes
										//----------------------------------- (16 byte boundary)
	float4      Color;                  // 16 bytes
										//----------------------------------- (16 byte boundary)
	float       SpotAngle;              // 4 bytes
	float       ConstantAttenuation;    // 4 bytes
	float       LinearAttenuation;      // 4 bytes
	float       QuadraticAttenuation;   // 4 bytes
										//----------------------------------- (16 byte boundary)
	int         LightType;              // 4 bytes
	bool        Enabled;                // 4 bytes
	int2        Padding;                // 8 bytes
										//----------------------------------- (16 byte boundary)
};  // Total:                           // 80 bytes (5 * 16)

cbuffer LightProperties : register(b1)
{
	float4 EyePosition;                 // 16 bytes
										//----------------------------------- (16 byte boundary)
	float4 GlobalAmbient;               // 16 bytes
										//----------------------------------- (16 byte boundary)
	Light Lights[MAX_LIGHTS];           // 80 * 8 = 640 bytes
}; 


//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float4 Pos : POSITION;
    float3 Norm : NORMAL;
    float4 Tangent : TANGENT;
    float2 Tex : TEXCOORD0;
    uint4 Joints : BLENDINDICES0;
    float4 Weights : BLENDWEIGHT0;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
	float4 worldPos : POSITION;
	float3 Norm : NORMAL;
	float2 Tex : TEXCOORD0;
};


float4 DoDiffuse(Light light, float3 L, float3 N)
{
	float NdotL = max(0, dot(N, L));
	return light.Color * NdotL;
}

float4 DoSpecular(Light lightObject, float3 pixelToEyeVectorNormalised, float3 pixelToLightVectorNormalised, float3 Normal)
{
    float lightIntensity = saturate(dot(Normal, pixelToLightVectorNormalised));
	float4 specular = float4(0, 0, 0, 0);
	if (lightIntensity > 0.0f)
	{
		// note the reflection equation requires the *light to pixel* vector - hence we reverse it
        float3 reflection = reflect(-pixelToLightVectorNormalised, Normal);
        specular = pow(saturate(dot(reflection, pixelToEyeVectorNormalised)), 32); // 32 = specular power Material.SpecularPower
    }

	return specular;
}

float DoAttenuation(Light light, float d)
{
	return 1.0f / (light.ConstantAttenuation + light.LinearAttenuation * d + light.QuadraticAttenuation * d * d);
}

struct LightingResult
{
	float4 Diffuse;
	float4 Specular;
};

LightingResult DoPointLight(Light light, float3 pixelToLightVectorNormalised, float3 pixelToEyeVectorNormalised, float distanceFromPixelToLight, float3 N)
{
	LightingResult result;

    float attenuation = DoAttenuation(light, distanceFromPixelToLight);
    attenuation = 1;

    result.Diffuse = DoDiffuse(light, pixelToLightVectorNormalised, N) * attenuation;
    result.Specular = DoSpecular(light, pixelToEyeVectorNormalised, pixelToLightVectorNormalised, N) * attenuation; 

	return result;
}

LightingResult ComputeLighting(float4 pixelToLightVectorNormalised, float4 pixelToEyeVectorNormalised, float distanceFromPixelToLight, float3 N)
{
	LightingResult totalResult = { { 0, 0, 0, 0 },{ 0, 0, 0, 0 } };

	[unroll]
	for (int i = 0; i < MAX_LIGHTS; ++i)
	{
		LightingResult result = { { 0, 0, 0, 0 },{ 0, 0, 0, 0 } };

		if (!Lights[i].Enabled) 
			continue;
		
        result = DoPointLight(Lights[i], pixelToLightVectorNormalised.xyz, pixelToEyeVectorNormalised.xyz, distanceFromPixelToLight, N);
		
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	totalResult.Diffuse = saturate(totalResult.Diffuse);
	totalResult.Specular = saturate(totalResult.Specular);

	return totalResult;
}

float G_sub(float3 N, float3 V, float k)
{
    float3 NdotV = dot(N, V);
    return NdotV / (NdotV * (1 - k) + k);
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    
    float4 r = roughness * c0 * c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 * r.zw;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( VS_INPUT input )
{
    PS_INPUT output = (PS_INPUT)0;
    output.Pos = mul( input.Pos, World );
	output.worldPos = output.Pos;
    output.Pos = mul( output.Pos, View );
    output.Pos = mul( output.Pos, Projection );

	// multiply the normal by the world transform (to go from model space to world space)
	output.Norm = mul(float4(input.Norm, 0), World).xyz;

	output.Tex = input.Tex;
    
    return output;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float4 PS_PBR(PS_INPUT IN) : SV_TARGET
{
    //float3 albedo = float3(0.3, 0.3, 0.3);
    //float metallic = 0.2;
    //float roughness = 1.0;
    float3 albedo = albedoMap.Sample(samLinear, IN.Tex);
    float metallic = metallicMap.Sample(samLinear, IN.Tex).r;
    float roughness = roughnessMap.Sample(samLinear, IN.Tex).r;
 
// 2. Get the normal from the normal map 

    float3 N = IN.Norm;
 
// 3. Calculate vectors 
    float3 V = normalize(EyePosition - IN.worldPos); // View vector 
    float3 L = normalize(Lights[0].Position - IN.worldPos); // Light vector 
    float3 H = normalize(V + L); // Halfway vector 
    float3 R = reflect(-V, N);
    float cosTheta = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
 
 
// 4. Calculate Fresnel F0 
// For dielectrics, F0 is constant. For metals, it's the albedo. 
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);
	
    float3 F = F0 + (1 - F0) * pow(1 - cosTheta, 5);
	
// 5. Specular Component
// 5.1 NDF
    float rSq = roughness * roughness;
    float NdotH = dot(N, H);
    float D = rSq / (PI * pow(pow(NdotH, 2) * (rSq - 1) + 1, 2));

//// 5.2 GGX
    float k = pow(roughness + 1, 2) / 8;
    float G = G_sub(L, N, k) * G_sub(V, N, k);
	
//// 5.3 Specular BRDF
    float3 specular = (D * G * F) / (4 * dot(N, L) * dot(N, V));

//// 6. Diffuse Component
    float3 dColor = (1 - F) * (1 - metallic);
    float3 diffuse = dColor * albedo / PI;

//// 7. Result
    float3 finalPoint = (diffuse + specular) * NdotL;
    
	

// Part B

    
    // Part B: Environment
    float3 finalIBL = float3(0, 0, 0);
    const int typeIBL = 2;
    switch (typeIBL)
    {
        case 0:
            const float3 ambientColor = float3(0.1, 0.1, 0.1);
            finalIBL = ambient * dColor * albedo;
            break;
        case 1:
            const float3 skyCol = float3(0.11, 0.11, 0.94);
            const float3 groundCol = float3(0.0, 0.33, 0.0);
            const float3 up = float3(0, 1, 0);
            
            float weightN = dot(N, up) * 0.5 + 0.5;
            float weightR = dot(R, up) * 0.5 + 0.5;
            float3 diffuseLightHem = lerp(groundCol, skyCol, weightN);
            float3 specularLightHem = lerp(groundCol, skyCol, weightR);
            float3 diffuseHem = dColor * albedo * diffuseLightHem;
            float3 specularHem = F * specularLightHem;
            finalIBL = diffuseHem + specularHem;
            
            break;
    
        case 2:

            float3 irradiance = iblDiffuseMap.Sample(samLinear, N);
            float3 diffuseIBL = dColor * albedo * irradiance;
            
            float prefilteredLod = roughness * maxReflectionLod;
            float3 prefilteredColour = iblSpecularMap.SampleLevel(samLinear, R, prefilteredLod).rgb;
        
            float2 brdf = IntegrateBRDF(dot(N, V), roughness);
            float3 specularIBL = prefilteredColour * (F * brdf.x + brdf.y);
            finalIBL = diffuseIBL + specularIBL;
        
            
            break;
    }


    float3 ambient = 0.01 * albedo;
    return float4(finalPoint + finalIBL + ambient, 1.0);
}


//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------

float4 PS_Normal(PS_INPUT IN) : SV_TARGET
{
    float4 pixelToLightVectorNormalised = normalize(Lights[0].Position - IN.worldPos);
    float4 pixelToEyeVectorNormalised = normalize(EyePosition - IN.worldPos);
    float distanceFromPixelToLight = length(IN.worldPos - Lights[0].Position);
	
    LightingResult lit = ComputeLighting(pixelToLightVectorNormalised, pixelToEyeVectorNormalised, distanceFromPixelToLight, normalize(IN.Norm));

	// NOTE we aren't using any material properties just yet. 
    float4 ambient = GlobalAmbient;
	float4 diffuse = lit.Diffuse;
	float4 specular = lit.Specular;

    float4 texColor = albedoMap.Sample(samLinear, float2(1.0 - (IN.Tex.x + gTime * 0.1), IN.Tex.y));
	
    float diffuseIntensity = saturate(dot(normalize(IN.Norm), pixelToLightVectorNormalised.xyz));
    if (diffuseIntensity > 0.8)
        diffuseIntensity = 1.0;
    else if (diffuseIntensity > 0.4)
        diffuseIntensity = 0.6;
    else
        diffuseIntensity = 0.2;
    //float4 texColor = texColorPrimary;
    float4 diffuseColor = (ambient + (diffuse + specular) * diffuseIntensity) * texColor;
    return float4(diffuseColor.rgb, 1.0);
}

//--------------------------------------------------------------------------------------
// PSSolid - render a solid color
//--------------------------------------------------------------------------------------
float4 PSSolid(PS_INPUT input) : SV_Target
{
	return vOutputColor;
}
