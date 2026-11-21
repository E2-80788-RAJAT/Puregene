cmake_minimum_required(VERSION 3.16)
project(PureGeneApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# CRITICAL FIX: Added 'Widgets' required for Qt Charts
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick Charts)

qt_add_executable(appPureGeneApp
    main.cpp
    HardwareController.cpp
    HardwareController.h
    drv8825.c
)

qt_add_qml_module(appPureGeneApp
    URI PureGene
    VERSION 1.0
    QML_FILES main.qml
)

target_link_libraries(appPureGeneApp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::Charts
    lgpio curl pthread
)