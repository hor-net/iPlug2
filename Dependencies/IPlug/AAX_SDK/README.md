<!--
 *
 * Copyright 2024-2025 Avid Technology, Inc.
 * All rights reserved.
 * 
 * This file is part of the Avid AAX SDK.
 * 
 * The AAX SDK is subject to commercial or open-source licensing.
 * 
 * By using the AAX SDK, you agree to the terms of both the Avid AAX SDK License
 * Agreement and Avid Privacy Policy.
 * 
 * AAX SDK License: https://developer.avid.com/aax
 * Privacy Policy: https://www.avid.com/legal/privacy-policy-statement
 * 
 * Or: You may also use this code under the terms of the GPL v3 (see
 * www.gnu.org/licenses).
 * 
 * THE AAX SDK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
 * EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
 * DISCLAIMED.
 *
 -->

# AAX SDK

## Prerequisites

To access the AAX SDK, first sign up as an AAX developer at
[developer.avid.com/audio](https://developer.avid.com/audio). You can then download
the AAX SDK from [my.avid.com](https://my.avid.com/products/cppsdk)

Pro Tools requires an iLok authorization. Contact devauth@avid.com if you need to
request a license.

Pro Tools also requires that AAX plugins are digitally signed using tools from PACE
Anti-Piracy Inc. Special "developer builds" of Pro Tools can run unsigned plugins.
Developer build installers are available at [my.avid.com](https://my.avid.com/products/cppsdk).

When you are ready to make your AAX products available to users, request access to the
digital signing tools by writing to audiosdk@avid.com

## Quick Start

1. Build the AAX Library project in [Libs/AAXLibrary](Libs/AAXLibrary)
2. Build any of the example plugin projects in [ExamplePlugIns](ExamplePlugIns)
3. Install the resulting aaxplugin into the AAX plug-ins directory

   * macOS: /Library/Application Support/Avid/Audio/Plug-Ins
   * Windows: C:\Program Files\Common Files\Avid\Audio\Plug-Ins

4. Run a developer build of Pro Tools to use the plugin

<!--
The AAX Library is a static library containing base implementations of the
AAX API. AAX plug-ins typically link to this library to inherit a default
implementation of the AAX API, then override the library classes to provide
customized behavior.
-->

## Documentation

See the [code generated documentation](docs.html) for more information about the AAX SDK

The SDK documentation is also provided in PDF form. The PDF documentation is not
recommended for browsing but can be very useful for text-based searches.


## Building with CMake

The AAX SDK includes project descriptions using [CMake](https://cmake.org). CMake can
generate and build these projects in a location you specify.

These example commands will generate projects into the folder `/path/to/MyBuild`:
- Default makefiles (macOS) or Visual Studio projects (Windows)
  - `cmake -B /path/to/MyBuild -S /path/to/aax-sdk`
- Xcode projects
  - `cmake -B /path/to/MyBuild -S /path/to/aax-sdk -G Xcode`
- Visual Studio projects with a specific version
  - `cmake -B /path/to/MyBuild -S /path/to/aax-sdk -G "Visual Studio 16 2019"`

To build the generated projects with CMake:
- `cmake --build /path/to/MyBuild`

It is common practice to run `cmake` commands from within the SDK folder:

```bash
# Go to the SDK root folder
cd /path/to/aax-sdk

# Clear any previously generated projects in folder "build"
rm -rf build

# Generate all projects in folder "build"
cmake -B build -S .

# Build all generated projects
cmake --build build
```

### Optional Flags

Some optional flags are defined in [CMakeLists.txt](CMakeLists.txt) at the root of the SDK. Set these
flags when generating the projects. To set a flag, use the `-D` option prefix. For
example:
- Define a custom output path using `AAX_SDK_PLUGIN_OUTPUT_DIRECTORY`:
  - `cmake --build build -S . -DAAX_SDK_PLUGIN_OUTPUT_DIRECTORY=/path/to/MyPlugIns`
- Skip the example plugin projects by disabling `AAX_BUILD_EXAMPLES`:
  - `cmake --build build -S . -DAAX_BUILD_EXAMPLES=FALSE`

### CMake Presets

[CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) allow you to pre-define your preferred generator settings. Specify a preset using the `--preset` option. For example:
- Generate the AAX Library target only using a CMake preset:
  - `cmake -B build -S . --preset=aaxlibrary`


## Tips

If you create a folder named "Plug-Ins" next to your host executable then
this plug-ins folder will be used instead of the the system plug-ins folder.

On Windows, if you place the SDK too deep in your file hierarchy you may
encounter build failures due to the system's path length limitation. To avoid
this issue, locate the SDK near the root of your drive.
