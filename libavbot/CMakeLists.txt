cmake_minimum_required(VERSION 2.8)
project(libavbot)


add_library(libavbot STATIC
	avbot.hpp avbot.cpp
	avbot_account_and_message_loop.cpp avbot_account_and_message_loop.hpp
	avbot_accounts.hpp
)

target_include_directories(libavbot PUBLIC
    $<TARGET_PROPERTY:xmpp,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(libavbot irc webqq xmpp mailexchange)

set_target_properties(libavbot
		PROPERTIES
		OUTPUT_NAME avbot
		PREFIX "lib"
		CLEAN_DIRECT_OUTPUT 1)