# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 2.8

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canoncical targets will work.
.SUFFIXES:

# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list

# Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build

# Utility rule file for ExperimentalCoverage.

CMakeFiles/ExperimentalCoverage:
	/usr/bin/ctest -D ExperimentalCoverage

ExperimentalCoverage: CMakeFiles/ExperimentalCoverage
ExperimentalCoverage: CMakeFiles/ExperimentalCoverage.dir/build.make
.PHONY : ExperimentalCoverage

# Rule to build all files generated by this target.
CMakeFiles/ExperimentalCoverage.dir/build: ExperimentalCoverage
.PHONY : CMakeFiles/ExperimentalCoverage.dir/build

CMakeFiles/ExperimentalCoverage.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/ExperimentalCoverage.dir/cmake_clean.cmake
.PHONY : CMakeFiles/ExperimentalCoverage.dir/clean

CMakeFiles/ExperimentalCoverage.dir/depend:
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39 /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39 /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/CMakeFiles/ExperimentalCoverage.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/ExperimentalCoverage.dir/depend

