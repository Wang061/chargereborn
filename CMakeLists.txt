# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# S3-Forge: 不用 MINIMAL_BUILD —— 真实固件需完整组件 + Core Dump/调试组件可用。
project(chargereborn_brain)
