#include "Skeleton.hpp"

namespace Core {
namespace {

// glTF does not guarantee that parents appear before children in the node
// array, so resolve recursively with memoisation rather than a single pass.
const glm::mat4& resolveGlobal(const Skeleton& skeleton, int node,
                               std::vector<glm::mat4>& global,
                               std::vector<char>& resolved) {
    const size_t index = static_cast<size_t>(node);
    if (!resolved[index]) {
        resolved[index] = 1;  // set first: guards against a malformed cycle
        const glm::mat4 local = skeleton.localPose[index].toMatrix();
        const int parent = skeleton.parents[index];
        global[index] =
            parent >= 0
                ? resolveGlobal(skeleton, parent, global, resolved) * local
                : local;
    }
    return global[index];
}

}  // namespace

void computeGlobalTransforms(const Skeleton& skeleton,
                             std::vector<glm::mat4>& out) {
    const size_t nodes = skeleton.nodeCount();
    out.assign(nodes, glm::mat4(1.0f));
    std::vector<char> resolved(nodes, 0);
    for (size_t i = 0; i < nodes; ++i) {
        resolveGlobal(skeleton, static_cast<int>(i), out, resolved);
    }
}

void computeJointMatrices(const Skeleton& skeleton,
                          std::vector<glm::mat4>& out) {
    std::vector<glm::mat4> global;
    computeGlobalTransforms(skeleton, global);

    const size_t joints = skeleton.jointCount();
    out.resize(joints);
    for (size_t i = 0; i < joints; ++i) {
        const int node = skeleton.jointNodes[i];
        const glm::mat4 nodeGlobal =
            (node >= 0 && static_cast<size_t>(node) < global.size())
                ? global[static_cast<size_t>(node)]
                : glm::mat4(1.0f);
        const glm::mat4 ibm =
            i < skeleton.inverseBind.size() ? skeleton.inverseBind[i]
                                            : glm::mat4(1.0f);
        out[i] = nodeGlobal * ibm;
    }
}

}  // namespace Core
