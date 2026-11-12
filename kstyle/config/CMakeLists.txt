### configuration plugin
add_definitions(-DTRANSLATION_DOMAIN=\"breeze_style_config\")
set(kstyle_breeze_config_PART_SRCS breezestyleconfig.cpp)

set(kstyle_breeze_config_PART_SRCS ${kstyle_breeze_config_PART_SRCS} breezestyleconfigmodule.cpp)

kconfig_add_kcfg_files(kstyle_breeze_config_PART_SRCS ../breezestyleconfigdata.kcfgc)
# Breeze NG: the settings are built in code (no .ui) and the plugin has its own name: under the same name as the
# stock one it stood in for it, also for Breeze itself
kcoreaddons_add_plugin(breezengstyleconfig SOURCES
    ${kstyle_breeze_config_PART_SRCS}
    INSTALL_NAMESPACE "kstyle_config"
)

target_link_libraries(breezengstyleconfig
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

if(HAVE_QTDBUS)
    target_link_libraries(breezengstyleconfig
        PUBLIC
            Qt6::DBus
    )
endif()

target_link_libraries(breezengstyleconfig
    PUBLIC
        KF6::CoreAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::I18n
)
if(QT_MAJOR_VERSION STREQUAL "6")
    target_link_libraries(breezengstyleconfig
        PUBLIC
    KF6::KCMUtils
)
endif()
kcmutils_generate_desktop_file(breezengstyleconfig)

set(breeze_settings_SOURCES main.cpp)
add_executable(breeze-settings ${breeze_settings_SOURCES} )
target_link_libraries(breeze-settings Qt6::Core Qt6::Gui Qt6::Widgets )
target_link_libraries(breeze-settings KF6::I18n KF6::KCMUtils KF6::CoreAddons)

set_target_properties(breeze-settings PROPERTIES OUTPUT_NAME breezeng-settings)

install(TARGETS breeze-settings ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})


# kcmshell6 < 6.27.0 doesn't know the --smoke-test param, so skip this test for
# now.
# TODO: Drop this when KF6_MIN_VERSION >= 6.27.0
if(KF6KCMUtils_VERSION VERSION_GREATER_EQUAL "6.27.0")
    add_test(NAME breezestyleconfig_smoketest COMMAND kcmshell6 -style BreezeNG --smoke-test kstyle_config/breezengstyleconfig)
    set_tests_properties(breezestyleconfig_smoketest PROPERTIES
        ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
    )
endif()
