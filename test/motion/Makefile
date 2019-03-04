include ../Common.mk

DATASET_SIZE=MINI_DATASET
DATASET_SIZE_DEF = -D$(DATASET_SIZE)

all: perf ene

perf: base-clang llvmir
ene: base-clang-ene llvmir-ene

base-clang:
	clang++ $(DATASET_SIZE_DEF) $(GCCFIX) -std=c++11 -O3 motion.cpp $(GCCLINKERFIX) -o motion.O3.$(DATASET_SIZE).CPU.CLANG

base-clang-run: perf
	./motion.O3.$(DATASET_SIZE).CPU.CLANG -i frame0.bmp -j frame1.bmp -o frame_motion.bmp

llvmir:
	clang++ -std=c++11 -$(LLVMIROPT) $(VALIDATE) -DLLVMIR  -S $(DATASET_SIZE_DEF) $(GCCFIX) -emit-llvm  motion.cpp -o motion.$(LLVMIROPT).$(DATASET_SIZE).ll

#ENERGY
base-clang-ene:
	clang++ $(MEASURE_ENERGY_FLAG) $(DATASET_SIZE_DEF) $(GCCFIX) -std=c++11 -O3 motion.cpp $(GCCLINKERFIX) $(MEASURE_ENERGY_LINKER_FLAGS) -o motion.O3.$(DATASET_SIZE).CPU.CLANG.ENE

llvmir-ene:
	clang++ -std=c++11 -$(LLVMIROPT) $(VALIDATE) -DLLVMIR -S $(MEASURE_ENERGY_FLAG) $(DATASET_SIZE_DEF) $(GCCFIX) -emit-llvm  motion.cpp -o motion.$(LLVMIROPT).$(DATASET_SIZE).ENE.ll

