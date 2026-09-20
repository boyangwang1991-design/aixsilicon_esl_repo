include_guard(GLOBAL)
function(esl_model name package)
    cmake_parse_arguments(M "" "" "LIBRARIES;PACKAGES" ${ARGN})
    set(_esl_cmake "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    include("${_esl_cmake}/ESLSystemC.cmake")
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    add_library(aix_esl_${name} STATIC systemc/src/model.cpp)
    add_library(aix::esl::${name} ALIAS aix_esl_${name})
    set_target_properties(aix_esl_${name} PROPERTIES EXPORT_NAME ${name} POSITION_INDEPENDENT_CODE ON)
    target_compile_features(aix_esl_${name} PUBLIC cxx_std_${ESL_CXX_STANDARD})
    target_include_directories(aix_esl_${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/systemc/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE "${_esl_cmake}/../common/systemc/include")
    target_link_libraries(aix_esl_${name} PUBLIC SystemC::systemc ${M_LIBRARIES})
    set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${package}")
    set(ESL_PACKAGE ${package})
    set(ESL_DEPENDENCIES "")
    foreach(dependency IN LISTS M_PACKAGES)
        string(APPEND ESL_DEPENDENCIES "find_dependency(${dependency} 0.1 CONFIG)\n")
    endforeach()
    configure_package_config_file("${_esl_cmake}/ESLModelConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${package}Config.cmake" INSTALL_DESTINATION "${_package_dir}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/${package}ConfigVersion.cmake"
        VERSION 0.1.0 COMPATIBILITY SameMinorVersion)
    install(TARGETS aix_esl_${name} EXPORT ${package}Targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY systemc/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT ${package}Targets NAMESPACE aix::esl:: DESTINATION "${_package_dir}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${package}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${package}ConfigVersion.cmake" DESTINATION "${_package_dir}")
    install(FILES "${_esl_cmake}/ESLSystemC.cmake" DESTINATION "${_package_dir}/cmake")
    install(FILES "${_esl_cmake}/../contracts/environment.json" DESTINATION "${_package_dir}/contracts")
    # Preserve repository-relative documentation links in the installed bundle.
    set(_docs_root "${CMAKE_INSTALL_DATADIR}/aix-esl")
    install(FILES model.yaml README.md DESTINATION ${_docs_root}/models/${name})
    install(DIRECTORY docs/ DESTINATION ${_docs_root}/models/${name}/docs)
    install(DIRECTORY "${_esl_cmake}/../contracts/" DESTINATION ${_docs_root}/contracts
        FILES_MATCHING PATTERN "*.md" PATTERN "*.json" PATTERN "*.yaml")
    if(EXISTS "${_esl_cmake}/../registry.yaml")
        set(_repo "${_esl_cmake}/..")
        file(GLOB_RECURSE _doc_files RELATIVE "${_repo}"
            "${_repo}/docs/*.md" "${_repo}/models/*.md" "${_repo}/examples/*.md"
            "${_repo}/systems/*.md"
            "${_repo}/common/*.md" "${_repo}/reference/*.md" "${_repo}/tests/*.md")
        list(APPEND _doc_files README.md registry.yaml)
        foreach(_file IN LISTS _doc_files)
            get_filename_component(_directory "${_file}" DIRECTORY)
            install(FILES "${_repo}/${_file}" DESTINATION "${_docs_root}/${_directory}")
        endforeach()
    endif()
endfunction()
