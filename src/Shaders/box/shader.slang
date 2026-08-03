struct UBO {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UBO> ubo;
Sampler2D texSampler;

struct VertexInput {
    float3 position;
    float2 texCoord;
};

struct VertexOutput {
    float2 texCoord;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input) {
    VertexOutput output;
    output.sv_position = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.position, 1.0))));
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
    return texSampler.Sample(inVert.texCoord);
}
