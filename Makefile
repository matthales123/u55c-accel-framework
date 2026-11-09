SHELL := /usr/bin/env bash

.PHONY: bootstrap check host correctness bench plot hls_xclbin rtl_project

bootstrap:
	./scripts/bootstrap_deps.sh

check:
	./scripts/check_setup.sh

host:
	./scripts/build_host.sh

correctness:
	./scripts/run_correctness.sh

bench:
	./scripts/run_benchmark_sweep.sh

plot:
	python3 host/python/plot_results.py --csv $${CSV:-results/benchmark_latest.csv} --out $${OUT:-results/benchmark_latest.png}

hls_xclbin:
	$(MAKE) -C hardware/hls xclbin PLATFORM=$${U55C_PLATFORM:-xilinx_u55c_gen3x16_xdma_3_202210_1}

rtl_project:
	./hardware/rtl/run_vivado.sh
