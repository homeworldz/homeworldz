// Every joint name a rigged mesh may bind to, as Firestorm resolves them.
//
// Generated from the viewer's own data on 2026-08-08 — `avatar_skeleton.xml`
// (133 bones, 26 collision volumes) and the attachment points in
// `avatar_lad.xml` — rather than transcribed, because a transcribed list is a
// second copy of a fact that can drift from the first.
//
// **The accept-set is not the joint count.** The skeleton has
// 133+26=159 joints, which is what `rigged_skeleton_joints` publishes, but a
// viewer resolves 407 distinct names onto them:
//
//   - 133 bone names, and 153 aliases declared on those bones.
//     LLAvatarAppearance::makeJointAliases maps each bone's name to itself and
//     every space-separated token of its `aliases` attribute.
//   - 26 collision volumes. Not in the alias map — makeJointAliases skips
//     anything that is not a joint — but LLVOAvatar::getJoint falls through to
//     findJoint, which walks the whole tree, so they resolve anyway.
//   - 55 attachment points, plus 40 spaceless variants. getJointAliases adds
//     the underscore forms itself, "to give a mechanism for referencing such
//     joints in daes, which don't allow spaces".
//
// Validating against the 159 canonical names alone would refuse rigs the
// viewer accepts, and not rare ones: the aliases include `hip`, `abdomen` and
// `avatar_mPelvis`, which is what Blender and Avastar pipelines emit.
#ifndef HOMEWORLDZ_AVATAR_JOINTS_H
#define HOMEWORLDZ_AVATAR_JOINTS_H

#include <string_view>

namespace homeworldz::mesh {

// Sorted, so a lookup may binary-search and a reader may diff two versions.
inline constexpr std::string_view riggable_joint_names[] = {
    "Alt Left Ear", "Alt Left Eye", "Alt Right Ear",
    "Alt Right Eye", "Alt_Left_Ear", "Alt_Left_Eye",
    "Alt_Right_Ear", "Alt_Right_Eye", "Avatar Center",
    "Avatar_Center", "BELLY", "BUTT",
    "Bottom", "Bottom Left", "Bottom Right",
    "Bottom_Left", "Bottom_Right", "CHEST",
    "Center", "Center 2", "Center_2",
    "Chest", "Chin", "Groin",
    "HEAD", "Jaw", "L Forearm",
    "L Lower Leg", "L Upper Arm", "L Upper Leg",
    "LEFT_HANDLE", "LEFT_PEC", "LOWER_BACK",
    "L_CLAVICLE", "L_FOOT", "L_Forearm",
    "L_HAND", "L_LOWER_ARM", "L_LOWER_LEG",
    "L_Lower_Leg", "L_UPPER_ARM", "L_UPPER_LEG",
    "L_Upper_Arm", "L_Upper_Leg", "Left Ear",
    "Left Eyeball", "Left Foot", "Left Hand",
    "Left Hind Foot", "Left Hip", "Left Pec",
    "Left Ring Finger", "Left Shoulder", "Left Wing",
    "Left_Ear", "Left_Eyeball", "Left_Foot",
    "Left_Hand", "Left_Hind_Foot", "Left_Hip",
    "Left_Pec", "Left_Ring_Finger", "Left_Shoulder",
    "Left_Wing", "Mouth", "NECK",
    "Neck", "Nose", "PELVIS",
    "Pelvis", "R Forearm", "R Lower Leg",
    "R Upper Arm", "R Upper Leg", "RIGHT_HANDLE",
    "RIGHT_PEC", "R_CLAVICLE", "R_FOOT",
    "R_Forearm", "R_HAND", "R_LOWER_ARM",
    "R_LOWER_LEG", "R_Lower_Leg", "R_UPPER_ARM",
    "R_UPPER_LEG", "R_Upper_Arm", "R_Upper_Leg",
    "Right Ear", "Right Eyeball", "Right Foot",
    "Right Hand", "Right Hind Foot", "Right Hip",
    "Right Pec", "Right Ring Finger", "Right Shoulder",
    "Right Wing", "Right_Ear", "Right_Eyeball",
    "Right_Foot", "Right_Hand", "Right_Hind_Foot",
    "Right_Hip", "Right_Pec", "Right_Ring_Finger",
    "Right_Shoulder", "Right_Wing", "Skull",
    "Spine", "Stomach", "Tail Base",
    "Tail Tip", "Tail_Base", "Tail_Tip",
    "Tongue", "Top", "Top Left",
    "Top Right", "Top_Left", "Top_Right",
    "UPPER_BACK", "abdomen", "avatar_mAnkleLeft",
    "avatar_mAnkleRight", "avatar_mChest", "avatar_mCollarLeft",
    "avatar_mCollarRight", "avatar_mElbowLeft", "avatar_mElbowRight",
    "avatar_mEyeLeft", "avatar_mEyeRight", "avatar_mFaceCheekLowerLeft",
    "avatar_mFaceCheekLowerRight", "avatar_mFaceCheekUpperLeft", "avatar_mFaceCheekUpperRight",
    "avatar_mFaceChin", "avatar_mFaceEar1Left", "avatar_mFaceEar1Right",
    "avatar_mFaceEar2Left", "avatar_mFaceEar2Right", "avatar_mFaceEyeAltLeft",
    "avatar_mFaceEyeAltRight", "avatar_mFaceEyeLidLowerLeft", "avatar_mFaceEyeLidLowerRight",
    "avatar_mFaceEyeLidUpperLeft", "avatar_mFaceEyeLidUpperRight", "avatar_mFaceEyebrowCenterLeft",
    "avatar_mFaceEyebrowCenterRight", "avatar_mFaceEyebrowInnerLeft", "avatar_mFaceEyebrowInnerRight",
    "avatar_mFaceEyebrowOuterLeft", "avatar_mFaceEyebrowOuterRight", "avatar_mFaceEyecornerInnerLeft",
    "avatar_mFaceEyecornerInnerRight", "avatar_mFaceForeheadCenter", "avatar_mFaceForeheadLeft",
    "avatar_mFaceForeheadRight", "avatar_mFaceJaw", "avatar_mFaceJawShaper",
    "avatar_mFaceLipCornerLeft", "avatar_mFaceLipCornerRight", "avatar_mFaceLipLowerCenter",
    "avatar_mFaceLipLowerLeft", "avatar_mFaceLipLowerRight", "avatar_mFaceLipUpperCenter",
    "avatar_mFaceLipUpperLeft", "avatar_mFaceLipUpperRight", "avatar_mFaceNoseBase",
    "avatar_mFaceNoseBridge", "avatar_mFaceNoseCenter", "avatar_mFaceNoseLeft",
    "avatar_mFaceNoseRight", "avatar_mFaceRoot", "avatar_mFaceTeethLower",
    "avatar_mFaceTeethUpper", "avatar_mFaceTongueBase", "avatar_mFaceTongueTip",
    "avatar_mFootLeft", "avatar_mFootRight", "avatar_mGroin",
    "avatar_mHandIndex1Left", "avatar_mHandIndex1Right", "avatar_mHandIndex2Left",
    "avatar_mHandIndex2Right", "avatar_mHandIndex3Left", "avatar_mHandIndex3Right",
    "avatar_mHandMiddle1Left", "avatar_mHandMiddle1Right", "avatar_mHandMiddle2Left",
    "avatar_mHandMiddle2Right", "avatar_mHandMiddle3Left", "avatar_mHandMiddle3Right",
    "avatar_mHandPinky1Left", "avatar_mHandPinky1Right", "avatar_mHandPinky2Left",
    "avatar_mHandPinky2Right", "avatar_mHandPinky3Left", "avatar_mHandPinky3Right",
    "avatar_mHandRing1Left", "avatar_mHandRing1Right", "avatar_mHandRing2Left",
    "avatar_mHandRing2Right", "avatar_mHandRing3Left", "avatar_mHandRing3Right",
    "avatar_mHandThumb1Left", "avatar_mHandThumb1Right", "avatar_mHandThumb2Left",
    "avatar_mHandThumb2Right", "avatar_mHandThumb3Left", "avatar_mHandThumb3Right",
    "avatar_mHead", "avatar_mHindLimb1Left", "avatar_mHindLimb1Right",
    "avatar_mHindLimb2Left", "avatar_mHindLimb2Right", "avatar_mHindLimb3Left",
    "avatar_mHindLimb3Right", "avatar_mHindLimb4Left", "avatar_mHindLimb4Right",
    "avatar_mHindLimbsRoot", "avatar_mHipLeft", "avatar_mHipRight",
    "avatar_mKneeLeft", "avatar_mKneeRight", "avatar_mNeck",
    "avatar_mPelvis", "avatar_mShoulderLeft", "avatar_mShoulderRight",
    "avatar_mSkull", "avatar_mSpine1", "avatar_mSpine2",
    "avatar_mSpine3", "avatar_mSpine4", "avatar_mTail1",
    "avatar_mTail2", "avatar_mTail3", "avatar_mTail4",
    "avatar_mTail5", "avatar_mTail6", "avatar_mToeLeft",
    "avatar_mToeRight", "avatar_mTorso", "avatar_mWing1Left",
    "avatar_mWing1Right", "avatar_mWing2Left", "avatar_mWing2Right",
    "avatar_mWing3Left", "avatar_mWing3Right", "avatar_mWing4FanLeft",
    "avatar_mWing4FanRight", "avatar_mWing4Left", "avatar_mWing4Right",
    "avatar_mWingsRoot", "avatar_mWristLeft", "avatar_mWristRight",
    "chest", "figureHair", "head",
    "hip", "lCollar", "lFoot",
    "lForeArm", "lHand", "lShin",
    "lShldr", "lThigh", "mAnkleLeft",
    "mAnkleRight", "mChest", "mCollarLeft",
    "mCollarRight", "mElbowLeft", "mElbowRight",
    "mEyeLeft", "mEyeRight", "mFaceCheekLowerLeft",
    "mFaceCheekLowerRight", "mFaceCheekUpperLeft", "mFaceCheekUpperRight",
    "mFaceChin", "mFaceEar1Left", "mFaceEar1Right",
    "mFaceEar2Left", "mFaceEar2Right", "mFaceEyeAltLeft",
    "mFaceEyeAltRight", "mFaceEyeLidLowerLeft", "mFaceEyeLidLowerRight",
    "mFaceEyeLidUpperLeft", "mFaceEyeLidUpperRight", "mFaceEyebrowCenterLeft",
    "mFaceEyebrowCenterRight", "mFaceEyebrowInnerLeft", "mFaceEyebrowInnerRight",
    "mFaceEyebrowOuterLeft", "mFaceEyebrowOuterRight", "mFaceEyecornerInnerLeft",
    "mFaceEyecornerInnerRight", "mFaceForeheadCenter", "mFaceForeheadLeft",
    "mFaceForeheadRight", "mFaceJaw", "mFaceJawShaper",
    "mFaceLipCornerLeft", "mFaceLipCornerRight", "mFaceLipLowerCenter",
    "mFaceLipLowerLeft", "mFaceLipLowerRight", "mFaceLipUpperCenter",
    "mFaceLipUpperLeft", "mFaceLipUpperRight", "mFaceNoseBase",
    "mFaceNoseBridge", "mFaceNoseCenter", "mFaceNoseLeft",
    "mFaceNoseRight", "mFaceRoot", "mFaceTeethLower",
    "mFaceTeethUpper", "mFaceTongueBase", "mFaceTongueTip",
    "mFootLeft", "mFootRight", "mGroin",
    "mHandIndex1Left", "mHandIndex1Right", "mHandIndex2Left",
    "mHandIndex2Right", "mHandIndex3Left", "mHandIndex3Right",
    "mHandMiddle1Left", "mHandMiddle1Right", "mHandMiddle2Left",
    "mHandMiddle2Right", "mHandMiddle3Left", "mHandMiddle3Right",
    "mHandPinky1Left", "mHandPinky1Right", "mHandPinky2Left",
    "mHandPinky2Right", "mHandPinky3Left", "mHandPinky3Right",
    "mHandRing1Left", "mHandRing1Right", "mHandRing2Left",
    "mHandRing2Right", "mHandRing3Left", "mHandRing3Right",
    "mHandThumb1Left", "mHandThumb1Right", "mHandThumb2Left",
    "mHandThumb2Right", "mHandThumb3Left", "mHandThumb3Right",
    "mHead", "mHindLimb1Left", "mHindLimb1Right",
    "mHindLimb2Left", "mHindLimb2Right", "mHindLimb3Left",
    "mHindLimb3Right", "mHindLimb4Left", "mHindLimb4Right",
    "mHindLimbsRoot", "mHipLeft", "mHipRight",
    "mKneeLeft", "mKneeRight", "mNeck",
    "mPelvis", "mShoulderLeft", "mShoulderRight",
    "mSkull", "mSpine1", "mSpine2",
    "mSpine3", "mSpine4", "mTail1",
    "mTail2", "mTail3", "mTail4",
    "mTail5", "mTail6", "mToeLeft",
    "mToeRight", "mTorso", "mWing1Left",
    "mWing1Right", "mWing2Left", "mWing2Right",
    "mWing3Left", "mWing3Right", "mWing4FanLeft",
    "mWing4FanRight", "mWing4Left", "mWing4Right",
    "mWingsRoot", "mWristLeft", "mWristRight",
    "neck", "rCollar", "rFoot",
    "rForeArm", "rHand", "rShin",
    "rShldr", "rThigh",
};

// True when a skin may bind to this name.
bool is_riggable_joint(std::string_view name);

} // namespace homeworldz::mesh

#endif
