LLVMIROPT=O0

MEASURE_ENERGY_FLAG=-DMEASURE_ENERGY
MEASURE_ENERGY_LINKER_FLAGS=-L/usr/ampehre/lib/ -lms_common -lms_common_apapi

LIB_POLLY_DIR=
APP_FLAGS=-DMEASURE
# Setup googletest here.
GTEST_SRC=
GTEST_BUILD=

#comment this out for final eval
VALIDATE=-DVALIDATE -isystem ${GTEST_SRC}/include

