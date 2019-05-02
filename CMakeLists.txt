cmake_minimum_required(VERSION 3.14)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(GLSLANGLIBS glslang HLSL OSDependent OGLCompiler SPIRV SPIRV-Tools SPIRV-Tools-opt)

project(CDSV)
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# GLSLang validator
FetchContent_Declare(
	glslangGit
	GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
	GIT_TAG 7.11.3188
	UPDATE_COMMAND python ${FETCHCONTENT_BASE_DIR}/glslanggit-src/update_glslang_sources.py
	WORKING_DIRECTORY ${FETCHCONTENT_BASE_DIR}/glslanggit-src/
)
FetchContent_MakeAvailable(glslangGit)

# ImGUI
include_directories("3rdParty/ImGui/include")

file(GLOB imguiSrc "3rdParty/ImGui/src/*.cpp" "3rdParty/ImGui/src/*.h")
add_library(imgui STATIC ${imguiSrc} ${imguiInc})
target_link_libraries(imgui glfw)

# actual project
file(GLOB projectFiles "src/*")
add_executable(${PROJECT_NAME} ${projectFiles})

target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan glfw glm imgui ${GLSLANGLIBS})
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)


