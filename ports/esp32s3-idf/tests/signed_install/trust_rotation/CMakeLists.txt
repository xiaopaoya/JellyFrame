set(rotation_target jfsg_rotation_${rotation})
jfsg_add_rotation_target(${rotation_target} "${store}" "${adapter}")
jfsg_configure_package_trust(${rotation_target} "${${rotation}_manifest}")
