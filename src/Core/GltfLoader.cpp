#include "GltfLoader.hpp"

// The implementation lives in tiny_gltf_impl.cpp; here we only need the
// declarations. The NO_STB defines must match that TU so the header does not
// pull in a second copy of stb_image (Renderer already owns that).
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf_v3.h"

#include <algorithm>
#include <cctype>
#include <source_location>
#include <stdexcept>
#include "spdlog/spdlog.h"

namespace Core {
namespace {

[[noreturn]] void throwWithLocation(std::string message, std::source_location location = std::source_location::current()) {
    throw std::runtime_error(std::string(location.function_name()) + ": " + std::move(message));
}

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

View makeView(const tg3_model& model, int accessorIndex, const char* what) {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors_count) {
        throwWithLocation(std::string("glTF: missing accessor for ") + what);
    }
    const tg3_accessor& accessor = model.accessors[static_cast<size_t>(accessorIndex)];
    if (accessor.sparse.is_sparse) {
        throwWithLocation(std::string("glTF: sparse accessors are not supported (") + what + ")");
    }
    if (accessor.buffer_view < 0) {
        throwWithLocation(std::string("glTF: accessor without bufferView for ") + what);
    }
    const tg3_buffer_view& bufferView = model.buffer_views[static_cast<size_t>(accessor.buffer_view)];
    const tg3_buffer& buffer = model.buffers[static_cast<size_t>(bufferView.buffer)];

    const int stride =
        bufferView.byte_stride > 0
            ? static_cast<int>(bufferView.byte_stride)
            : static_cast<int>(tg3_component_size(accessor.component_type) * tg3_num_components(static_cast<uint32_t>(accessor.type)));
    if (stride <= 0) {
        throwWithLocation(std::string("glTF: invalid byteStride for ") + what);
    }

    View view;
    view.data = buffer.data.data + bufferView.byte_offset + accessor.byte_offset;
    view.stride = static_cast<size_t>(stride);
    view.count = accessor.count;
    view.componentType = accessor.component_type;
    view.numComponents = tg3_num_components(static_cast<uint32_t>(accessor.type));
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
        case TG3_COMPONENT_TYPE_FLOAT: {
            float value = 0.0f;
            memcpy(&value, base + sizeof(float) * static_cast<size_t>(component), sizeof(float));
            return value;
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t raw = base[component];
            return view.normalized ? static_cast<float>(raw) / 255.0f : static_cast<float>(raw);
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t raw = 0;
            memcpy(&raw, base + sizeof(uint16_t) * static_cast<size_t>(component), sizeof(uint16_t));
            return view.normalized ? static_cast<float>(raw) / 65535.0f : static_cast<float>(raw);
        }
        default:
            throwWithLocation("glTF: unsupported component type for float data");
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
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
            return base[component];
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t raw = 0;
            memcpy(&raw, base + sizeof(uint16_t) * static_cast<size_t>(component), sizeof(uint16_t));
            return raw;
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t raw = 0;
            memcpy(&raw, base + sizeof(uint32_t) * static_cast<size_t>(component), sizeof(uint32_t));
            return raw;
        }
        default:
            throwWithLocation("glTF: unsupported component type for integer data");
    }
}

bool isIdentity(const double matrix[16]) {
    for (int i = 0; i < 16; ++i) {
        if (matrix[i] != ((i % 5) == 0 ? 1.0 : 0.0)) {
            return false;
        }
    }
    return true;
}

Transform nodeTransform(const tg3_node& node) {
    if (!isIdentity(node.matrix)) {
        throwWithLocation(
            "glTF: nodes using the 'matrix' form are not supported; re-export "
            "with TRS (translation/rotation/scale)");
    }
    Transform transform;

    transform.translation = glm::vec3(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
                                      static_cast<float>(node.translation[2]));

    // glTF stores xyzw; glm's quat constructor takes (w, x, y, z).
    transform.rotation = glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                   static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));

    transform.scale = glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]));

    return transform;
}

Path parsePath(const std::string& path, bool& supported) {
    supported = true;
    if (path == "translation")
        return Path::Translation;
    if (path == "rotation")
        return Path::Rotation;
    if (path == "scale")
        return Path::Scale;
    supported = false;  // "weights" (morph targets) is out of scope
    return Path::Translation;
}

Interpolation parseInterpolation(const std::string& mode) {
    if (mode == "STEP")
        return Interpolation::Step;
    if (mode == "CUBICSPLINE")
        return Interpolation::CubicSpline;
    return Interpolation::Linear;
}

std::string tg3_str_to_str(const tg3_str& tg3str) {
    return std::string(tg3str.data, tg3str.len);
}

void loadPrimitive(const tg3_model& model, const tg3_primitive& primitive, MeshData& out) {
    std::vector<std::string> allowedAttributes = {"POSITION", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"};
    std::vector<std::string> unsupportedAttributes;
    std::unordered_map<std::string, int32_t> attributeMap;
    spdlog::get("VibeGame")->debug("glTF: loading primitive with {} attributes", primitive.attributes_count);
    for (uint32_t i = 0; i < primitive.attributes_count; i++) {
        if (std::find(allowedAttributes.begin(), allowedAttributes.end(), tg3_str_to_str(primitive.attributes[i].key)) !=
            allowedAttributes.end()) {
            auto emplace_result = attributeMap.try_emplace(tg3_str_to_str(primitive.attributes[i].key), primitive.attributes[i].value);
            if (!emplace_result.second) {
                throwWithLocation(std::string("glTF: duplicate attribute ") + tg3_str_to_str(primitive.attributes[i].key));
            }
            continue;
        }
        unsupportedAttributes.push_back(tg3_str_to_str(primitive.attributes[i].key));
    }
    if (unsupportedAttributes.size() > 0) {
        std::string msg = "glTF: primitive has unsupported attributes: ";
        for (size_t i = 0; i < unsupportedAttributes.size(); ++i) {
            if (i > 0) {
                msg += ", ";
            }
            msg += unsupportedAttributes[i];
        }
        spdlog::get("VibeGame")->warn(msg);
    }
    const auto positionIt = attributeMap.find("POSITION");
    if (positionIt == attributeMap.end()) {
        throwWithLocation("glTF: primitive without POSITION attribute");
    }
    const View positions = makeView(model, positionIt->second, "POSITION");

    // Optional attributes. SimpleSkin has no TEXCOORD_0, so absence must be
    // tolerated and defaulted rather than treated as an error.
    const auto texIt = attributeMap.find("TEXCOORD_0");
    const auto jointsIt = attributeMap.find("JOINTS_0");
    const auto weightsIt = attributeMap.find("WEIGHTS_0");

    const bool hasTex = texIt != attributeMap.end();
    const bool hasJoints = jointsIt != attributeMap.end();
    const bool hasWeights = weightsIt != attributeMap.end();

    View texCoords, joints, weights;
    if (hasTex)
        texCoords = makeView(model, texIt->second, "TEXCOORD_0");
    if (hasJoints)
        joints = makeView(model, jointsIt->second, "JOINTS_0");
    if (hasWeights)
        weights = makeView(model, weightsIt->second, "WEIGHTS_0");

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

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

LoadedModel loadModel(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const std::string extension = dot == std::string::npos ? std::string() : toLower(path.substr(dot + 1));

    if (extension == "gltf" || extension == "glb") {
        return loadGltf(path);
    }

    if (extension == "fbx") {
        throwWithLocation("FBX support is not wired in yet; add an Assimp-based importer to load " + path);
    }

    throwWithLocation("unsupported model format '" + extension + "' for " + path);
}

LoadedModel loadGltf(const std::string& path) {
    tg3_model model;
    tg3_error_stack errors;
    tg3_parse_options opts;

    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);

    tg3_error_code err = tg3_parse_file(&model, &errors, path.c_str(), 10, &opts);
    if (err != TG3_OK) {
        for (uint32_t i = 0; i < errors.count; i++) {
            spdlog::get("VibeGame")
                ->error("glTF: parse error [{}] {}", (int)errors.entries[i].severity,
                        errors.entries[i].message ? errors.entries[i].message : "(null)");
        }
        throwWithLocation("glTF: failed to parse " + path + ": " +
                          (errors.count > 0 && errors.entries[0].message ? errors.entries[0].message : "(unknown error)"));
    }

    LoadedModel result;

    // --- node hierarchy -----------------------------------------------------
    const size_t nodeCount = model.nodes_count;
    result.skeleton.parents.assign(nodeCount, -1);
    result.skeleton.localPose.resize(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i) {
        result.skeleton.localPose[i] = nodeTransform(model.nodes[i]);
        for (uint32_t j = 0; j < model.nodes[i].children_count; ++j) {
            const int32_t child = model.nodes[i].children[j];
            if (child >= 0 && static_cast<size_t>(child) < nodeCount) {
                result.skeleton.parents[static_cast<size_t>(child)] = static_cast<int>(i);
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
        throwWithLocation("glTF: no mesh found in " + path);
    }

    const tg3_node& node = model.nodes[static_cast<size_t>(meshNode)];
    const tg3_mesh& mesh = model.meshes[static_cast<size_t>(node.mesh)];
    for (const auto& primitive : mesh.primitives_count > 0
                                     ? std::vector<tg3_primitive>(mesh.primitives, mesh.primitives + mesh.primitives_count)
                                     : std::vector<tg3_primitive>()) {
        loadPrimitive(model, primitive, result.mesh);
    }

    // --- skin --------------------------------------------------------------
    if (node.skin >= 0) {
        const tg3_skin& skin = model.skins[static_cast<size_t>(node.skin)];
        result.skeleton.jointNodes = std::vector<int32_t>(skin.joints, skin.joints + skin.joints_count);
        result.skeleton.inverseBind.assign(skin.joints_count, glm::mat4(1.0f));
        // inverseBindMatrices is optional; absent means identity per joint.
        if (skin.inverse_bind_matrices >= 0) {
            const View ibm = makeView(model, skin.inverse_bind_matrices, "inverseBindMatrices");
            const size_t count = std::min(ibm.count, result.skeleton.inverseBind.size());
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
    for (const auto& animation : model.animations_count > 0
                                     ? std::vector<tg3_animation>(model.animations, model.animations + model.animations_count)
                                     : std::vector<tg3_animation>()) {
        AnimationClip clip;
        clip.name = tg3_str_to_str(animation.name);
        for (const auto& channel : animation.channels_count > 0 ? std::vector<tg3_animation_channel>(
                                                                      animation.channels, animation.channels + animation.channels_count)
                                                                : std::vector<tg3_animation_channel>()) {
            if (channel.sampler < 0 || channel.target.node < 0) {
                continue;
            }
            bool supported = false;
            const Path pathKind = parsePath(tg3_str_to_str(channel.target.path), supported);
            if (!supported) {
                continue;  // morph-target weights are out of scope
            }
            const tg3_animation_sampler& sampler = animation.samplers[static_cast<size_t>(channel.sampler)];

            Channel out;
            out.targetNode = channel.target.node;
            out.path = pathKind;
            out.track.interp = parseInterpolation(tg3_str_to_str(sampler.interpolation));

            const View input = makeView(model, sampler.input, "animation input");
            const View output = makeView(model, sampler.output, "animation output");

            out.track.times.reserve(input.count);
            for (size_t k = 0; k < input.count; ++k) {
                out.track.times.push_back(readFloat(input, k, 0));
            }
            out.track.values.reserve(output.count);
            for (size_t k = 0; k < output.count; ++k) {
                out.track.values.push_back(
                    glm::vec4(readFloat(output, k, 0), readFloat(output, k, 1), readFloat(output, k, 2), readFloat(output, k, 3)));
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
    tg3_model_free(&model);
    spdlog::logger logger = *spdlog::get("VibeGame");
    logger.debug("glTF: loaded {} vertices and {} indices from mesh node {}", result.mesh.vertices.size(), result.mesh.indices.size(),
                 meshNode);
    for (const auto& vertex : result.mesh.vertices) {
        logger.debug("mesh vertex: ({}, {}, {})", vertex.pos[0], vertex.pos[1], vertex.pos[2]);
    }
    logger.debug("glTF: loaded {} nodes, {} joints, and {} animation clips", result.skeleton.localPose.size(),
                 result.skeleton.jointNodes.size(), result.clips.size());
    tg3_error_stack_free(&errors);
    return result;
}

}  // namespace Core
