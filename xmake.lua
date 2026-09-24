set_xmakever("3.0.0")

set_project("TESFormSafeLoad")
set_version("0.1.0")
set_languages("c++23")
set_arch("x64")
set_warnings("allextra")
set_encodings("utf-8")

local commonlibRoot = os.getenv("COMMONLIBF4_ROOT")
if not commonlibRoot or commonlibRoot == "" then
    commonlibRoot = path.join(os.projectdir(), "third_party", "commonlibf4")
end

if not os.isfile(path.join(commonlibRoot, "xmake.lua")) then
    raise("CommonLibF4 was not found. Set COMMONLIBF4_ROOT to a compatible checkout.")
end

includes(path.join(commonlibRoot, "xmake.lua"))

target("SaveLoadGuardF4", function()
    set_kind("shared")
    set_default(true)
    set_version("0.1.0")
    set_targetdir("$(builddir)/plugins")
    set_pcxxheader("src/PCH.h")

    add_rules("commonlibf4.plugin", {
        name = "SaveLoadGuardF4",
        author = "SaveLoadGuardF4 contributors",
        description = "Guard Fallout 4 save-load editor-ID dispatch against stale TESForm vtables",
        plugin_template = "res/commonlibf4-plugin.cpp.in"
    })

    add_files(
        "src/Configuration.cpp",
        "src/Diagnostics/PeriodicDrain.cpp",
        "src/Diagnostics/SaveLoadIncidentQueue.cpp",
        "src/Hooks/SaveLoadGuard.cpp",
        "src/Platform/Logging.cpp",
        "src/Runtime/SaveLoadGuardScan.cpp",
        "src/SaveLoadGuardMain.cpp",
        "src/Validation/MemoryReader.cpp",
        "src/Validation/SaveLoadGuardValidation.cpp")
    add_includedirs("src", { public = true })
    add_syslinks("shell32", "ole32")
    add_cxxflags("cl::/EHsc", "cl::/permissive-", "cl::/bigobj")
    add_shflags("/Brepro")
end)
