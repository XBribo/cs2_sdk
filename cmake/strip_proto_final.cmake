# Allow PlayerCommand to inherit the generated CSGOUserCmdPB.
file(GLOB headers "${GENERATED_DIR}/*.pb.h")
foreach(header IN LISTS headers)
    file(READ "${header}" content)
    string(REPLACE " final :" " :" content "${content}")
    file(WRITE "${header}" "${content}")
endforeach()
