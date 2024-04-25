# ##############################################################
# A Logan Thomas Barnes project
# ##############################################################

### System Packages ###
find_package(Vulkan REQUIRED)

### External Repositories ###
cpmaddpackage("gh:gabime/spdlog@1.13.0")
cpmaddpackage(
  NAME
  GLFW
  GITHUB_REPOSITORY
  glfw/glfw
  GIT_TAG
  3.3.9
  OPTIONS
  "GLFW_BUILD_TESTS OFF"
  "GLFW_BUILD_EXAMPLES OFF"
  "GLFW_BUILD_DOCS OFF"
)

if (spdlog_ADDED)
  # Mark external include directories as system includes to avoid warnings.
  target_include_directories(
    spdlog
    SYSTEM
    INTERFACE
    $<BUILD_INTERFACE:${spdlog_SOURCE_DIR}/include>
  )
endif ()

if (GLFW_ADDED)
  add_library(
    glfw::glfw
    ALIAS
    glfw
  )
endif ()
