@PACKAGE_INIT@

set_and_check(derecho_INCLUDE_DIRS "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
set(derecho_LIBRARIES "-L@PACKAGE_CMAKE_INSTALL_LIBDIR@ -lderecho -pthread")
include("@PACKAGE_ConfigPackageLocation@/derechoTargets.cmake")

check_required_components(derecho)
