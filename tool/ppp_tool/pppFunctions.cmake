function(ppp_compile base outfiles_var outinclude_var)
    set(PH_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ph")
    get_filename_component(PPP_BASE "${base}" ABSOLUTE)

	# Add a custom build rule for every file
	foreach (infile ${ARGN})
		ppp_command_add("${PPP_BASE}" "${infile}" outfile outinclude)
		list(APPEND o_files ${outfile})
		list(APPEND o_includes ${outinclude})
	endforeach()

    list(APPEND o_includes ${PH_INCLUDE_DIR})

    set(${outfiles_var} ${o_files} PARENT_SCOPE)
	set(${outinclude_var} ${o_includes} PARENT_SCOPE)
endfunction()

function(ppp_command_add base infile outfile_var outinclude_var)
    set(PH_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ph")
	
    get_filename_component(PH_ABS "${infile}" ABSOLUTE)
	file(RELATIVE_PATH PH_REL "${base}" "${PH_ABS}")

	if (IS_ABSOLUTE "${PH_REL}")
		message(FATAL_ERROR "The ppp input file ${infile} was explicitely based to ${${PROJECT_NAME}_PPP_BASE} which is not a file base!")
	endif()

	get_filename_component(PH_PATH "${PH_INCLUDE_DIR}/${PH_REL}" PATH)
	get_filename_component(PH_NAME "${infile}" NAME_WE)

	# Create the output directory if it does not exist
	if (NOT EXISTS "${PH_PATH}")
		file(MAKE_DIRECTORY "${PH_PATH}")
	endif()

	get_filename_component(OUTPUT_FILE "${PH_PATH}/${PH_NAME}.h" ABSOLUTE)
	get_filename_component(PH_SRC_PATH "${infile}" DIRECTORY)

	message(STATUS "Adding compile command for '${OUTPUT_FILE}'")
	# Add the build rule
	add_custom_command(OUTPUT ${OUTPUT_FILE}
		COMMAND ppp
		ARGS "-CGV_DIR=${CGV_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${infile}" "${OUTPUT_FILE}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${infile}"
		IMPLICIT_DEPENDS ${PPP_WORK_FILES})
	set(${outfile_var} ${OUTPUT_FILE} PARENT_SCOPE)
	set(${outinclude_var} ${PH_PATH} PARENT_SCOPE)
endfunction()
