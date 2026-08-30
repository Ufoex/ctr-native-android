# CMake generated Testfile for 
# Source directory: /home/radityar/ctr/ctr-native-android/.claude/worktrees/ctrds-bottom-screen
# Build directory: /home/radityar/ctr/ctr-native-android/.claude/worktrees/ctrds-bottom-screen/build64
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("ctr_native_version" "/home/radityar/ctr/ctr-native-android/.claude/worktrees/ctrds-bottom-screen/build64/ctr_native" "--version")
set_tests_properties("ctr_native_version" PROPERTIES  PASS_REGULAR_EXPRESSION "^CTR Native " _BACKTRACE_TRIPLES "/home/radityar/ctr/ctr-native-android/.claude/worktrees/ctrds-bottom-screen/CMakeLists.txt;194;add_test;/home/radityar/ctr/ctr-native-android/.claude/worktrees/ctrds-bottom-screen/CMakeLists.txt;0;")
subdirs("externals/SDL")
