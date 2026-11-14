set(KENVIX_WINDOWS_COMMON
    storage.cpp
    tpm_protection.cpp
    ble.cpp
    security.cpp
    remote_storage.cpp
    remote_ipc.cpp)
add_library(bluetooth_unlock_windows STATIC ${KENVIX_WINDOWS_COMMON})
target_link_libraries(bluetooth_unlock_windows PUBLIC bluetooth_unlock_protocol windowsapp bcrypt ncrypt crypt32 advapi32 shell32 ole32 runtimeobject wtsapi32)
target_compile_definitions(bluetooth_unlock_windows PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(bluetooth_unlock_windows PRIVATE /W4 /EHsc /utf-8)
target_include_directories(bluetooth_unlock_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(BluetoothUnlockCredentialProvider SHARED credential_provider.cpp credential_provider.def)
target_link_libraries(BluetoothUnlockCredentialProvider PRIVATE bluetooth_unlock_windows credui secur32)
target_compile_options(BluetoothUnlockCredentialProvider PRIVATE /W4 /EHsc /utf-8)
set_target_properties(BluetoothUnlockCredentialProvider PROPERTIES PREFIX "")

add_executable(BluetoothUnlock WIN32
    settings.cpp remote_settings.cpp remote_service.cpp remote_http.cpp
    service_manager.cpp service_settings.cpp settings.rc)
target_link_libraries(BluetoothUnlock PRIVATE
    bluetooth_unlock_windows winhttp dwmapi comctl32 uxtheme gdi32 user32 shell32)
target_link_options(BluetoothUnlock PRIVATE /MANIFEST:NO)
target_compile_options(BluetoothUnlock PRIVATE /W4 /EHsc /utf-8)

install(TARGETS BluetoothUnlockCredentialProvider BluetoothUnlock
        RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
if(BUILD_TESTING)
    add_test(NAME app_self_test COMMAND $<TARGET_FILE:BluetoothUnlock> --self-test)
    add_executable(interop_windows_tests ../tests/interop_win_tests.cpp)
    target_link_libraries(interop_windows_tests PRIVATE bluetooth_unlock_windows)
    target_compile_options(interop_windows_tests PRIVATE /W4 /EHsc /utf-8)
    add_test(NAME interop_windows_tests COMMAND $<TARGET_FILE:interop_windows_tests>)
    add_executable(tpm_storage_tests ../tests/tpm_storage_tests.cpp)
    target_link_libraries(tpm_storage_tests PRIVATE bluetooth_unlock_windows)
    target_compile_options(tpm_storage_tests PRIVATE /W4 /EHsc /utf-8)
    add_test(NAME tpm_storage_tests COMMAND $<TARGET_FILE:tpm_storage_tests>)
    set_tests_properties(interop_windows_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
