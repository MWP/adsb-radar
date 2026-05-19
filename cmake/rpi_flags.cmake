if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(STATUS "AArch64 target: enabling ARMv8-A SIMD flags")
    # Use cortex-a76 for RPi5; cortex-a72 for RPi4
    # Override via: cmake -DRPI_CPU=cortex-a76 ..
    if(NOT DEFINED RPI_CPU)
        set(RPI_CPU "cortex-a72")
    endif()
    target_compile_options(adsb-radar PRIVATE
        -march=armv8-a+crc+simd
        -mtune=${RPI_CPU}
        -ffast-math
        -funroll-loops
    )
    message(STATUS "Tuning for CPU: ${RPI_CPU}")
endif()
