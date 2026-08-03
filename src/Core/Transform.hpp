#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Core {

// A TRS transform. glTF mandates that the local matrix is composed as
// T * R * S, i.e. scale is applied to the vertices first, then rotation,
// then translation.
struct Transform {
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // glm order is (w, x, y, z)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 toMatrix() const {
        return glm::translate(glm::mat4(1.0f), translation) *
               glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }
};

}  // namespace Core
