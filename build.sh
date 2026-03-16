#!/bin/bash
MAKE_PLUGIN="com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.100.202601091506"
GCC_PLUGIN="com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740"
IDE_BASE="/c/ST/STM32CubeIDE_2.1.0/STM32CubeIDE/plugins"

MAKE="${IDE_BASE}/${MAKE_PLUGIN}/tools/bin/make.exe"
GCC_BIN="${IDE_BASE}/${GCC_PLUGIN}/tools/bin"

export PATH="${GCC_BIN}:${PATH}"

BUILD_DIR="/c/Users/berts/Documents/uv390-self_dev/V2/MDUV380_firmware/MDUV380_10W_PLUS_FW"

cd "${BUILD_DIR}" && "${MAKE}" -j4 all 2>&1 | tail -20
