
file(GLOB_RECURSE SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

cgv_create_lib(cgv_math CORE_LIB SOURCES ${SOURCES})
