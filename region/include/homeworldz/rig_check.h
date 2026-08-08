// Does a rigged mesh's skeleton actually stand where the Bento skeleton rests?
//
// A joint bound to the wrong target, given that target's inverse bind matrix,
// produces a correct-looking bind pose: the same wrong choice writes the
// matrices that would otherwise reveal it. So the source's own bind position is
// compared against where the skeleton rests the joint its *name* resolved to,
// with sign, before the matrices can absorb the difference.
//
// Sign matters and is the whole point. A mirrored rig - left joints carrying
// right positions - produces the correct *distances* between joints and the
// correct pose; only comparing each joint to its own named target with sign
// catches it. Comparing to the nearest joint, or comparing magnitudes, agrees
// with a mirrored skeleton.
#ifndef HOMEWORLDZ_RIG_CHECK_H
#define HOMEWORLDZ_RIG_CHECK_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace homeworldz::mesh {

// How far a joint may sit from its rest position and still be called a match.
//
// The bracket this must fall inside is a property of the skeleton, measured
// rather than chosen (client core, 2026-08-08):
//
//   2.00 mm  the natural left/right asymmetry of the skeleton's own leg chain.
//            A threshold at or below this calls a *correct* rig wrong.
//   7.81 mm  mFaceLipLowerCenter to mFaceTongueBase, the closest pair of joints
//            that are distinct at all. A threshold at or above this cannot tell
//            those two apart, so a mesh weighted to one and claiming the other
//            passes.
//
// 5 mm sits between them with room on both sides. The first calibration
// attempted here used ~1.2 m - the distance a mirrored rig displaces a hand -
// which is four orders of magnitude too coarse and would have accepted almost
// any skeleton whose overall proportions were human.
inline constexpr float rig_match_tolerance_m = 0.005f;

// Two joints closer together than this cannot be told apart by position at all,
// so a mesh weighted to either is geometrically identical. The skeleton has 26
// such pairs sitting at *exactly* zero distance - mChest and mSpine3 share a
// position to the micrometre, as do each eye and its three face-joint
// neighbours - covering 27 of the 159 joints. For those, "agrees" would be a
// statement the measurement cannot support, so the check reports that it cannot
// discriminate and names the alternatives instead of guessing.
inline constexpr float rig_coincidence_m = 0.0005f;

enum class JointVerdict {
    Agrees,          // within tolerance of the joint its name resolved to
    Disagrees,       // outside tolerance: the name and the position disagree
    Indiscriminate,  // coincident with another joint; position proves nothing
    Unknown,         // no rest position on record for this name
};

struct JointFinding {
    std::string name;
    JointVerdict verdict{JointVerdict::Unknown};
    float distance_m{};                  // from the *named* target, not the nearest
    std::array<float, 3> expected{};
    std::array<float, 3> observed{};
    std::vector<std::string> coincident_with;
};

struct RigFinding {
    // True only when every joint either agrees or is one the check honestly
    // cannot discriminate. A single disagreement is a refusal.
    bool agrees{};
    std::uint32_t agreed{};
    std::uint32_t disagreed{};
    std::uint32_t indiscriminate{};
    std::uint32_t unknown{};
    float worst_distance_m{};
    std::string worst_joint;
    std::vector<JointFinding> joints;
};

// `inverse_bind` are the glTF inverse bind matrices, column-major and in the
// glTF frame; this applies the same axis map the geometry gets, so the
// comparison happens in region axes on both sides.
RigFinding check_rig(const std::vector<std::string>& joints,
                     const std::vector<std::array<float, 16>>& inverse_bind);

// One line per joint, for the diagnostic tool and refusal messages.
std::string describe(const RigFinding& finding);

} // namespace homeworldz::mesh

#endif
