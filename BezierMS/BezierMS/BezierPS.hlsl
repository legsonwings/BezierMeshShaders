
struct VertexIn
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS : POSITION0;
    float3 Normal : NORMAL0;
};

float4 main(VertexIn input) : SV_TARGET
{
    float ambientIntensity = 0.2f;
    float3 lightColor = float3(1, 1, 1);
    float3 lightDir = -normalize(float3(-1, -1, 1));

    float3 diffuseColor = float3(0.8f, 0.f, 0.f);
    float shininess = 64.f;

    float3 normal = normalize(input.Normal);

    // Do some fancy Blinn-Phong shading!
    float cosAngle = saturate(dot(normal, lightDir));
    float3 viewDir = -normalize(input.PositionVS);
    float3 halfAngle = normalize(lightDir + viewDir);

    float blinnTerm = saturate(dot(normal, halfAngle));
    blinnTerm = cosAngle != 0.0 ? blinnTerm : 0.0;
    blinnTerm = pow(blinnTerm, shininess);

    float3 finalColor = (cosAngle + blinnTerm + ambientIntensity) * diffuseColor;
    
    return float4(finalColor, 1);
}
