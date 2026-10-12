set(SELFHOSTED_UI_TEST_TARGET selfhosted_ui_models_test)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

set(_client_root "${CMAKE_CURRENT_LIST_DIR}/../../../client")

add_executable(${SELFHOSTED_UI_TEST_TARGET}
    containers_model_test.cpp
    ${_client_root}/core/utils/containerEnum.h
    ${_client_root}/core/utils/protocolEnum.h
    ${_client_root}/ui/models/containersModel.cpp
    ${_client_root}/core/utils/containers/containerUtils.cpp
    ${_client_root}/core/protocols/protocolUtils.cpp
    ${_client_root}/core/models/containerConfig.cpp
    ${_client_root}/core/models/protocolConfig.cpp
    ${_client_root}/core/models/protocols/awgProtocolConfig.cpp
    ${_client_root}/core/models/protocols/amgptAuthProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/dnsProtocolConfig.cpp
    ${_client_root}/core/models/protocols/ikev2ProtocolConfig.cpp
    ${_client_root}/core/models/protocols/mtProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/openVpnProtocolConfig.cpp
    ${_client_root}/core/models/protocols/openClawCodexProtocolConfig.cpp
    ${_client_root}/core/models/protocols/sftpProtocolConfig.cpp
    ${_client_root}/core/models/protocols/socks5ProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/telemtProtocolConfig.cpp
    ${_client_root}/core/models/protocols/torProtocolConfig.cpp
    ${_client_root}/core/models/protocols/wireGuardProtocolConfig.cpp
    ${_client_root}/core/models/protocols/xrayProtocolConfig.cpp
)

target_compile_features(${SELFHOSTED_UI_TEST_TARGET} PRIVATE cxx_std_17)
target_include_directories(${SELFHOSTED_UI_TEST_TARGET} PRIVATE ${_client_root})
target_link_libraries(${SELFHOSTED_UI_TEST_TARGET} PRIVATE Qt6::Core Qt6::Test)
set_target_properties(${SELFHOSTED_UI_TEST_TARGET} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

add_test(NAME selfhosted.ui COMMAND ${SELFHOSTED_UI_TEST_TARGET})
set_tests_properties(selfhosted.ui PROPERTIES
    LABELS "unit"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests ${SELFHOSTED_UI_TEST_TARGET})
endif()

# Exercise the real trust input component with the selected Qt kit.
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
find_program(AMNEZIA_QMLTESTRUNNER qmltestrunner HINTS "${_qt_bin}" REQUIRED)
add_test(NAME selfhosted.trust-input-qml
    COMMAND "${AMNEZIA_QMLTESTRUNNER}" -input "${CMAKE_CURRENT_LIST_DIR}/tst_public_trust_input.qml")
set_tests_properties(selfhosted.trust-input-qml PROPERTIES
    LABELS "qml"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Render and operate the production workload page with fake controller/process boundaries.
find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick)
add_executable(selfhosted_workload_page_test workload_page_test.cpp ${_client_root}/ui/utils/pageEnum.h
    ${_client_root}/images/images.qrc)
target_include_directories(selfhosted_workload_page_test PRIVATE ${_client_root})
target_compile_features(selfhosted_workload_page_test PRIVATE cxx_std_17)
target_compile_definitions(selfhosted_workload_page_test PRIVATE AMNEZIA_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(selfhosted_workload_page_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
set_target_properties(selfhosted_workload_page_test PROPERTIES AUTOMOC ON AUTORCC ON)
add_test(NAME selfhosted.workload-page-qml COMMAND selfhosted_workload_page_test)
set_tests_properties(selfhosted.workload-page-qml PROPERTIES LABELS "qml" TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests selfhosted_workload_page_test)
endif()
