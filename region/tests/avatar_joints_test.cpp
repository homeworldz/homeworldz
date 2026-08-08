// The set of joint names a rigged mesh may bind to.
//
// Generated from Firestorm's own avatar_skeleton.xml and avatar_lad.xml. These
// assertions exist because the generated list is the thing a validator refuses
// against: silently losing a name refuses content the viewer accepts, and
// silently gaining one accepts content it rejects. Neither shows up until real
// content arrives.

#include "homeworldz/avatar_joints.h"
#include "homeworldz/mesh_acceptance.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <string_view>

int main() {
    const auto count = std::size(homeworldz::mesh::riggable_joint_names);

    // 133 bones + 26 collision volumes + 153 bone aliases + 55 attachment
    // points + 40 spaceless variants, deduplicated.
    assert(count == 407);

    // Sorted and unique: is_riggable_joint binary-searches, which silently
    // returns wrong answers on an unsorted table rather than failing.
    assert(std::is_sorted(std::begin(homeworldz::mesh::riggable_joint_names),
                          std::end(homeworldz::mesh::riggable_joint_names)));
    assert(std::adjacent_find(std::begin(homeworldz::mesh::riggable_joint_names),
                              std::end(homeworldz::mesh::riggable_joint_names)) ==
           std::end(homeworldz::mesh::riggable_joint_names));

    // A canonical bone, a Bento bone, and a collision volume. The Bento entry
    // is the one that would vanish if the list were ever regenerated from the
    // pre-Bento skeleton, which is the mistake this whole area already made
    // once by publishing 71 joints.
    assert(homeworldz::mesh::is_riggable_joint("mPelvis"));
    assert(homeworldz::mesh::is_riggable_joint("mWingsRoot"));
    assert(homeworldz::mesh::is_riggable_joint("PELVIS"));

    // Aliases. Refusing these would reject what Blender and Avastar pipelines
    // actually emit, while the published policy claimed compatibility.
    assert(homeworldz::mesh::is_riggable_joint("hip"));
    assert(homeworldz::mesh::is_riggable_joint("abdomen"));
    assert(homeworldz::mesh::is_riggable_joint("avatar_mPelvis"));

    // Attachment points, and the underscore forms the viewer adds itself so
    // that formats which disallow spaces can name them.
    assert(homeworldz::mesh::is_riggable_joint("Chest"));
    assert(homeworldz::mesh::is_riggable_joint("Left Shoulder"));
    assert(homeworldz::mesh::is_riggable_joint("Left_Shoulder"));

    // Not joints: a plausible-looking invention, and a name from a different
    // skeleton convention.
    assert(!homeworldz::mesh::is_riggable_joint("mNotAJoint"));
    assert(!homeworldz::mesh::is_riggable_joint("Bip01_Spine"));
    assert(!homeworldz::mesh::is_riggable_joint(""));

    // The published joint count describes the skeleton, not the accept-set.
    // Conflating them is what a reader of the policy would most easily do.
    assert(homeworldz::mesh::rigged_skeleton_joints == 159);
    assert(count > homeworldz::mesh::rigged_skeleton_joints);

    // Aliases resolve to the joint the skeleton file declares, and canonical
    // names resolve to themselves. Nothing is guessed: a name that is neither
    // resolves to empty, so the caller refuses instead of picking a near match.
    assert(homeworldz::mesh::canonical_joint("hip") == "mPelvis");
    assert(homeworldz::mesh::canonical_joint("abdomen") == "mTorso");
    assert(homeworldz::mesh::canonical_joint("mWristLeft") == "mWristLeft");
    assert(homeworldz::mesh::canonical_joint("pelvis.L").empty());
    assert(homeworldz::mesh::canonical_joint("CC_Base_Hip").empty());

    // Rest positions exist for bones and collision volumes. Attachment points
    // are legal rig targets but are not bones, so they resolve without one --
    // a caller must handle "resolved, no position" rather than assume.
    float x = 0, y = 0, z = 0;
    assert(homeworldz::mesh::joint_rest("mPelvis", x, y, z));
    assert(!homeworldz::mesh::joint_rest("Left Shoulder", x, y, z));

    // The mirror trap, stated as data rather than prose. A left wrist and a
    // right wrist sit the same distance from the pelvis and differ only in the
    // sign of y, so any check comparing *distance* calls a perfect mirror a
    // perfect match. This asserts the property that makes the naive check
    // wrong, so nobody reintroduces it believing the geometry forgives it.
    float lx = 0, ly = 0, lz = 0, rx = 0, ry = 0, rz = 0;
    assert(homeworldz::mesh::joint_rest("mWristLeft", lx, ly, lz));
    assert(homeworldz::mesh::joint_rest("mWristRight", rx, ry, rz));
    const auto close = [](float a, float b) { return std::fabs(a - b) < 1e-4F; };
    assert(close(lx, rx) && close(lz, rz));   // same but for the lateral axis
    assert(close(ly, -ry) && std::fabs(ly) > 0.1F);  // mirrored, and not near zero
    // The distance from the pelvis is identical, which is the whole trap.
    float px = 0, py = 0, pz = 0;
    assert(homeworldz::mesh::joint_rest("mPelvis", px, py, pz));
    const auto span = [&](float ax, float ay, float az) {
        return std::sqrt((ax - px) * (ax - px) + (ay - py) * (ay - py) + (az - pz) * (az - pz));
    };
    assert(std::fabs(span(lx, ly, lz) - span(rx, ry, rz)) < 1e-4F);

    // A parent standing in for its child is the other failure, and it differs
    // from the mirror in magnitude rather than in sign -- so one threshold has
    // to catch both. mWristLeft against its parent mElbowLeft is the short end
    // of that range, which is what makes it the case worth measuring.
    float ex = 0, ey = 0, ez = 0;
    assert(homeworldz::mesh::joint_rest("mElbowLeft", ex, ey, ez));
    const auto elbow_to_wrist = std::sqrt((lx - ex) * (lx - ex) + (ly - ey) * (ly - ey) +
                                          (lz - ez) * (lz - ez));
    assert(elbow_to_wrist > 0.05F);
    // Both faults are larger than a limb segment, so a threshold set from the
    // skeleton's own scale separates them from the genuine slack between a
    // source skeleton's proportions and this one.
    assert(std::fabs(ly - ry) > elbow_to_wrist);

    return 0;
}
