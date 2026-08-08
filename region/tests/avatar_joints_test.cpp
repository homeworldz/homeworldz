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
    return 0;
}
