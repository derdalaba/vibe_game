#pragma once

#include <string>
#include <vector>

#include "Transform.hpp"

namespace Core {

enum class Interpolation { Linear, Step, CubicSpline };

enum class Path { Translation, Rotation, Scale };

// A keyframe track. `values` is vec4 for every path so glTF's TRS paths and
// quaternions share one container: translation/scale use xyz, rotation uses
// xyzw in glTF order (NOT glm's wxyz constructor order).
//
// For CubicSpline, glTF stores three elements per keyframe -- in-tangent,
// value, out-tangent -- so `values` has 3x the entries of `times` and the
// value for keyframe k lives at index 3*k + 1.
struct Track {
    std::vector<float> times;
    std::vector<glm::vec4> values;
    Interpolation interp = Interpolation::Linear;
};

struct Channel {
    int targetNode = -1;
    Path path = Path::Translation;
    Track track;
};

struct AnimationClip {
    std::string name;
    std::vector<Channel> channels;
    float duration = 0.0f;

    // Hand-authored keyframe building: appends a key to the track for
    // (targetNode, path), creating the channel on first use. Keys must be
    // added in non-decreasing time order, matching glTF's requirement that
    // timestamps strictly increase.
    void addKey(int targetNode, Path path, float time, const glm::vec4& value,
                Interpolation interp = Interpolation::Linear);
};

// Samples every channel of `clip` at `time` and writes the results into
// `nodes`. Only the TRS components actually targeted by a channel are
// modified, so untouched nodes keep their rest pose. `time` is clamped to the
// clip range -- looping is the caller's decision, since glTF deliberately
// defines no runtime playback behaviour.
void sample(const AnimationClip& clip, float time,
            std::vector<Transform>& nodes);

}  // namespace Core
