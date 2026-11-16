cmake_minimum_required(VERSION 3.28)
project(Helix.RenderPipelines.Physical LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(RenderPipelines.Core)
include("${HELIX_BINDGEN_ROOT}/CMake/HelixBindgen.cmake")

find_program(HELIX_DOTNET NAMES dotnet HINTS "$ENV{HELIX_DOTNET_ROOT}" "$ENV{DOTNET_ROOT}" REQUIRED)
set(physical_artifacts "${CMAKE_BINARY_DIR}/HelixRenderPipelinesPhysical")
helix_dependency_dir(shaders_dir Shaders)
helix_dependency_dir(engine_dir Engine)
helix_dependency_dir(math_dir Math)
helix_dependency_dir(core_dir RenderPipelines.Core)
set(engine_source "${engine_dir}/Source")
set(core_source "${core_dir}/Source")
set(physical_source "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# The pipeline in C++: a native module (HelixRenderPipelinesPhysical) over the engine and the core, a shared library
# beside theirs (a static one linked into the app on the Web). C# reaches it through its bindings.
file(GLOB_RECURSE physical_cpp CONFIGURE_DEPENDS "${physical_source}/Helix/RenderPipelines/Physical/*.cpp")
file(GLOB_RECURSE physical_headers CONFIGURE_DEPENDS "${physical_source}/Helix/RenderPipelines/Physical/*.h")
if(HELIX_PLATFORM STREQUAL "web")
    add_library(helix_render_pipelines_physical STATIC ${physical_cpp} ${physical_headers})
else()
    add_library(helix_render_pipelines_physical SHARED ${physical_cpp} ${physical_headers})
    set_target_properties(helix_render_pipelines_physical PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
                                                                     LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
target_link_libraries(helix_render_pipelines_physical PUBLIC Helix::RenderPipelines.Core)
add_library(Helix::RenderPipelines.Physical ALIAS helix_render_pipelines_physical)
set_target_properties(helix_render_pipelines_physical PROPERTIES OUTPUT_NAME HelixRenderPipelinesPhysical)
target_include_directories(helix_render_pipelines_physical PUBLIC "${physical_source}")
helix_apply_options(helix_render_pipelines_physical)
# The energy tables' tool (desktop): writes Source/Helix/RenderPipelines/Physical/Energy/EnergyTables.inl, which the
# pipeline embeds; the tests check the embedded tables against the same integrals.
if(HELIX_PLATFORM STREQUAL "windows" OR HELIX_PLATFORM STREQUAL "linux")
    add_executable(helix_physical_energy_tables "${CMAKE_CURRENT_SOURCE_DIR}/Tools/EnergyTables/EnergyTables.cpp"
                   "${physical_source}/Helix/RenderPipelines/Physical/Energy/Implementation/EnergyIntegration.cpp")
    target_include_directories(helix_physical_energy_tables PRIVATE "${physical_source}")
    target_link_libraries(helix_physical_energy_tables PRIVATE Helix::Core)
    helix_apply_options(helix_physical_energy_tables)
    add_custom_target(helix_physical_energy_tables_update
                      COMMAND helix_physical_energy_tables "${physical_source}/Helix/RenderPipelines/Physical/Energy/EnergyTables.inl"
                      COMMENT "Writing the OpenPBR energy tables")
endif()
# The headers whose tagged types C# sees.
set(physical_bound_headers "${physical_source}/Helix/RenderPipelines/Physical/Settings/PhysicalSettings.h"
    "${physical_source}/Helix/RenderPipelines/Physical/Lighting/LightUnits.h"
    "${physical_source}/Helix/RenderPipelines/Physical/Components/AdditionalData.h"
    "${physical_source}/Helix/RenderPipelines/Physical/Overrides/Overrides.h"
    "${physical_source}/Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipelineAsset.h"
    "${physical_source}/Helix/RenderPipelines/Physical/Pipeline/PhysicalRenderPipeline.h")
# The core's bound headers (its types the pipeline's bindings name).
set(core_references "${core_source}/Helix/RenderPipelines/Core/Volume/VolumeComponent.h" "${core_source}/Helix/RenderPipelines/Core/Volume/VolumeParameter.h"
    "${core_source}/Helix/RenderPipelines/Core/Context/ContextContainer.h")
set(engine_references "${engine_source}/Helix/Engine/Object/EntityId.h" "${engine_source}/Helix/Engine/Object/Object.h"
    "${engine_source}/Helix/Engine/GameObject/GameObject.h" "${engine_source}/Helix/Engine/Script/GameBehaviour.h"
    "${engine_source}/Helix/Engine/Script/ScriptableObject.h" "${engine_source}/Helix/Engine/Transform/Transform.h"
    "${engine_source}/Helix/Engine/Texture/Texture.h" "${engine_source}/Helix/Engine/Material/Material.h" "${engine_source}/Helix/Engine/Shader/Shader.h"
    "${engine_source}/Helix/Engine/Rendering/Camera.h" "${engine_source}/Helix/Engine/Rendering/Light.h"
    "${engine_source}/Helix/Engine/Rendering/Pipeline/RenderStates.h" "${engine_source}/Helix/Engine/Texture/RenderTexture.h"
    "${engine_source}/Helix/Engine/Rendering/SystemInfo.h" "${engine_source}/Helix/Engine/Mesh/Mesh.h"
    "${math_dir}/Source/Helix/Math/Vector.h" "${math_dir}/Source/Helix/Math/Color.h" "${math_dir}/Source/Helix/Math/Matrix4x4.h"
    "${math_dir}/Source/Helix/Math/Geometry.h")
helix_bindgen(helix_render_pipelines_physical MODULE HelixRenderPipelinesPhysical NAMESPACE HelixEngine.Rendering.Physical KIND NativeGame
              HEADERS ${physical_bound_headers} REFERENCES ${engine_references} ${core_references} REFERENCE_NAMESPACE HelixEngine
              INCLUDE_ROOTS "${physical_source}" "${core_source}" "${engine_source}" "${math_dir}/Source" CS_OUTPUT physical_cs)

# The C# side: the generated bindings and the hand written C# in the reference's shape, over HelixEngine.CoreModule
# and the core's runtime.
set(runtime_dir "${physical_source}/Helix.RenderPipelines.Physical.Runtime")
set(runtime_dll "${physical_artifacts}/bin/Helix.RenderPipelines.Physical.Runtime/release/Helix.RenderPipelines.Physical.Runtime.dll")
file(GLOB_RECURSE runtime_sources CONFIGURE_DEPENDS "${runtime_dir}/*.cs" "${runtime_dir}/*.csproj")
list(FILTER runtime_sources EXCLUDE REGEX "/(bin|obj)/")
add_custom_command(
    OUTPUT "${runtime_dll}"
    COMMAND "${HELIX_DOTNET}" build "${runtime_dir}/Helix.RenderPipelines.Physical.Runtime.csproj" -c Release --artifacts-path "${physical_artifacts}"
            --nologo -v quiet "-p:HelixCoreModule=${HELIX_CORE_MODULE}" "-p:HelixScriptingRuntime=${HELIX_SCRIPTING_RUNTIME}"
            "-p:HelixRenderPipelinesCore=${HELIX_RENDER_PIPELINES_CORE}" "-p:HelixModuleBindings=${physical_cs}"
    DEPENDS ${runtime_sources} "${HELIX_CORE_MODULE}" "${HELIX_RENDER_PIPELINES_CORE}" "${physical_cs}"
    COMMENT "Building Helix.RenderPipelines.Physical.Runtime")
add_custom_target(helix_render_pipelines_physical_runtime ALL DEPENDS "${runtime_dll}")
add_dependencies(helix_render_pipelines_physical_runtime helix_core_module helix_render_pipelines_core_runtime helix_render_pipelines_physical)

# The pipeline's C# shaders (with the core's shader library beside them).
set(physical_shaders_bin "${physical_artifacts}/bin/Helix.RenderPipelines.Physical.Shaders/release")
set(physical_shaders_dll "${physical_shaders_bin}/Helix.RenderPipelines.Physical.Shaders.dll")
file(GLOB_RECURSE physical_shader_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.csproj"
     "${core_dir}/ShaderLibrary/*.cs" "${shaders_dir}/Source/*.cs" "${engine_dir}/Shaders/*.cs")
list(FILTER physical_shader_sources EXCLUDE REGEX "/(bin|obj)/")
add_custom_command(
    OUTPUT "${physical_shaders_dll}"
    COMMAND "${HELIX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Helix.RenderPipelines.Physical.Shaders.csproj" -c Release
            --artifacts-path "${physical_artifacts}" --nologo -v quiet "-p:HelixShadersDir=${shaders_dir}" "-p:HelixEngineDir=${engine_dir}"
            "-p:HelixRenderPipelinesCoreDir=${core_dir}"
    DEPENDS ${physical_shader_sources}
    COMMENT "Building the Physical pipeline's C# shaders")
add_custom_target(helix_render_pipelines_physical_shaders ALL DEPENDS "${physical_shaders_dll}")
add_dependencies(helix_render_pipelines_physical helix_render_pipelines_physical_shaders)
set(HELIX_RENDER_PIPELINES_PHYSICAL "${runtime_dll}" CACHE INTERNAL "")
set(HELIX_RENDER_PIPELINES_PHYSICAL_SHADERS "${physical_shaders_dll}" CACHE INTERNAL "")

# Tests: C# test scripts run by a C++ program in the engine, with the engine's test shaders, the core's and the
# pipeline's shaders as assets: the shared engine on the desktop, a .NET web app in node on the Web.
if(HELIX_PLATFORM STREQUAL "windows" OR HELIX_PLATFORM STREQUAL "linux")
    helix_add_test_suite(helix_render_pipelines_physical_tests EXCLUDE "/(Managed|Shaders)/" DEPS Helix::EngineShared Helix::RenderPipelines.Physical)
elseif(HELIX_PLATFORM STREQUAL "web")
    helix_add_test_suite(helix_render_pipelines_physical_tests DOTNET EXCLUDE "/(Managed|Shaders)/" DEPS Helix::Engine Helix::RenderPipelines.Physical)
endif()
if(TARGET helix_render_pipelines_physical_tests)
    set(tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    # The test program's own game module: C++ features and passes; their C# classes are compiled into the C# tests.
    helix_bindgen(helix_render_pipelines_physical_tests MODULE RenderPipelinesPhysicalTests NAMESPACE PhysicalTests KIND NativeGame
                  HEADERS "${tests_dir}/Cpp/CppScripts.h" REFERENCES ${engine_references} ${core_references} ${physical_bound_headers}
                  REFERENCE_NAMESPACE HelixEngine INCLUDE_ROOTS "${tests_dir}" "${physical_source}" "${core_source}" "${engine_source}" "${math_dir}/Source"
                  CS_OUTPUT test_bindings_cs)
    set(tests_dll "${physical_artifacts}/bin/Helix.RenderPipelines.Physical.Tests/release/Helix.RenderPipelines.Physical.Tests.dll")
    get_filename_component(tests_bin "${tests_dll}" DIRECTORY)
    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${tests_dir}/Managed/*.cs" "${tests_dir}/Managed/*.csproj")
    list(FILTER test_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${tests_dll}"
        COMMAND "${HELIX_DOTNET}" build "${tests_dir}/Managed/Helix.RenderPipelines.Physical.Tests.csproj" -c Release --artifacts-path "${physical_artifacts}"
                --nologo -v quiet "-p:HelixCoreModule=${HELIX_CORE_MODULE}" "-p:HelixRenderPipelinesCore=${HELIX_RENDER_PIPELINES_CORE}"
                "-p:HelixRenderPipelinesPhysical=${runtime_dll}" "-p:HelixScriptingRuntime=${HELIX_SCRIPTING_RUNTIME}"
                "-p:HelixModuleBindings=${test_bindings_cs}"
        DEPENDS ${test_sources} "${runtime_dll}" "${test_bindings_cs}"
        COMMENT "Building the C# tests of Helix.RenderPipelines.Physical")
    add_custom_target(helix_render_pipelines_physical_test_scripts DEPENDS "${tests_dll}")
    add_dependencies(helix_render_pipelines_physical_test_scripts helix_render_pipelines_physical_runtime)
    add_dependencies(helix_render_pipelines_physical_tests helix_render_pipelines_physical_test_scripts helix_render_pipelines_physical_shaders
                     helix_render_pipelines_core_shaders)

    # The engine's C# test shaders built against this repo's Helix.Shaders, the core's shaders and shader library, and
    # the pipeline's shaders: embedded, written into an asset folder by the tests.
    set(test_shaders_dir "${physical_artifacts}/bin/HelixEngine.TestShaders/release")
    file(GLOB_RECURSE test_shader_sources CONFIGURE_DEPENDS "${engine_dir}/Tests/Shaders/*.cs" "${engine_dir}/Tests/Shaders/*.csproj"
         "${engine_dir}/Shaders/*.cs" "${engine_dir}/Shaders/*.csproj")
    list(FILTER test_shader_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${test_shaders_dir}/HelixEngine.TestShaders.dll" "${test_shaders_dir}/HelixEngine.TestShaders.pdb"
        COMMAND "${HELIX_DOTNET}" build "${engine_dir}/Tests/Shaders/HelixEngine.TestShaders.csproj" -c Release --artifacts-path "${physical_artifacts}"
                --nologo -v quiet "-p:HelixShadersDir=${shaders_dir}"
        DEPENDS ${test_shader_sources}
        COMMENT "Building the engine's C# test shaders")
    # The pipeline's test shaders (its light modes, its lights and shadows).
    set(physical_test_shaders_dll "${physical_artifacts}/bin/Helix.RenderPipelines.Physical.TestShaders/release/Helix.RenderPipelines.Physical.TestShaders.dll")
    file(GLOB_RECURSE physical_test_shader_sources CONFIGURE_DEPENDS "${tests_dir}/Shaders/*.cs" "${tests_dir}/Shaders/*.csproj")
    list(FILTER physical_test_shader_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${physical_test_shaders_dll}"
        COMMAND "${HELIX_DOTNET}" build "${tests_dir}/Shaders/Helix.RenderPipelines.Physical.TestShaders.csproj" -c Release --artifacts-path "${physical_artifacts}"
                --nologo -v quiet "-p:HelixShadersDir=${shaders_dir}" "-p:HelixEngineDir=${engine_dir}" "-p:HelixRenderPipelinesCoreDir=${core_dir}"
        DEPENDS ${physical_test_shader_sources} "${physical_shaders_dll}"
        COMMENT "Building the Physical pipeline's C# test shaders")
    set(tests_generated "${CMAKE_CURRENT_BINARY_DIR}/Generated")
    set(embedded "${tests_generated}/TestShadersAssembly.h" "${tests_generated}/CoreShadersAssembly.h" "${tests_generated}/CoreShaderLibraryAssembly.h"
        "${tests_generated}/PhysicalShadersAssembly.h" "${tests_generated}/PhysicalTestShadersAssembly.h")
    add_custom_command(
        OUTPUT ${embedded}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${tests_generated}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${test_shaders_dir}/HelixEngine.TestShaders.dll" "-DOUTPUT=${tests_generated}/TestShadersAssembly.h"
                -DNAME=kTestShadersAssembly -P "${HELIX_EMBED_FILE}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${HELIX_RENDER_PIPELINES_CORE_SHADERS}" "-DOUTPUT=${tests_generated}/CoreShadersAssembly.h"
                -DNAME=kCoreShadersAssembly -P "${HELIX_EMBED_FILE}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${HELIX_RENDER_PIPELINES_CORE_SHADER_LIBRARY}" "-DOUTPUT=${tests_generated}/CoreShaderLibraryAssembly.h"
                -DNAME=kCoreShaderLibraryAssembly -P "${HELIX_EMBED_FILE}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${physical_shaders_dll}" "-DOUTPUT=${tests_generated}/PhysicalShadersAssembly.h" -DNAME=kPhysicalShadersAssembly
                -P "${HELIX_EMBED_FILE}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${physical_test_shaders_dll}" "-DOUTPUT=${tests_generated}/PhysicalTestShadersAssembly.h"
                -DNAME=kPhysicalTestShadersAssembly -P "${HELIX_EMBED_FILE}"
        DEPENDS "${test_shaders_dir}/HelixEngine.TestShaders.dll" "${HELIX_RENDER_PIPELINES_CORE_SHADERS}" "${HELIX_RENDER_PIPELINES_CORE_SHADER_LIBRARY}"
                "${physical_shaders_dll}" "${physical_test_shaders_dll}" "${HELIX_EMBED_FILE}"
        COMMENT "Embedding the test shaders, the core's and the pipeline's shaders")
    target_sources(helix_render_pipelines_physical_tests PRIVATE ${embedded})
    target_include_directories(helix_render_pipelines_physical_tests PRIVATE "${tests_dir}" "${tests_generated}" "${engine_dir}/Tests")
    target_compile_definitions(helix_render_pipelines_physical_tests PRIVATE "HX_TEST_RUNTIME_ASSEMBLY=\"${HELIX_SCRIPTING_RUNTIME}\""
                                                                            "HX_TEST_CORE_MODULE=\"${HELIX_CORE_MODULE}\"" "HX_TEST_SCRIPTS=\"${tests_dll}\""
                                                                            "HX_TEST_CORE_RUNTIME=\"${tests_bin}/Helix.RenderPipelines.Core.Runtime.dll\""
                                                                            "HX_TEST_PHYSICAL_RUNTIME=\"${tests_bin}/Helix.RenderPipelines.Physical.Runtime.dll\""
                                                                            "HELIX_ENGINE_GOLDEN_DIR=\"${tests_dir}/Golden\"")
    # The web app's assemblies.
    set_property(TARGET helix_render_pipelines_physical_tests APPEND PROPERTY HELIX_DOTNET_ASSEMBLIES "${HELIX_CORE_MODULE}" ${HELIX_CORE_MODULE_REFERENCES}
                                                                                                      "${HELIX_RENDER_PIPELINES_CORE}" "${runtime_dll}" "${tests_dll}")
    if(TARGET helix_render_pipelines_physical_tests_web)
        add_dependencies(helix_render_pipelines_physical_tests_web helix_render_pipelines_physical_test_scripts helix_core_module)
    endif()
    # Once more on the coreclr starter (as Android hosts the runtime), and with the render thread and persistent blocks.
    if(HELIX_PLATFORM STREQUAL "windows" OR HELIX_PLATFORM STREQUAL "linux")
        add_test(NAME helix_render_pipelines_physical_tests_coreclr COMMAND helix_render_pipelines_physical_tests)
        set_tests_properties(helix_render_pipelines_physical_tests_coreclr PROPERTIES ENVIRONMENT "HELIX_DOTNET_RUNTIME=coreclr")
        add_test(NAME helix_render_pipelines_physical_tests_render_thread COMMAND helix_render_pipelines_physical_tests)
        set_tests_properties(helix_render_pipelines_physical_tests_render_thread PROPERTIES ENVIRONMENT "HELIX_RENDER_THREAD=1;HELIX_RENDER_BATCHING=1")
    endif()
endif()

# The public surface of the runtime against its inventory (Tests/Api/Helix.RenderPipelines.Physical.csv), with Helix.ScriptCore's tool.
if(HELIX_BUILD_TESTS AND PROJECT_IS_TOP_LEVEL AND HELIX_API_INVENTORY)
    add_test(NAME helix_render_pipelines_physical_api COMMAND "${HELIX_DOTNET}" "${HELIX_API_INVENTORY}" --assembly "${runtime_dll}" --reference "${HELIX_CORE_MODULE}"
             --reference "${HELIX_SCRIPTING_RUNTIME}" --reference "${HELIX_RENDER_PIPELINES_CORE}"
             --inventory "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Api/Helix.RenderPipelines.Physical.csv")
    add_dependencies(helix_render_pipelines_physical_runtime helix_api_inventory)
endif()

if(TARGET helix_render_pipelines_physical_tests AND HELIX_ENABLE_ASAN AND HELIX_PLATFORM STREQUAL "linux")
    helix_dependency_dir(scripting_dir Scripting)
    set(suppressions "LSAN_OPTIONS=suppressions=${scripting_dir}/Tests/Support/LeakSuppressions.txt")
    set_tests_properties(helix_render_pipelines_physical_tests PROPERTIES ENVIRONMENT "${suppressions}")
    set_tests_properties(helix_render_pipelines_physical_tests_coreclr PROPERTIES ENVIRONMENT "HELIX_DOTNET_RUNTIME=coreclr;${suppressions}")
    set_tests_properties(helix_render_pipelines_physical_tests_render_thread PROPERTIES ENVIRONMENT "HELIX_RENDER_THREAD=1;HELIX_RENDER_BATCHING=1;${suppressions}")
endif()
