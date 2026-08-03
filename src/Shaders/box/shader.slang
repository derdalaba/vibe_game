// Explicit bindings/locations: Slang otherwise assigns them positionally from
// declaration order, so appending or reordering a resource would silently shift
// the others out from under the C++ descriptor writes.
struct UBO {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] Sampler2D texSampler;

// Joint matrices for every skinned instance, concatenated. Each draw selects
// its own range via pc.jointOffset. StructuredBuffer maps to a readonly storage
// buffer, so the array is runtime-sized (no compile-time joint cap).
[[vk::binding(2, 0)]] StructuredBuffer<float4x4> jointMatrices;

struct PushConstants {
    float4x4 model;    // object placement in the world
    uint jointOffset;  // first joint matrix belonging to this instance
};
[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 texCoord;
    [[vk::location(2)]] uint4 joints;
    [[vk::location(3)]] float4 weights;
};

struct VertexOutput {
    float2 texCoord;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input) {
    // Linear blend skinning. The joint matrices already carry
    // globalTransform(joint) * inverseBindMatrix, so this takes the vertex from
    // bind pose into the model's space; pc.model then places it in the world.
    // The glTF mesh node's own transform is deliberately ignored, per spec.
    float4x4 skin =
        input.weights.x * jointMatrices[pc.jointOffset + input.joints.x] +
        input.weights.y * jointMatrices[pc.jointOffset + input.joints.y] +
        input.weights.z * jointMatrices[pc.jointOffset + input.joints.z] +
        input.weights.w * jointMatrices[pc.jointOffset + input.joints.w];

    float4 skinned = mul(skin, float4(input.position, 1.0));

    VertexOutput output;
    output.sv_position = mul(ubo.proj, mul(ubo.view, mul(pc.model, skinned)));
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
    return texSampler.Sample(inVert.texCoord);
}
