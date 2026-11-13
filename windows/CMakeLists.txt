set(KENVIX_WINDOWS_COMMON
    storage.cpp
    ble.cpp
    security.cpp
    remote_storage.cpp
    remote_ipc.cpp)
add_library(kenvix_windows STATIC ${KENVIX_WINDOWS_COMMON})
target_link_libraries(kenvix_windows PUBLIC kenvix_protocol windowsapp bcrypt crypt32 advapi32 shell32 ole32 runtimeobject wtsapi32)
target_compile_definitions(kenvix_windows PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kenvix_windows PRIVATE /W4 /EHsc /utf-8)
target_include_directories(kenvix_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(KenvixBleCredentialProvider SHARED credential_provider.cpp credential_provider.def)
target_link_libraries(KenvixBleCredentialProvider PRIVATE kenvix_windows credui secur32)
target_compile_options(KenvixBleCredentialProvider PRIVATE /W4 /EHsc /utf-8)
set_target_properties(KenvixBleCredentialProvider PROPERTIES PREFIX "")

add_executable(KenvixBluetoothUnlock WIN32
    settings.cpp remote_settings.cpp remote_service.cpp remote_http.cpp
    service_manager.cpp settings.rc)
target_link_libraries(KenvixBluetoothUnlock PRIVATE
    kenvix_windows winhttp dwmapi comctl32 gdi32 user32 shell32)
target_link_options(KenvixBluetoothUnlock PRIVATE /MANIFEST:NO)
target_compile_options(KenvixBluetoothUnlock PRIVATE /W4 /EHsc /utf-8)

install(TARGETS KenvixBleCredentialProvider KenvixBluetoothUnlock
        RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
if(BUILD_TESTING)
    add_test(NAME app_self_test COMMAND $<TARGET_FILE:KenvixBluetoothUnlock> --self-test)
endif()
