p="/work/spiral303/vendor/JUCE/extras/Build/CMake/JUCEUtils.cmake"
s=open(p).read()
needle='''    if(CMAKE_RC_COMPILER)
        list(APPEND PASSTHROUGH_ARGS "-DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}")
    endif()
'''
addition=needle+'''    list(APPEND PASSTHROUGH_ARGS "-DCMAKE_BUILD_TYPE=Release")
    list(APPEND PASSTHROUGH_ARGS "-DCMAKE_CXX_COMPILER_FORCED=TRUE")
    list(APPEND PASSTHROUGH_ARGS "-DCMAKE_C_COMPILER_FORCED=TRUE")
'''
if s.count("CMAKE_CXX_COMPILER_FORCED=TRUE")==0:
    assert s.count(needle)==1
    s=s.replace(needle,addition,1)
open(p,"w").write(s)
print("ok",s.count("CMAKE_CXX_COMPILER_FORCED=TRUE"))