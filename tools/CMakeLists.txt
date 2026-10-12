add_executable(analyze_quality analyze_quality.cpp)
target_include_directories(analyze_quality PRIVATE ".")
target_compile_features(analyze_quality PRIVATE cxx_std_17)
