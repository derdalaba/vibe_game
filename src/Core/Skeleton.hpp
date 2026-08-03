#pragma once

#include <vector>

#include "Transform.hpp"

namespace Core {

// The glTF node hierarchy, plus the skin's joint list and inverse bind
// matrices. Joints are simply nodes designated as such by skin.joints, so the
// hierarchy is stored per-node and `jointNodes` maps joint index -> node index.
//
// JOINTS_0 vertex attributes index into `jointNodes` (i.e. joint index), not
// into the node array, so no remapping is needed at draw time.
struct Skeleton {
    std::vector<int> parents;          // per node; -1 for roots
    std::vector<Transform> localPose;  // per node; mutated by animation
    std::vector<Transform> restPose;   // per node; as loaded
    std::vector<int> jointNodes;       // joint index -> node index
    std::vector<glm::mat4> inverseBind;  // per joint; identity if absent

    bool isSkinned() const { return !jointNodes.empty(); }
    size_t nodeCount() const { return parents.size(); }
    size_t jointCount() const { return jointNodes.size(); }
};

// Resolves every node's global transform by walking parents.
void computeGlobalTransforms(const Skeleton& skeleton,
                             std::vector<glm::mat4>& out);

// Per the glTF spec this is the two-term form:
//     jointMatrix[i] = globalTransform(jointNodes[i]) * inverseBind[i]
//
// The skinned mesh node's own transform MUST be ignored (spec), which is why
// there is no leading inverse() term here. The renderer supplies its own
// object-placement matrix separately via push constants.
void computeJointMatrices(const Skeleton& skeleton,
                          std::vector<glm::mat4>& out);

}  // namespace Core
