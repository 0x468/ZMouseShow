function(zmouse_apply_project_options target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_23)

    target_compile_definitions(${target_name}
        PRIVATE
            NOMINMAX
            STRICT
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00
    )

    target_compile_options(${target_name}
        PRIVATE
            /W4
            /WX
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
            /external:anglebrackets
            /external:W0
            /guard:cf
    )
endfunction()

function(zmouse_apply_windows_hardening target_name)
    target_link_options(${target_name}
        PRIVATE
            /DYNAMICBASE
            /NXCOMPAT
            /CETCOMPAT
            /guard:cf
    )
endfunction()
