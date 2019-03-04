//    Copyright (c) 2019 University of Paderborn 
//                         (Marvin Damschen <marvin.damschen@gullz.de>,
//                          Gavin Vaz <gavin.vaz@uni-paderborn.de>,
//                          Heinrich Riebler <heinrich.riebler@uni-paderborn.de>)

//    Permission is hereby granted, free of charge, to any person obtaining a copy
//    of this software and associated documentation files (the "Software"), to deal
//    in the Software without restriction, including without limitation the rights
//    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//    copies of the Software, and to permit persons to whom the Software is
//    furnished to do so, subject to the following conditions:

//    The above copyright notice and this permission notice shall be included in
//    all copies or substantial portions of the Software.

//    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//    THE SOFTWARE.

#ifndef _MEASURE_ENERGY_H
#define _MEASURE_ENERGY_H

#include "/usr/ampehre/include/ms_measurement.h"
#include "/usr/ampehre/include/ms_cpu_intel_xeon_sandy.h"
#include "/usr/ampehre/include/ms_fpga_maxeler_max3a.h"
#include "/usr/ampehre/include/ms_gpu_nvidia_tesla_kepler.h"
#include "/usr/ampehre/include/ms_mic_intel_knc.h"
#include "/usr/ampehre/include/ms_list.h"

#define MeasureEnergyInit()\
MS_VERSION version = { .major = MS_MAJOR_VERSION, .minor = MS_MINOR_VERSION, .revision = MS_REVISION_VERSION };\
    MS_SYSTEM *ms = ms_init(&version, CPU_GOVERNOR_ONDEMAND, 2000000, 2500000, GPU_FREQUENCY_CUR, IPMI_SET_TIMEOUT, SKIP_PERIODIC, VARIANT_FULL);\
    MS_LIST *m1 = ms_alloc_measurement(ms);\
    ms_set_timer(m1, CPU , 0, 30000000, 1);\
    ms_set_timer(m1, GPU , 0, 30000000, 1);\
    ms_set_timer(m1, FPGA , 0, 100000000, 1);\
    ms_set_timer(m1, SYSTEM , 0, 100000000, 1);\
    ms_set_timer(m1, MIC , 0, 40000000, 1);\
    ms_init_measurement(ms, m1, CPU|GPU|MIC);\
    ms_start_measurement(ms);\

#define MeasureEnergyKernelStart()\
    MS_MEASUREMENT_CPU * cpuMeasurement = ((MS_MEASUREMENT_CPU *) getMeasurement(&m1, CPU));\
    double cpu_kernel_energy = cpuMeasurement->msr_energy_acc[0][PKG] +\
			      cpuMeasurement->msr_energy_acc[1][PKG] +\
			      cpuMeasurement->msr_energy_acc[0][DRAM] +\
			      cpuMeasurement->msr_energy_acc[1][DRAM];\
    MS_MEASUREMENT_GPU * gpuMeasurement = ((MS_MEASUREMENT_GPU *) getMeasurement(&m1, GPU));\
    double gpu_kernel_energy = gpuMeasurement->nvml_energy_acc;\
    MS_MEASUREMENT_MIC * micMeasurement = ((MS_MEASUREMENT_MIC *) getMeasurement(&m1, MIC));\
    double mic_kernel_energy = micMeasurement->mic_energy_acc[MIC_POWER];

#define MeasureEnergyKernelStop()\
    cpu_kernel_energy = cpuMeasurement->msr_energy_acc[0][PKG] +\
			    cpuMeasurement->msr_energy_acc[1][PKG] +\
			    cpuMeasurement->msr_energy_acc[0][DRAM] +\
			    cpuMeasurement->msr_energy_acc[1][DRAM] -\
			    cpu_kernel_energy;\
    gpu_kernel_energy = gpuMeasurement->nvml_energy_acc - gpu_kernel_energy;\
    mic_kernel_energy = micMeasurement->mic_energy_acc[MIC_POWER] - mic_kernel_energy;\
    std::cout.precision(2);\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP kernel CPU : " << cpu_kernel_energy/1000.0;\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP kernel GPU : " << gpu_kernel_energy/1000.0;\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP kernel PHI : " << mic_kernel_energy/1000.0;\
    std::cout.flush();

#define MeasureEnergyFin()\
    ms_stop_measurement(ms);\
    ms_join_measurement(ms);\
    ms_fini_measurement(ms);\
    double cpu_energy =  cpu_energy_total_pkg(m1, 0) + cpu_energy_total_pkg(m1, 1) + cpu_energy_total_dram(m1, 0) + cpu_energy_total_dram(m1, 1);\
    double gpu_energy = gpu_energy_total(m1);\
    double mic_energy = mic_energy_total_power_usage(m1);\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP total CPU : " << cpu_energy/1000.0;\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP total GPU : " << gpu_energy/1000.0 ;\
    std::cout <<std::fixed<< "\nMEASURE-POWER: INAPP total PHI : " << mic_energy/1000.0;\
    std::cout <<std::fixed<< "\nPOWER: (WattSecond|microseconds)";\
    std::cout.flush();\
    ms_free_measurement(m1);\
    ms_fini(ms);

#endif                          /* _MEASURE_ENERGY_H */
