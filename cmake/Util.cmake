include(GenerateExportHeader)

function(CreateConfigPackageFile target_name namespace_name)
    message(STATUS "Creating config package file for the target ${target_name}")

    generate_export_header(${target_name})

    install(TARGETS ${target_name} EXPORT ${target_name}Targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
    )

    export(EXPORT ${target_name}Targets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/${target_name}/${target_name}Targets.cmake"
        NAMESPACE ${namespace_name}::
    )

    configure_file(cmake/${target_name}Config.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}/${target_name}Config.cmake"
        COPYONLY
    )

    set(ConfigPackageLocation lib/cmake/${target_name})

    install(EXPORT ${target_name}Targets
        FILE ${target_name}Targets.cmake
        NAMESPACE ${namespace_name}::
        DESTINATION ${ConfigPackageLocation}
    )

    install(
        FILES
            cmake/${target_name}Config.cmake
        DESTINATION
            ${ConfigPackageLocation}
        COMPONENT
            Devel
    )
endfunction()
