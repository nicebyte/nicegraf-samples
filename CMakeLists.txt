#[[
Copyright (c) 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]]

cmake_minimum_required(VERSION 3.0.2)

project(nicegraf-samples)

if (WIN32)
  set(NICEGRAF_COMMON_COMPILE_OPTS "/W4" "/WX" "/std:c++17")
  set(EXECUTABLE_SUFFIX ".exe")
else()
  set(NICEGRAF_COMMON_COMPILE_OPTS "--std=c++17" "-Wall" "-Wconversion" "-Werror"
        "-Wno-unknown-pragmas")
  set(EXECUTABLE_SUFFIX "")
endif()

# add_definitions(-DNGF_NO_IMGUI)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/nicegraf)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/nicegraf-shaderc)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/glfw)

add_library(imgui
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui/imgui.cpp
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui/imgui_draw.cpp
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui/imgui_widgets.cpp
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui/imgui_demo.cpp
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui/examples/imgui_impl_glfw.cpp)

target_include_directories(imgui PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui)

target_compile_options(imgui PRIVATE "--std=c++17")

target_link_libraries(imgui glfw)
  
add_library(imgui_editor 
  ${CMAKE_CURRENT_LIST_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/third_party/ImGuiColorTextEdit/TextEditor.h
)

target_include_directories(imgui_editor PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/third_party/ImGuiColorTextEdit
)

target_link_libraries(imgui_editor imgui)

target_compile_options(imgui_editor PRIVATE "--std=c++14")

set(NGF_SAMPLES_COMMON_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/common/common.cpp
  ${CMAKE_CURRENT_LIST_DIR}/common/common.h
  ${CMAKE_CURRENT_LIST_DIR}/common/imgui_ngf_backend.h
  ${CMAKE_CURRENT_LIST_DIR}/common/imgui_ngf_backend.cpp)

if(APPLE)
  add_definitions(-DNGF_BACKEND_METAL)
  set(NGF_SAMPLES_COMMON_SOURCES ${NGF_SAMPLES_COMMON_SOURCES}
      ${CMAKE_CURRENT_LIST_DIR}/common/get_glfw_contentview.mm
      ${CMAKE_CURRENT_LIST_DIR}/common/apple_main.mm)
else()
  #add_definitions(-DNGF_BACKEND_OPENGL)
  add_definitions(-DNGF_BACKEND_VULKAN)
endif()

add_library(common
  ${NGF_SAMPLES_COMMON_SOURCES})
 
target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/common)

target_include_directories(common PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated)
  
target_compile_options(common PRIVATE ${NICEGRAF_COMMON_COMPILE_OPTS})

target_link_libraries(common glfw imgui imgui_editor nicegraf_vk nicegraf_util)

add_dependencies(common generated_shaders)

if(WIN32)
  set(NGF_OPENGL_LIB "opengl32.lib")
elseif(UNIX AND NOT APPLE)
  set(NGF_OPENGL_LIB "GL")
endif()

function(set_output_dir target dir)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${dir}")
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${dir}")
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${dir}")
endfunction(set_output_dir)

file(GLOB shader_files ${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/hlsl/*.hlsl)
foreach(source_path ${shader_files})
  file(STRINGS ${source_path} tech_lines REGEX "// *T *: *([a-zA-Z0-9_]+)")
  if (tech_lines)
    set(tech_names "")
    foreach(tech_line ${tech_lines})
      string(REPLACE ":" ";" tmp ${tech_line})
      list(GET tmp 1 tmp)
      string(STRIP "${tmp}" tmp)
      string(REGEX REPLACE " +" ";" tmp ${tmp})
      list(GET tmp 0 tech_name)
      list(APPEND tech_names "${tech_name}")
    endforeach(tech_line)
    set(output_files_list "")
    foreach(tech ${tech_names})
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.vs.430.glsl")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.vs.12.msl")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.vs.spv")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.ps.430.glsl")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.ps.12.msl")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.ps.spv")
      list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${tech}.pipeline")
    endforeach(tech)
    get_filename_component(header_file_name ${source_path} NAME_WE)
    list(APPEND output_files_list "${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated/${header_file_name}_binding_consts.h")
    add_custom_command(OUTPUT ${output_files_list}
                       MAIN_DEPENDENCY ${source_path}
                       DEPENDS nicegraf_shaderc
                       COMMAND ${CMAKE_CURRENT_LIST_DIR}/nicegraf-shaderc/nicegraf_shaderc${EXECUTABLE_SUFFIX} ARGS ${source_path} "-t" "gl430" "-t" "msl12" "-t" "spv" "-O" "../generated/" "-h" "${header_file_name}_binding_consts.h"
                       WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/hlsl)
    set(generated_shaders_list "${output_files_list};${generated_shaders_list}")
  endif()
endforeach(source_path)
add_custom_target(generated_shaders DEPENDS ${generated_shaders_list})

function (add_sample volume number name)
  set(SAMPLE ${number}-${name})
  add_executable(${SAMPLE}
    "${CMAKE_CURRENT_LIST_DIR}/vol-${volume}/${number}-${name}/${name}.cpp")
  target_link_libraries(${SAMPLE} common ${NGF_OPENGL_LIB})
  target_compile_options(${SAMPLE} PRIVATE ${NICEGRAF_COMMON_COMPILE_OPTS})
  target_include_directories(${SAMPLE} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/third_party/tinyobjloader)
  target_include_directories(${SAMPLE} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/artifacts/shaders/generated)
  add_dependencies(${SAMPLE} generated_shaders)
  set_output_dir(${SAMPLE} "${CMAKE_CURRENT_LIST_DIR}/artifacts")
  set_target_properties(${SAMPLE} PROPERTIES PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build")
  set_target_properties(${SAMPLE} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/artifacts")
endfunction(add_sample)

add_sample("01" "00" "imgui")
add_sample("01" "01" "full-screen-triangle")
add_sample("01" "02" "spec-consts")
add_sample("01" "03" "hexagon")
add_sample("01" "04" "hexagon-indexed")
add_sample("01" "05" "uniform-buffers")
add_sample("01" "06" "depth")
add_sample("01" "07" "simple-texture")
add_sample("01" "08" "render-to-texture")
add_sample("01" "09" "texture-sampling")
add_sample("01" "0a" "instancing")
add_sample("01" "0b" "shader-editor")
add_sample("01" "0c" "cubemap")
add_sample("02" "00" "model-view-projection")

