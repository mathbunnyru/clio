include("${CMAKE_CURRENT_LIST_DIR}/ClioVersion.cmake")

set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/clio")
set(CPACK_PACKAGE_VERSION "${CLIO_VERSION}")

include(pkg/deb)
include(CPack)
