from os.path import join

Import("env")


framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")

# pioarduino 3.3.7 does not consistently propagate transitive include paths
# between Arduino core libraries. Add the framework library headers that are
# referenced from other libraries so dependent units compile reliably.
framework_lib_includes = [
    "ESPmDNS",
    "FS",
    "Hash",
    "Network",
    "SPI",
    "Update",
    "Wire",
]

env.Append(
    CPPPATH=[
        join(framework_dir, "libraries", library_name, "src")
        for library_name in framework_lib_includes
    ]
)
