# U55C Accelerator Framework

Reusable vector-add example and project scaffold for Alveo U55C: XRT C++ host,
HLS/RTL build paths, Python benchmark tooling and correctness tests.

## Build and run

```bash
./scripts/check_setup.sh
./scripts/build_host.sh
./scripts/run_correctness.sh
make -C hardware/hls xclbin TARGET=hw KERNEL_NAME=vector_add PLATFORM=xilinx_u55c_gen3x16_xdma_3_202210_1
export U55C_XCLBIN="$PWD/build/hardware/hw/vector_add.xclbin"
python3 host/python/run_benchmark.py --mode hw --iterations 20 --sizes 65536 262144
```

The host can build in dry-run mode without XRT. Dry-run success is not hardware
validation. Generated data and historical reports are excluded. The build/run commands above are project-local. Optional shared project
generation and verification orchestration require the private
`fpga-verification-tools` repository.

## Requirements

For FPGA builds, install AMD Vitis/Vivado 2025.1, XRT, and the compatible
`xilinx_u55c_gen3x16_xdma_3_202210_1` platform. Source your installation's
`settings64.sh` and XRT setup script before building. Python 3, a C++ compiler,
CMake and Make are required for the host/tooling. Run commands from this repository.
Keep the checkout path free of spaces for Vitis HLS.

The source includes host programs, HDL/HLS, tests and local web tooling. Generated
builds, debugging sessions, reports and results are excluded from this public copy.
Web applications are local development interfaces with hardware controls; public
Internet demo access is a separate future deployment.
