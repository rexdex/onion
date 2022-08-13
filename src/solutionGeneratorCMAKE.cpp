#include "common.h"
#include "project.h"
#include "projectManifest.h"
#include "utils.h"
#include "externalLibrary.h"
#include "configuration.h"
#include "fileGenerator.h"
#include "fileRepository.h"
#include "solutionGeneratorCMAKE.h"

SolutionGeneratorCMAKE::SolutionGeneratorCMAKE(FileRepository& files, const Configuration& config, std::string_view mainGroup)
    : SolutionGenerator(files, config, mainGroup)
{
    m_files.resolveDirectoryPath("cmake", m_cmakeScriptsPath);
}

static const char* NameCMakeConfiguration(ConfigurationType config)
{    
    switch (config)
    {
        case ConfigurationType::Checked: return "Checked";
        case ConfigurationType::Release: return "Release";
        case ConfigurationType::Debug: return "Debug";
        case ConfigurationType::Final: return "Final";
        default: break;
    }

    return "Release";
}

static std::string EscapePath(fs::path path)
{
    path.make_preferred();

    std::stringstream ss;
    ss << "\"";
    ss << MakeGenericPath(path.u8string());
    ss << "\"";

    return ss.str();
}

bool SolutionGeneratorCMAKE::generateSolution(FileGenerator& gen)
{
    if (!CheckVersion("cmake", "cmake version", "", "3.22.0"))
        return false;

    auto* file = gen.createFile(m_config.derivedSolutionPath / "CMakeLists.txt");
    auto& f = file->content;

    writeln(f, "# Onion Build");
    writeln(f, "# AutoGenerated file. Please DO NOT MODIFY.");
    writeln(f, "");

    const auto projectName = m_rootGroup->name;

    writelnf(f, "project(%s)", projectName.c_str());
    writeln(f, "");
    writeln(f, "cmake_minimum_required(VERSION 2.8.10)");
    writeln(f, "");

    //writeln(f, "#SET(CMAKE_C_COMPILER /usr/bin/gcc)");
    //writeln(f, "#SET(CMAKE_CXX_COMPILER /usr/bin/gcc)");
    //writeln(f, "#set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)");
    //writeln(f, "#set(CMAKE_DISABLE_SOURCE_CHANGES  ON)");

    writeln(f, "set(CMAKE_VERBOSE_MAKEFILE ON)");
    writeln(f, "set(CMAKE_COLOR_MAKEFILE ON)");
    writelnf(f, "set(CMAKE_CONFIGURATION_TYPES \"%s\")", NameCMakeConfiguration(m_config.configuration));
    writeln(f, "set(OpenGL_GL_PREFERENCE \"GLVND\")");
    writelnf(f, "set(CMAKE_MODULE_PATH %s)", EscapePath(m_cmakeScriptsPath).c_str());
    writelnf(f, "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY %s)", EscapePath(m_config.derivedSolutionPath / "lib").c_str());
    writelnf(f, "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY %s)", EscapePath(m_config.derivedSolutionPath / "lib").c_str());
    writelnf(f, "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY %s)", EscapePath(m_config.derivedBinaryPath).c_str());

    //if (solution.platformType == PlatformType.WINDOWS)
    writeln(f, "set_property(GLOBAL PROPERTY USE_FOLDERS ON)");

    writeln(f, "");

    //writeln(f, "#include(cotire)");
    writeln(f, "#include(PrecompiledHeader)");
    writeln(f, "include(OptimizeForArchitecture)"); // Praise OpenSource!
    writeln(f, "");

    for (const auto* p : m_projects)
        if (p->type == ProjectType::SharedLibrary || p->type == ProjectType::StaticLibrary || p->type == ProjectType::Application || p->type == ProjectType::TestApplication)
            writelnf(f, "add_subdirectory(%s)", EscapePath(p->generatedPath).c_str());

    return true;
}

bool SolutionGeneratorCMAKE::generateProjects(FileGenerator& gen)
{
    bool valid = true;

    #pragma omp parallel for
    for (int i=0; i< m_projects.size(); ++i)
    {
        const auto* p = m_projects[i];
        if (p->type == ProjectType::SharedLibrary || p->type == ProjectType::StaticLibrary || p->type == ProjectType::Application || p->type == ProjectType::TestApplication)
        {
            const fs::path projectPath = p->generatedPath / "CMakeLists.txt";

            auto* file = gen.createFile(projectPath);
            valid &= generateProjectFile(p, file->content);
        }
    }

    return valid;
}

void SolutionGeneratorCMAKE::extractSourceRoots(const SolutionProject* project, std::vector<fs::path>& outPaths) const
{
    for (const auto& sourceRoot : m_sourceRoots)
        outPaths.push_back(sourceRoot);

    if (!project->rootPath.empty())
    {
        outPaths.push_back(project->rootPath / "src");
        outPaths.push_back(project->rootPath / "include");
    }

    outPaths.push_back(m_config.derivedSolutionPath / "generated/_shared");
    outPaths.push_back(project->generatedPath);

	for (const auto& path : project->additionalIncludePaths)
		outPaths.push_back(path);
}

bool SolutionGeneratorCMAKE::generateProjectFile(const SolutionProject* p, std::stringstream& f) const
{
    const auto windowsPlatform = (m_config.platform == PlatformType::Windows || m_config.platform == PlatformType::UWP);

	writeln(f, "# Onion Build");
	writeln(f, "# AutoGenerated file. Please DO NOT MODIFY.");
	writeln(f, "");
    writelnf(f, "project(%s)", p->name.c_str());
    writeln(f, "");

    writeln(f, "set(CMAKE_CXX_STANDARD 17)");
    writeln(f, "set(CMAKE_CXX_STANDARD_REQUIRED ON)");
    writeln(f, "set(CMAKE_CXX_EXTENSIONS OFF)");

    writelnf(f, "add_definitions(-DPROJECT_NAME=%s)", p->name.c_str());
    writeln(f, "string(TOUPPER \"${CMAKE_BUILD_TYPE}\" uppercase_CMAKE_BUILD_TYPE)");
    writelnf(f, "set(CMAKE_CONFIGURATION_TYPES \"%s\")", NameCMakeConfiguration(m_config.configuration));

    const bool staticLink = (p->type == ProjectType::StaticLibrary);
    if (staticLink)
    {
        writeln(f, "add_definitions(-DBUILD_AS_LIBS)");
    }
    else
    {
        std::string exportsMacroName = ToUpper(p->name) + "_EXPORTS";
        writelnf(f, "add_definitions(-D%s)", exportsMacroName.c_str());

        if (p->type == ProjectType::SharedLibrary)
            writeln(f, "add_definitions(-DBUILD_DLL)");
    }

    for (const auto* dep : p->allDependencies)
    {
        if (dep->type == ProjectType::SharedLibrary || dep->type == ProjectType::StaticLibrary)
            writelnf(f, "add_definitions(-DHAS_%s)", ToUpper(dep->name).c_str());
    }

    writelnf(f, "set(CMAKE_EXE_LINKER_FLAGS_CHECKED \"${CMAKE_EXE_LINKER_FLAGS_RELEASE}\")");
    writelnf(f, "set(CMAKE_SHARED_LINKER_FLAGS_CHECKED \"${CMAKE_SHARED_LINKER_FLAGS_CHECKED}\")");

    if (m_config.platform == PlatformType::Windows) {
        writeln(f, "set( CMAKE_CXX_FLAGS  \"${CMAKE_CXX_FLAGS} /MP\")");
    } else if (m_config.platform == PlatformType::Linux) {
      
    }

    if (m_config.configuration == ConfigurationType::Debug)
        writeln(f, "set( CMAKE_CXX_FLAGS  \"${CMAKE_CXX_FLAGS} -DBUILD_DEBUG -D_DEBUG -DDEBUG\")");
    else if (m_config.configuration == ConfigurationType::Checked)
        writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -DBUILD_CHECKED -DNDEBUG\")");
    else if (m_config.configuration == ConfigurationType::Release)
        writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -DBUILD_RELEASE -DNDEBUG\")");
    else if (m_config.configuration == ConfigurationType::Final)
        writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -DBUILD_RELEASE -DBUILD_FINAL -DNDEBUG\")");

    if (windowsPlatform)
    {
        writeln(f, "add_definitions(-DUNICODE -D_UNICODE -D_WIN64 -D_WINDOWS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)");
        writeln(f, "add_definitions(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)");
        writeln(f, "add_definitions(-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)");
        writeln(f, "add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)");
        writeln(f, "add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)");

        if (!p->optionUseWindowSubsystem)
            writeln(f, "add_definitions(-DCONSOLE)");
    }
    else
    {
        writeln(f, "set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -pthread\")");

        if (p->optionUseExceptions)
            writeln(f, "set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -fexceptions\")");
        else
            writeln(f, "set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -fno-exceptions\")");

        writeln(f, "set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -g\")");

        if (m_config.configuration == ConfigurationType::Debug)
            writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -O0 -m64 -fstack-protector-all\")");
        else if (m_config.configuration == ConfigurationType::Checked)
            writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -O2 -m64 -fstack-protector-all\")");
        else
            writeln(f, "set( CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -O3 -m64 -fno-stack-protector\")");
    }

    /*if (solutionSetup.solutionType == SolutionType.FINAL)
        writelnf(f, "add_definitions(-DBUILD_FINAL)");
    else
        writelnf(f, "add_definitions(-DBUILD_DEV)");*/

    std::vector<fs::path> paths;
    extractSourceRoots(p, paths);

    writeln(f, "# Project include directories");
    for (const auto& path : paths)
        writelnf(f, "include_directories(%s)", EscapePath(path).c_str());
    writeln(f, "");

    writeln(f, "# Project library includes");
    for (const auto* lib : p->libraryDependencies)
    {
		if (!lib->includePath.empty())
			writelnf(f, "include_directories(%s)", EscapePath(lib->includePath).c_str());

		for (const auto& path : lib->libraryFiles)
			writelnf(f, "link_libraries(%s)", EscapePath(path).c_str());
    }

    /*for (const auto* lib : p->directDependencies)
    {
        if (lib->type == ProjectType::LocalLibrary && lib->optionGlobalInclude)
        {
            const auto path = lib->rootPath / "include";
            writelnf(f, "include_directories(%s)", EscapePath(path).c_str());
        }
    }*/

    writeln(f, "");

    writeln(f, "# Project files");
    for (const auto* pf : p->files)
    {
        //if (fs::is_regular_file(pf->absolutePath) || pf->)
        {
            if (pf->type == ProjectFileType::CppSource)
                writelnf(f, "list(APPEND FILE_SOURCES %s)", EscapePath(pf->absolutePath).c_str());
            else if (pf->type == ProjectFileType::CppHeader)
                writelnf(f, "list(APPEND FILE_HEADERS %s)", EscapePath(pf->absolutePath).c_str());
        }
        /*else
        {
            std::cerr << "Missing file " << pf->absolutePath << " that is referenced in sources, was the file generated ?\n";
            return false;
        }*/
    }
    writeln(f, "");

    /*// get all source files
    if (requiresRTTI())
    {
        writeln(f, "# Generated reflection file");
        writelnf(f, "list(APPEND FILE_HEADERS %s)", solutionSetup.escapePath(localGeneratedPath.resolve("reflection.inl")));
        writeln(f, "");
    }*/

    writeln(f, "# Project output");
    if (p->type == ProjectType::Application || p->type == ProjectType::TestApplication)
    {
        if (p->optionUseWindowSubsystem && m_config.platform == PlatformType::Windows)
			writelnf(f, "add_executable(%s WIN32 ${FILE_SOURCES} ${FILE_HEADERS})", p->name.c_str());
        else
			writelnf(f, "add_executable(%s ${FILE_SOURCES} ${FILE_HEADERS})", p->name.c_str());
    }
    else
    {
        if (staticLink)
            writelnf(f, "add_library(%s ${FILE_SOURCES} ${FILE_HEADERS})", p->name.c_str());
        else
            writelnf(f, "add_library(%s SHARED ${FILE_SOURCES} ${FILE_HEADERS})", p->name.c_str());
    }

    writeln(f, "");

    writeln(f, "# Project dependencies");
    //if (m_config.)
    {
        if (p->type == ProjectType::Application || p->type == ProjectType::TestApplication)
        {
            auto reversedDeps = p->allDependencies;
            std::reverse(reversedDeps.begin(), reversedDeps.end());

            const auto& deps = windowsPlatform ? p->allDependencies : reversedDeps;
            for (const auto* dep : deps)
                writelnf(f, "target_link_libraries(%s %s)", p->name.c_str(), dep->name.c_str());
        }
    }
    /*else
    {        
        auto reversedDeps = p->directDependencies;
        std::reverse(reversedDeps.begin(), reversedDeps.end());

        const auto& deps = windowsPlatform ? p->directDependencies : reversedDeps;
        for (const auto* dep : deps)
            writelnf(f, "target_link_libraries(%s %s)", p->name.c_str(), dep->name.c_str());
    }*/
    writeln(f, "");

    if (m_config.platform == PlatformType::Linux || m_config.platform == PlatformType::Darwin || m_config.platform == PlatformType::DarwinArm)
    {
        writeln(f, "# Hardcoded system libraries");

        std::vector<std::string> extraLibs, extraFrameworks;
        extraLibs.push_back("dl");

        if (m_config.platform == PlatformType::Linux)
            extraLibs.push_back("rt");
        else if (m_config.platform == PlatformType::DarwinArm || m_config.platform == PlatformType::Darwin)
            extraLibs.push_back("stdc++");

        for (const auto* lib : p->libraryDependencies)
        {
            for (const auto &name: lib->additionalSystemLibraries)
                if (!Contains(extraLibs, name))
                    extraLibs.push_back(name);

            for (const auto &name: lib->additionalSystemFrameworks)
                if (!Contains(extraFrameworks, name))
                    extraFrameworks.push_back(name);

        }

        bool first = true;
        std::stringstream libStr;
        for (const auto& name : extraLibs)
        {
            if (!first) libStr << " ";
            libStr << name;
            first = false;
        }

        if (m_config.platform == PlatformType::DarwinArm || m_config.platform == PlatformType::Darwin)
        {
            for (const auto& name : extraFrameworks)
            {
                if (!first) libStr << " ";
                libStr << "\"-framework " << name << "\"";
                first = false;
            }

            if (!extraFrameworks.empty())
                libStr << " objc";
        }

        writelnf(f, "target_link_libraries(%s %s)", p->name.c_str(), libStr.str().c_str());
    }
    else if (m_config.platform == PlatformType::Windows || m_config.platform == PlatformType::UWP)
    {
        writeln(f, "# Precompiled header setup");

        for (const auto* pf : p->files)
        {
            if (pf->type == ProjectFileType::CppSource)
            {
                if (pf->name == "build.cpp" || pf->name == "build.cxx")
                    writelnf(f, "set_source_files_properties(%s PROPERTIES COMPILE_FLAGS \"/Ycbuild.h\")", EscapePath(pf->absolutePath).c_str());
                else if (pf->usePrecompiledHeader)
                    writelnf(f, "set_source_files_properties(%s PROPERTIES COMPILE_FLAGS \"/Yubuild.h\")", EscapePath(pf->absolutePath).c_str());
            }
        }

        //writelnf(f, "add_precompiled_header(%s build.h FORCEINCLUDE)", name);
        //writelnf(f, "set_target_properties(%s PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT \"build.h\")", name);
        //writelnf(f, "cotire(%s)", name);
    }

    writeln(f, "");

    if (p->type == ProjectType::SharedLibrary)
    {
        writeln(f, "# Final copy of DLL to binary folder");
        writelnf(f, "add_custom_command(TARGET %hs POST_BUILD", p->name.c_str());
        writelnf(f, "\tCOMMAND ${CMAKE_COMMAND} -E copy");
#ifdef __APPLE__
        writelnf(f, "\t${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lib%hs.dylib", p->name.c_str());
        writelnf(f, "\t${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib%hs.dylib)", p->name.c_str());
#else
        writelnf(f, "\t${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lib%hs.so", p->name.c_str());
        writelnf(f, "\t${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib%hs.so)", p->name.c_str());
#endif
    }

    return true;
}

