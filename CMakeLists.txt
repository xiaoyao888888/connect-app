cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR) 
project(rkwifibt)

file(GLOB_RECURSE WIFI_SRC "wifi/*.c")
file(GLOB_RECURSE UTILITY "utility/*.c")

add_definitions(-DREALTEK -DDefGContext)

message("build rkwifibt app ...")

add_definitions(-DBLUEZ5_UTILS -DFIXED_POINT=16)

file(GLOB_RECURSE BLUZ_SRC "bluez/*.c")

add_library(rkwifibt SHARED
#add_library(rkwifibt STATIC
		${WIFI_SRC}
		${UTILITY}
		${BLUZ_SRC}
		)

target_include_directories(rkwifibt PUBLIC
		"bluez/include"
		"utility"
		"include"
		)

set(TEST_FILES
	test/main.c
	test/bt_test.c
	test/rk_ble_app.c
	test/rk_wifi_test.c
	test/softap/softap.c
)
set(SERVER_FILES 
	test/rk3326-wangyi-dictionary-pan/rkbtsource_server.c
)
set(CLIENT_FILES 
	test/rk3326-wangyi-dictionary-pan/rkbtsource.c
)

find_package(PkgConfig)
pkg_check_modules (GLIB2 glib-2.0 REQUIRED)
pkg_check_modules (DBUS dbus-1 REQUIRED)

include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${DBUS_INCLUDE_DIRS})
include_directories(${WPA_SUPPLICANT_INCLUDE_DIRS})

#target_link_libraries (rkwifibt libglib-2.0.a)
#target_link_libraries (rkwifibt libdbus-1.a)
#target_link_libraries (rkwifibt libbluetooth-internal.a)
#target_link_libraries (rkwifibt libwpa_client.a)

target_link_libraries (rkwifibt ${GLIB2_LIBRARIES})
target_link_libraries (rkwifibt ${DBUS_LIBRARIES})
target_link_libraries (rkwifibt libbluetooth.so)
target_link_libraries (rkwifibt libwpa_client.so)
#target_link_libraries (rkwifibt libasan.so)
#target_link_libraries (rkwifibt libubsan.so)


#add_definitions("-Wall -rdynamic -g -gdwarf -O0 -fpermissive -Wno-unused-variable -Wno-write-strings -Wno-unused-function -Wno-write-strings")
#add_definitions("-Wall -g -rdynamic -fPIC -gdwarf -O0 -Wno-unused-function -Wno-unused-variable -D_GNU_SOURCE")
add_definitions("-Wall -g -O1 -D_GNU_SOURCE")
add_definitions("-Wno-deprecated-declarations -Wno-unused-function -Wno-unused-result")
#add_definitions("-Wno-unused-variable -Wno-write-strings -Wno-unused-function -Wno-unused-result")
#add_definitions("-fsanitize=address")
#add_definitions("-fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined")
add_definitions("-fvisibility=hidden")

add_executable(rkwifibt_app_test ${TEST_FILES})
target_include_directories(rkwifibt_app_test PUBLIC "include" )
target_link_libraries(rkwifibt_app_test pthread rkwifibt)

# install target
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS rkwifibt_app_test DESTINATION bin)
