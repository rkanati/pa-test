workspace "pa-test"
  configurations { "debug", "release" }

project "pa-test"
  kind "ConsoleApp"
  language "C++"
  targetdir "%{cfg.buildcfg}/bin"

  files { "src/**.hpp", "src/**.cpp" }

  includedirs { "rk-core/include" }
  links { "pulse" }

  warnings "Extra"

  filter "configurations:debug"
    defines { "DEBUG" }
    flags { "Symbols" }

  filter "configurations:release"
    defines { "NDEBUG" }
    optimize "On"

  filter "action:gmake" -- FIXME: this should be toolset:gcc, but toolset: is broken in premake5 as of 2015-09-01
    buildoptions { "-std=c++14" }

