Clustered deferred shading in Vulkan API
================================
<p align="center">
    <img width="80%"  src="https://github.com/CaptainUnitaco/Clustered-Deferred-shading-in-Vulkan/blob/Cmake/d2_intro.png?raw=true">
</p>

## About
Implementation of clustered deferred shading using *Vulkan API*. 

I'm sure that code is not perfect, contains lot of bugs and some bad decisions were made. My goal was to learn *Vulkan API* and also learn as much as possible things from field of computer graphics. Everything was created from scratch, without any previous knowledge, in 5 months.

Application was developed under Visual Studio IDE, and the development branch is [PingPong](https://github.com/CaptainUnitaco/Clustered-Deferred-shading-in-Vulkan/tree/PingPong).

Special thanks to Ing. Tomáš Milet, for the amount of information he gave me through development.

This implementation is part of bachelor thesis at [FIT BUT](http://www.fit.vutbr.cz).

## Dependencies
* [GLSLang Validator](https://github.com/KhronosGroup/glslang)
* [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools)
* [OpenGL Mathematics](https://glm.g-truc.net/0.9.9/index.html)
* [GLFW](https://www.glfw.org/)
* [Dear ImGUI](https://github.com/ocornut/imgui)
* [tinyobjloader](https://github.com/syoyo/tinyobjloader)
* [LodePNG](https://lodev.org/lodepng/)

## Todo
* Radix sort
* Better memory management
* Shadows
* Amd optimization (currently really bad performance)
