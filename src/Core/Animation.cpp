#include "Animation.hpp"

#include <algorithm>

namespace Core {
namespace {

// CubicSpline stores [inTangent, value, outTangent] per keyframe.
glm::vec4 keyValue(const Track& track, size_t k) {
    if (track.interp == Interpolation::CubicSpline) {
        size_t index = 3 * k + 1;
        return index < track.values.size() ? track.values[index]
                                           : glm::vec4(0.0f);
    }
    return k < track.values.size() ? track.values[k] : glm::vec4(0.0f);
}

glm::quat toQuat(const glm::vec4& v) {
    // glTF stores quaternions xyzw; glm's constructor takes (w, x, y, z).
    return glm::quat(v.w, v.x, v.y, v.z);
}

glm::vec4 fromQuat(const glm::quat& q) {
    return glm::vec4(q.x, q.y, q.z, q.w);
}

glm::vec4 sampleTrack(const Track& track, float time, bool isRotation) {
    if (track.times.empty()) {
        return glm::vec4(0.0f);
    }
    if (track.times.size() == 1 || time <= track.times.front()) {
        return keyValue(track, 0);
    }
    if (time >= track.times.back()) {
        return keyValue(track, track.times.size() - 1);
    }

    // times[k] <= time < times[k + 1]
    size_t k = static_cast<size_t>(
                   std::upper_bound(track.times.begin(), track.times.end(),
                                    time) -
                   track.times.begin()) -
               1;

    if (track.interp == Interpolation::Step) {
        return keyValue(track, k);
    }

    const glm::vec4 a = keyValue(track, k);
    const glm::vec4 b = keyValue(track, k + 1);
    const float span = track.times[k + 1] - track.times[k];
    const float t = span > 0.0f ? (time - track.times[k]) / span : 0.0f;

    // NOTE: CubicSpline is approximated as linear between the keyframe values,
    // ignoring the stored tangents. Visually smoother-than-step but not
    // spec-exact; upgrade here if an asset needs true spline interpolation.
    if (isRotation) {
        return fromQuat(glm::normalize(glm::slerp(toQuat(a), toQuat(b), t)));
    }
    return glm::mix(a, b, t);
}

}  // namespace

void AnimationClip::addKey(int targetNode, Path path, float time,
                           const glm::vec4& value, Interpolation interp) {
    Channel* channel = nullptr;
    for (auto& existing : channels) {
        if (existing.targetNode == targetNode && existing.path == path) {
            channel = &existing;
            break;
        }
    }
    if (channel == nullptr) {
        channels.push_back(Channel{targetNode, path, Track{}});
        channel = &channels.back();
        channel->track.interp = interp;
    }

    channel->track.times.push_back(time);
    channel->track.values.push_back(value);
    duration = std::max(duration, time);
}

void sample(const AnimationClip& clip, float time,
            std::vector<Transform>& nodes) {
    for (const auto& channel : clip.channels) {
        if (channel.targetNode < 0 ||
            static_cast<size_t>(channel.targetNode) >= nodes.size()) {
            continue;  // channel may target an extension-defined object
        }
        const bool isRotation = channel.path == Path::Rotation;
        const glm::vec4 value = sampleTrack(channel.track, time, isRotation);
        Transform& node = nodes[static_cast<size_t>(channel.targetNode)];

        switch (channel.path) {
            case Path::Translation:
                node.translation = glm::vec3(value);
                break;
            case Path::Rotation:
                node.rotation = toQuat(value);
                break;
            case Path::Scale:
                node.scale = glm::vec3(value);
                break;
        }
    }
}

}  // namespace Core
