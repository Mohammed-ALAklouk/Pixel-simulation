project "Game"
    kind "ConsoleApp"
    language "c++"
    
    includedirs {
        "Dependencies/SFML/include/",
        "Dependencies/ImGui/"
    }

    targetdir "bin/%{cfg.buildcfg}"
    
    files {"**.h", "**.cpp"}
    
    filter "configurations: Debug"
        defines { "DEBUG" }
        symbols "On"
        
        links {
            "sfml-system-d",
            "sfml-graphics-d",
            "sfml-window-d"
        }
        libdirs { "Dependencies/SFML/lib" }
    
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
        
        links {
            "sfml-system",
            "sfml-graphics",
            "sfml-window",
            "opengl32"
        }
        libdirs { "Dependencies/SFML/lib" }
