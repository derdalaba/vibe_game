#include "GltfLoader.hpp"

// The implementation lives in tiny_gltf_impl.cpp; here we only need the
// declarations. The NO_STB defines must match that TU so the header does not
// pull in a second copy of stb_image (Renderer already owns that).
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <algorithm>
#include <stdexcept>

namespace Core {
namespace {

// A resolved accessor: base pointer, element stride, and how to decode each
// component. Respecting `stride` is essential -- glTF bufferViews may be
// interleaved or over-strided (SimpleSkin stores an 8-byte ushort4 inside a
// 16-byte stride), so assuming tight packing reads garbage.
struct View {
    const unsigned char* data = nullptr;
    size_t stride = 0;
    size_t count = 0;
    int componentType = -1;
    int numComponents = 0;
    bool normalized = false;
};

View makeView(const tinygltf::Model& model, int accessorIndex,
              const char* what) {
    if (accessorIndex < 0 ||
        static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        throw std::runtime_error(std::string("glTF: missing accessor for ") +
                                 what);
    }
    const tinygltf::Accessor& accessor =
        model.accessors[static_cast<size_t>(accessorIndex)];
    if (accessor.sparse.isSparse) {
        throw std::runtime_error(std::string("glTF: sparse accessors are not "
                                             "supported (") +
                                 what + ")");
    }
    if (accessor.bufferView < 0) {
        throw std::runtime_error(std::string("glTF: accessor without "
                                             "bufferView for ") +
                                 what);
    }
    const tinygltf::BufferView& bufferView =
        model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    const tinygltf::Buffer& buffer =
        model.buffers[static_cast<size_t>(bufferView.buffer)];

    const int stride = accessor.ByteStride(bufferView);
    if (stride <= 0) {
        throw std::runtime_error(std::string("glTF: invalid byteStride for ") +
                                 what);
    }

    View view;
    view.data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    view.stride = static_cast<size_t>(stride);
    view.count = accessor.count;
    view.componentType = accessor.componentType;
    view.numComponents = tinygltf::GetNumComponentsInType(
        static_cast<uint32_t>(accessor.type));
    view.normalized = accessor.normalized;
    return view;
}

const unsigned char* elementPtr(const View& view, size_t element) {
    return view.data + element * view.stride;
}

// Reads one component as float, honouring glTF's normalized integer encodings
// (WEIGHTS_0 and TEXCOORD_0 may be float, normalized u8, or normalized u16).
float readFloat(const View& view, size_t element, int component) {
    if (component >= view.numComponents) {
        return 0.0f;
    }
    const unsigned char* base = elementPtr(view, element);
    switch (view.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float value = 0.0f;
            memcpy(&value, base + sizeof(float) * static_cast<size_t>(component),
                   sizeof(float));
            return value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t raw = base[component];
            return view.normalized ? static_cast<float>(raw) / 255.0f
                                   : static_cast<float>(raw);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t raw = 0;
            memcpy(&raw, base + sizeof(uint16_t) * static_cast<size_t>(component),
                   sizeof(uint16_t));
            return view.normalized ? static_cast<float>(raw) / 65535.0f
                                   : static_cast<float>(raw);
        }
        default:
            throw std::runtime_error(
                "glTF: unsupported component type for float data");
    }
}

// Reads one component as an unsigned integer (JOINTS_0 is u8 or u16 per spec;
// indices may additionally be u32).
uint32_t readUint(const View& view, size_t element, int component) {
    if (component >= view.numComponents) {
        return 0u;
    }
    const unsigned char* base = elementPtr(view, element);
    switch (view.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return base[component];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t raw = 0;
            memcpy(&raw, base + sizeof(uint16_t) * static_cast<size_t>(component),
                   sizeof(uint16_t));
            return raw;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t raw = 0;
            memcpy(&raw, base + sizeof(uint32_t) * static_cast<size_t>(component),
                   sizeof(uint32_t));
            return raw;
        }
        default:
            throw std::runtime_error(
                "glTF: unsupported component type for integer data");
    }
}

Transform nodeTransform(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        throw std::runtime_error(
            "glTF: nodes using the 'matrix' form are not supported; re-export "
            "with TRS (translation/rotation/scale)");
    }
    Transform transform;
    if (node.translation.size() == 3) {
        transform.translation = glm::vec3(static_cast<float>(node.translation[0]),
                                         static_cast<float>(node.translation[1]),
                                         static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        // glTF stores xyzw; glm's quat constructor takes (w, x, y, z).
        transform.rotation = glm::quat(static_cast<float>(node.rotation[3]),
                                       static_cast<float>(node.rotation[0]),
                                       static_cast<float>(node.rotation[1]),
                                       static_cast<float>(node.rotation[2]));
    }
    if (node.scale.size() == 3) {
        transform.scale = glm::vec3(static_cast<float>(node.scale[0]),
                                    static_cast<float>(node.scale[1]),
                                    static_cast<float>(node.scale[2]));
    }
    return transform;
}

Path parsePath(const std::string& path, bool& supported) {
    supported = true;
    if (path == "translation") return Path::Translation;
    if (path == "rotation") return Path::Rotation;
    if (path == "scale") return Path::Scale;
    supported = false;  // "weights" (morph targets) is out of scope
    return Path::Translation;
}

Interpolation parseInterpolation(const std::string& mode) {
    if (mode == "STEP") return Interpolation::Step;
    if (mode == "CUBICSPLINE") return Interpolation::CubicSpline;
    return Interpolation::Linear;
}

void loadPrimitive(const tinygltf::Model& model,
                   const tinygltf::Primitive& primitive, MeshData& out) {
    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
        throw std::runtime_error("glTF: primitive without POSITION attribute");
    }
    const View positions = makeView(model, positionIt->second, "POSITION");

    // Optional attributes. SimpleSkin has no TEXCOORD_0, so absence must be
    // tolerated and defaulted rather than treated as an error.
    const auto texIt = primitive.attributes.find("TEXCOORD_0");
    const auto jointsIt = primitive.attributes.find("JOINTS_0");
    const auto weightsIt = primitive.attributes.find("WEIGHTS_0");

    const bool hasTex = texIt != primitive.attributes.end();
    const bool hasJoints = jointsIt != primitive.attributes.end();
    const bool hasWeights = weightsIt != primitive.attributes.end();

    View texCoords, joints, weights;
    if (hasTex) texCoords = makeView(model, texIt->second, "TEXCOORD_0");
    if (hasJoints) joints = makeView(model, jointsIt->second, "JOINTS_0");
    if (hasWeights) weights = makeView(model, weightsIt->second, "WEIGHTS_0");

    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());

    for (size_t i = 0; i < positions.count; ++i) {
        Vertex vertex{};  // zero-init: unused joints/weights stay 0
        vertex.pos[0] = readFloat(positions, i, 0);
        vertex.pos[1] = readFloat(positions, i, 1);
        vertex.pos[2] = readFloat(positions, i, 2);

        if (hasTex) {
            vertex.texCoord[0] = readFloat(texCoords, i, 0);
            // glTF's V axis points down relative to how the image is sampled.
            vertex.texCoord[1] = 1.0f - readFloat(texCoords, i, 1);
        }
        if (hasJoints) {
            for (int c = 0; c < 4; ++c) {
                vertex.joints[c] = readUint(joints, i, c);
            }
        }
        if (hasWeights) {
            for (int c = 0; c < 4; ++c) {
                vertex.weights[c] = readFloat(weights, i, c);
            }
        } else {
            // Unskinned geometry: bind fully to joint 0 so a skinning shader
            // with an identity joint matrix reproduces the rest pose.
            vertex.weights[0] = 1.0f;
        }
        out.vertices.push_back(vertex);
    }

    if (primitive.indices >= 0) {
        const View indices = makeView(model, primitive.indices, "indices");
        for (size_t i = 0; i < indices.count; ++i) {
            out.indices.push_back(baseVertex + readUint(indices, i, 0));
        }
    } else {
        for (size_t i = 0; i < positions.count; ++i) {
            out.indices.push_back(baseVertex + static_cast<uint32_t>(i));
        }
    }
}

}  // namespace

LoadedModel loadGltf(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    const bool isBinary =
        path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0;
    // v2 does not auto-detect container format from content.
    const bool ok = isBinary
                        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                        : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) {
        throw std::runtime_error("glTF: failed to load " + path + ": " + err +
                                 warn);
    }

    LoadedModel result;

    // --- node hierarchy -----------------------------------------------------
    const size_t nodeCount = model.nodes.size();
    result.skeleton.parents.assign(nodeCount, -1);
    result.skeleton.localPose.resize(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i) {
        result.skeleton.localPose[i] = nodeTransform(model.nodes[i]);
        for (int child : model.nodes[i].children) {
            if (child >= 0 && static_cast<size_t>(child) < nodeCount) {
                result.skeleton.parents[static_cast<size_t>(child)] =
                    static_cast<int>(i);
            }
        }
    }
    result.skeleton.restPose = result.skeleton.localPose;

    // --- pick the mesh: prefer a skinned one -------------------------------
    int meshNode = -1;
    for (size_t i = 0; i < nodeCount; ++i) {
        if (model.nodes[i].mesh >= 0) {
            if (model.nodes[i].skin >= 0) {
                meshNode = static_cast<int>(i);
                break;
            }
            if (meshNode < 0) {
                meshNode = static_cast<int>(i);
            }
        }
    }
    if (meshNode < 0) {
        throw std::runtime_error("glTF: no mesh found in " + path);
    }

    const tinygltf::Node& node = model.nodes[static_cast<size_t>(meshNode)];
    const tinygltf::Mesh& mesh = model.meshes[static_cast<size_t>(node.mesh)];
    for (const auto& primitive : mesh.primitives) {
        loadPrimitive(model, primitive, result.mesh);
    }

    // --- skin --------------------------------------------------------------
    if (node.skin >= 0) {
        const tinygltf::Skin& skin = model.skins[static_cast<size_t>(node.skin)];
        result.skeleton.jointNodes = skin.joints;
        result.skeleton.inverseBind.assign(skin.joints.size(),
                                           glm::mat4(1.0f));
        // inverseBindMatrices is optional; absent means identity per joint.
        if (skin.inverseBindMatrices >= 0) {
            const View ibm =
                makeView(model, skin.inverseBindMatrices, "inverseBindMatrices");
            const size_t count =
                std::min(ibm.count, result.skeleton.inverseBind.size());
            for (size_t j = 0; j < count; ++j) {
                glm::mat4 m(1.0f);
                for (int c = 0; c < 16; ++c) {
                    // glTF matrices are column-major, matching glm's
                    // m[column][row] storage order.
                    m[c / 4][c % 4] = readFloat(ibm, j, c);
                }
                result.skeleton.inverseBind[j] = m;
            }
        }
    }

    // --- animations --------------------------------------------------------
    for (const auto& animation : model.animations) {
        AnimationClip clip;
        clip.name = animation.name;
        for (const auto& channel : animation.channels) {
            if (channel.sampler < 0 || channel.target_node < 0) {
                continue;
            }
            bool supported = false;
            const Path pathKind = parsePath(channel.target_path, supported);
            if (!supported) {
                continue;  // morph-target weights are out of scope
            }
            const tinygltf::AnimationSampler& sampler =
                animation.samplers[static_cast<size_t>(channel.sampler)];

            Channel out;
            out.targetNode = channel.target_node;
            out.path = pathKind;
            out.track.interp = parseInterpolation(sampler.interpolation);

            const View input = makeView(model, sampler.input, "animation input");
            const View output =
                makeView(model, sampler.output, "animation output");

            out.track.times.reserve(input.count);
            for (size_t k = 0; k < input.count; ++k) {
                out.track.times.push_back(readFloat(input, k, 0));
            }
            out.track.values.reserve(output.count);
            for (size_t k = 0; k < output.count; ++k) {
                out.track.values.push_back(
                    glm::vec4(readFloat(output, k, 0), readFloat(output, k, 1),
                              readFloat(output, k, 2), readFloat(output, k, 3)));
            }
            if (!out.track.times.empty()) {
                clip.duration = std::max(clip.duration, out.track.times.back());
            }
            clip.channels.push_back(std::move(out));
        }
        if (!clip.channels.empty()) {
            result.clips.push_back(std::move(clip));
        }
    }

    return result;
}

}  // namespace Core
