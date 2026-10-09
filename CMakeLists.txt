cmake_minimum_required(VERSION 3.16)
project(PubMedSearch VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(PubMedSearch
    main.cpp
    article.h
    pubmedclient.h
    pubmedclient.cpp
    mainwindow.h
    mainwindow.cpp
)

target_link_libraries(PubMedSearch PRIVATE Qt6::Widgets Qt6::Network)
