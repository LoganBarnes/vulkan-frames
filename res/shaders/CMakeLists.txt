# ##############################################################################
# A Logan Thomas Barnes project
# ##############################################################################
file(GLOB_RECURSE LtbVlk_SHADERS
  LIST_DIRECTORIES false
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.vert
  ${CMAKE_CURRENT_LIST_DIR}/*.tesc
  ${CMAKE_CURRENT_LIST_DIR}/*.tese
  ${CMAKE_CURRENT_LIST_DIR}/*.geom
  ${CMAKE_CURRENT_LIST_DIR}/*.frag
  ${CMAKE_CURRENT_LIST_DIR}/*.comp
  ${CMAKE_CURRENT_LIST_DIR}/*.task
  ${CMAKE_CURRENT_LIST_DIR}/*.mesh
)
file(GLOB_RECURSE LtbVlk_SHADER_INCLUDES
  LIST_DIRECTORIES false
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.glsl
)

add_library(ltb_vst_generate_spirv INTERFACE)
add_library(LtbVlk::generate_spirv ALIAS ltb_vst_generate_spirv)

file(MAKE_DIRECTORY ${LTB_VLK_GENERATED_DIR}/shaders)

# Generate the SPIR-V file for each shader
foreach (SHADER_FILE ${LtbVlk_SHADERS})
  get_filename_component(SHADER_FILE_NAME ${SHADER_FILE} NAME)
  set(SPIRV_FILE ${LTB_VLK_GENERATED_DIR}/shaders/${SHADER_FILE_NAME}.spv)

  if (LTB_DRIVEOS_DEVICE)
    add_custom_command(
      OUTPUT ${SPIRV_FILE}
      COMMAND ${Vulkan_GLSLC_EXECUTABLE}
      ARGS -vk -chip ga10b -binary ${SPIRV_FILE} ${SHADER_FILE}
      DEPENDS ${SHADER_FILE} ${LtbVlk_SHADER_INCLUDES}
      VERBATIM
    )
  else ()
    add_custom_command(
      OUTPUT ${SPIRV_FILE}
      COMMAND ${Vulkan_GLSLC_EXECUTABLE}
      ARGS ${SHADER_FILE} -o ${SPIRV_FILE}
      DEPENDS ${SHADER_FILE} ${LtbVlk_SHADER_INCLUDES}
      VERBATIM
    )
  endif ()

  set_source_files_properties(${SPIRV_FILE} PROPERTIES GENERATED TRUE)
  target_sources(ltb_vst_generate_spirv PRIVATE ${SPIRV_FILE})
endforeach ()
