# My First Openvr Triangle
A minimal test of displaying something with OpenVR

Depends on: 

- [SDL2](http://libsdl.org)
- [GLEW](http://glew.sourceforge.net)
- [GLM](http://glm.g-truc.net/0.9.8/index.html)
- [OpenVR](https://github.com/ValveSoftware/openvr)

## Overview

1. Quick Diagnostic
  - Checks if it can detect an HMD
  - Checks if the runtime is installed
2. Intialise OpenVR
3. Setup the companion window
4. Initialise OpenGL context + some scene data
5. Create frame buffer objects
  - We need two frame buffers for each eye (4 in total).
  - The first is the one you're application will actually use and has the associated image, depth, stencil etc. components
  - After rendering is done we blit the result to a 'resolve' buffer, that contains an image component only and send that to the HMD
6. Initialse the Compositor
7. Run the main loop
  1. `UpdateHMDMatrrixPose()`
    - This gets the current HMD position using the function `vr::VRCompositor()->WaitGetPoses()`
    - `WaitGetPoses()` __must__ be called otherwise the app will be considered unresponsive and nothing will be dislpayed on the HMD
  2. Render scene for left and right eyes
  3. Send frame buffers to the HMD
  4. Send frame buffers to the companion window
