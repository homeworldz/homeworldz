#include "homeworldz/avatar_joints.h"

#include <algorithm>
#include <iterator>

namespace homeworldz::mesh {

bool is_riggable_joint(std::string_view name) {
    // The table is sorted, so this is a binary search rather than a scan of 407
    // entries per joint of every skin in every upload.
    return std::binary_search(std::begin(riggable_joint_names),
                              std::end(riggable_joint_names), name);
}

} // namespace homeworldz::mesh
