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

# Include any dependencies generated for this target.
include src/CMakeFiles/ruby-format.dir/depend.make

# Include the progress variables for this target.
include src/CMakeFiles/ruby-format.dir/progress.make

# Include the compile flags for this target's objects.
include src/CMakeFiles/ruby-format.dir/flags.make

src/CMakeFiles/ruby-format.dir/ruby_module.o: src/CMakeFiles/ruby-format.dir/flags.make
src/CMakeFiles/ruby-format.dir/ruby_module.o: ../src/ruby_module.c
	$(CMAKE_COMMAND) -E cmake_progress_report /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/CMakeFiles $(CMAKE_PROGRESS_1)
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Building C object src/CMakeFiles/ruby-format.dir/ruby_module.o"
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src && /usr/bin/gcc  $(C_DEFINES) $(C_FLAGS) -o CMakeFiles/ruby-format.dir/ruby_module.o   -c /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/src/ruby_module.c

src/CMakeFiles/ruby-format.dir/ruby_module.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/ruby-format.dir/ruby_module.i"
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src && /usr/bin/gcc  $(C_DEFINES) $(C_FLAGS) -E /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/src/ruby_module.c > CMakeFiles/ruby-format.dir/ruby_module.i

src/CMakeFiles/ruby-format.dir/ruby_module.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/ruby-format.dir/ruby_module.s"
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src && /usr/bin/gcc  $(C_DEFINES) $(C_FLAGS) -S /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/src/ruby_module.c -o CMakeFiles/ruby-format.dir/ruby_module.s

src/CMakeFiles/ruby-format.dir/ruby_module.o.requires:
.PHONY : src/CMakeFiles/ruby-format.dir/ruby_module.o.requires

src/CMakeFiles/ruby-format.dir/ruby_module.o.provides: src/CMakeFiles/ruby-format.dir/ruby_module.o.requires
	$(MAKE) -f src/CMakeFiles/ruby-format.dir/build.make src/CMakeFiles/ruby-format.dir/ruby_module.o.provides.build
.PHONY : src/CMakeFiles/ruby-format.dir/ruby_module.o.provides

src/CMakeFiles/ruby-format.dir/ruby_module.o.provides.build: src/CMakeFiles/ruby-format.dir/ruby_module.o
.PHONY : src/CMakeFiles/ruby-format.dir/ruby_module.o.provides.build

# Object files for target ruby-format
ruby__format_OBJECTS = \
"CMakeFiles/ruby-format.dir/ruby_module.o"

# External object files for target ruby-format
ruby__format_EXTERNAL_OBJECTS =

src/ruby-format.so: src/CMakeFiles/ruby-format.dir/ruby_module.o
src/ruby-format.so: /usr/lib/libruby-1.9.1.so
src/ruby-format.so: src/CMakeFiles/ruby-format.dir/build.make
src/ruby-format.so: src/CMakeFiles/ruby-format.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --red --bold "Linking C shared module ruby-format.so"
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/ruby-format.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
src/CMakeFiles/ruby-format.dir/build: src/ruby-format.so
.PHONY : src/CMakeFiles/ruby-format.dir/build

src/CMakeFiles/ruby-format.dir/requires: src/CMakeFiles/ruby-format.dir/ruby_module.o.requires
.PHONY : src/CMakeFiles/ruby-format.dir/requires

src/CMakeFiles/ruby-format.dir/clean:
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src && $(CMAKE_COMMAND) -P CMakeFiles/ruby-format.dir/cmake_clean.cmake
.PHONY : src/CMakeFiles/ruby-format.dir/clean

src/CMakeFiles/ruby-format.dir/depend:
	cd /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39 /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/src /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src /mnt/usuarios/luizluca/prog/opensync/libopensync-plugin-ruby-0.39/build/src/CMakeFiles/ruby-format.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : src/CMakeFiles/ruby-format.dir/depend

