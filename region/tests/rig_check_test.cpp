#include "homeworldz/avatar_joints.h"
#include "homeworldz/rig_check.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using homeworldz::mesh::joint_rest_positions;

std::optional<std::array<float, 3>> rest_of(std::string_view name) {
    for (const auto& rest : joint_rest_positions)
        if (rest.name == name) return std::array<float, 3>{rest.x, rest.y, rest.z};
    return std::nullopt;
}

// The inverse bind matrix that places a joint at `p`. Region axes throughout:
// check_rig now reads matrices as the converted asset holds them, which
// mesh_convert has already mapped, so no frame change happens here.
//
// Identity basis, translation only: the inverse of a pure translation by -p is a
// translation by p. Column-major, as the matrices are stored.
std::array<float, 16> bind_at(const std::array<float, 3>& p) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1};
}

std::array<float, 3> offset(std::array<float, 3> p, float dx, float dy, float dz) {
    return {p[0] + dx, p[1] + dy, p[2] + dz};
}

homeworldz::mesh::JointVerdict verdict_for(const std::string& joint,
                                           const std::array<float, 3>& at) {
    const auto finding = homeworldz::mesh::check_rig({joint}, {bind_at(at)});
    return finding.joints.at(0).verdict;
}

} // namespace

int main() {
    using homeworldz::mesh::JointVerdict;
    using homeworldz::mesh::rig_match_tolerance_m;

    // mHead has no positional twin, so it is one of the joints the check can
    // actually decide.
    const auto head = rest_of("mHead");
    if (!head) return 1;
    if (verdict_for("mHead", *head) != JointVerdict::Agrees) return 2;

    // 3 mm: above the skeleton's own 2 mm left/right asymmetry and below the
    // 5 mm tolerance. A correct rig carrying ordinary authoring noise must pass,
    // which is the half of the bracket a too-tight threshold would break.
    if (verdict_for("mHead", offset(*head, 0.003f, 0, 0)) != JointVerdict::Agrees) return 3;

    // 10 mm: beyond tolerance but far below the 1.2 m a mirrored limb moves.
    // Calibrating on mirroring would have accepted this.
    if (verdict_for("mHead", offset(*head, 0.010f, 0, 0)) != JointVerdict::Disagrees) return 4;

    // mPelvis sits exactly where mSpine2 does. Landing on it is not evidence of
    // which one was meant, so the honest verdict is neither agreement nor
    // refusal.
    const auto pelvis = rest_of("mPelvis");
    if (!pelvis) return 5;
    if (verdict_for("mPelvis", *pelvis) != JointVerdict::Indiscriminate) return 6;
    {
        const auto finding = homeworldz::mesh::check_rig({"mPelvis"}, {bind_at(*pelvis)});
        // It must name the alternative rather than merely decline to judge.
        if (finding.joints.at(0).coincident_with.empty()) return 7;
        // A body rigged only to coincident joints is unproven, not wrong - and
        // equally not right. An earlier version returned "nothing disagreed"
        // here, which a caller could not tell from a body checked thoroughly.
        if (finding.outcome != homeworldz::mesh::RigOutcome::Unproven) return 8;
    }

    // The regression this file exists for. A joint with a twin, sitting 21 mm
    // from *both*, was first reported as merely indiscriminate — coincidence was
    // checked before distance, so an inability to say which joint was meant
    // suppressed the fact that neither was. Being unable to choose between
    // candidates must not excuse a rig that matches none of them.
    const auto chest = rest_of("mChest");
    if (!chest) return 9;
    if (verdict_for("mChest", *chest) != JointVerdict::Indiscriminate) return 10;
    if (verdict_for("mChest", offset(*chest, 0.021f, 0, 0)) != JointVerdict::Disagrees) return 11;

    // Sign. A mirrored rig has correct joint *spacing* and a correct-looking
    // bind pose; only comparing each joint against its own named target catches
    // it. Comparing magnitudes, or matching to the nearest joint, agrees here.
    const auto wrist_left = rest_of("mWristLeft");
    const auto wrist_right = rest_of("mWristRight");
    if (!wrist_left || !wrist_right) return 12;
    if (verdict_for("mWristLeft", *wrist_right) != JointVerdict::Disagrees) return 13;
    if (verdict_for("mWristLeft", *wrist_left) != JointVerdict::Agrees) return 14;

    // A 90 degree rotation about the up axis, which is what a glTF-exported SL
    // skeleton actually differs by (measured on the reference body, 2026-08-08).
    // Joints on the axis are unmoved by it, so a check that sampled only the
    // spine would call a rotated skeleton correct.
    {
        const std::vector<std::string> joints{"mPelvis", "mWristLeft"};
        const auto rotate = [](const std::array<float, 3>& p) {
            return std::array<float, 3>{-p[1], p[0], p[2]};
        };
        const auto finding = homeworldz::mesh::check_rig(
            joints, {bind_at(rotate(*pelvis)), bind_at(rotate(*wrist_left))});
        if (finding.outcome != homeworldz::mesh::RigOutcome::Disagrees) return 15;
        if (finding.disagreed != 1) return 16;
        // The pelvis is on the axis of rotation and lands on itself; the wrist
        // is what betrays the rotation.
        if (finding.joints.at(1).verdict != JointVerdict::Disagrees) return 17;
        if (finding.worst_joint != "mWristLeft") return 18;
    }

    // A name with no rest position on record cannot be judged, and must not be
    // silently counted as agreement.
    {
        const auto finding = homeworldz::mesh::check_rig({"mNotAJoint"}, {bind_at(*head)});
        if (finding.joints.at(0).verdict != JointVerdict::Unknown) return 19;
        if (finding.outcome != homeworldz::mesh::RigOutcome::Disagrees) return 20;
    }

    // A degenerate bind matrix has no recoverable position; saying so beats
    // reporting a position derived from a division by zero.
    {
        const std::array<float, 16> singular{0, 0, 0, 0, 0, 0, 0, 0,
                                             0, 0, 0, 0, 0, 0, 0, 1};
        const auto finding = homeworldz::mesh::check_rig({"mHead"}, {singular});
        if (finding.joints.at(0).verdict != JointVerdict::Unknown) return 21;
    }

    // Fewer matrices than joints is malformed input, not an implicit identity.
    {
        const auto finding = homeworldz::mesh::check_rig({"mHead", "mNeck"}, {bind_at(*head)});
        if (finding.joints.at(1).verdict != JointVerdict::Unknown) return 22;
        if (finding.outcome != homeworldz::mesh::RigOutcome::Disagrees) return 23;
    }

    // The tolerance must stay inside the bracket the skeleton itself sets: above
    // its 2 mm natural asymmetry, below the 7.81 mm closest distinguishable
    // pair. Asserted here so a later "just widen it slightly" has to fail a test
    // rather than quietly stop discriminating mFaceLipLowerCenter from
    // mFaceTongueBase.
    if (!(rig_match_tolerance_m > 0.002f)) return 24;
    if (!(rig_match_tolerance_m < 0.00781f)) return 25;

    // Agreement requires something to have been decided. Asserted separately
    // from the per-joint verdicts because the whole-body outcome is what a
    // caller will gate on, and it is the value that used to conflate "nothing
    // disagreed" with "this body is verified".
    {
        const auto finding = homeworldz::mesh::check_rig({"mHead"}, {bind_at(*head)});
        if (finding.outcome != homeworldz::mesh::RigOutcome::Agrees) return 26;
        if (finding.agreed != 1) return 27;
    }
    // A decided joint alongside an indiscriminate one still counts as agreement:
    // the coincident joint adds no evidence, and must subtract none either.
    {
        const auto finding = homeworldz::mesh::check_rig({"mHead", "mPelvis"},
                                                        {bind_at(*head), bind_at(*pelvis)});
        if (finding.outcome != homeworldz::mesh::RigOutcome::Agrees) return 28;
        if (finding.indiscriminate != 1) return 29;
    }

    return 0;
}
