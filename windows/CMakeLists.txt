set(KENVIX_WINDOWS_COMMON
    storage.cpp
    ble.cpp
    security.cpp
    remote_storage.cpp
    remote_ipc.cpp)
add_library(kenvix_windows STATIC ${KENVIX_WINDOWS_COMMON})
target_link_libraries(kenvix_windows PUBLIC kenvix_protocol windowsapp bcrypt crypt32 advapi32 shell32 ole32 runtimeobject wtsapi32)
target_compile_definitions(kenvix_windows PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kenvix_windows PRIVATE /W4 /EHsc)
target_include_directories(kenvix_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(KenvixBleCredentialProvider SHARED credential_provider.cpp credential_provider.def)
target_link_libraries(KenvixBleCredentialProvider PRIVATE kenvix_windows credui secur32)
target_compile_options(KenvixBleCredentialProvider PRIVATE /W4 /EHsc)
set_target_properties(KenvixBleCredentialProvider PROPERTIES PREFIX "")
add_executable(KenvixBleSettings WIN32 settings.cpp settings.rc)
target_link_libraries(KenvixBleSettings PRIVATE kenvix_windows dwmapi comctl32 gdi32 user32)
target_link_options(KenvixBleSettings PRIVATE /MANIFEST:NO)
target_compile_options(KenvixBleSettings PRIVATE /W4 /EHsc)
add_executable(KenvixRustDeskService remote_service.cpp remote_http.cpp)
target_link_libraries(KenvixRustDeskService PRIVATE kenvix_windows winhttp user32)
target_compile_options(KenvixRustDeskService PRIVATE /W4 /EHsc)
add_executable(KenvixRustDeskSettings WIN32 remote_settings.cpp remote_settings.rc remote_http.cpp)
target_link_libraries(KenvixRustDeskSettings PRIVATE kenvix_windows winhttp dwmapi comctl32 gdi32 user32)
target_link_options(KenvixRustDeskSettings PRIVATE /MANIFEST:NO)
target_compile_options(KenvixRustDeskSettings PRIVATE /W4 /EHsc)
install(TARGETS KenvixBleCredentialProvider KenvixBleSettings KenvixRustDeskService KenvixRustDeskSettings
        RUNTIME DESTINATION bin LIBRARY DESTINATION bin)

foreach(target kenvix_windows KenvixBleCredentialProvider KenvixBleSettings KenvixRustDeskService KenvixRustDeskSettings)
  target_compile_options(${target} PRIVATE /utf-8)
endforeach()
