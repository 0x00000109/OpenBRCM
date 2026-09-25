# SPDX-License-Identifier: GPL-2.0-only
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -I. -I../../src
SRC     := ../../src

.PHONY: run clean
run: ob_math_test ob_caps_test ob_dma_test ob_irq_test ob_rx_test \
     ob_fw_test ob_ucode_test ob_initvals_test ob_d3a0_test ob_d3a1_test
	./ob_math_test
	./ob_caps_test
	./ob_dma_test
	./ob_irq_test
	./ob_rx_test
	./ob_fw_test
	./ob_ucode_test
	./ob_initvals_test
	./ob_d3a0_test
	./ob_d3a1_test
	python3 test_sprom11_decode.py
	python3 test_srom_var_table.py

ob_math_test: ob_math_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_caps_test: ob_caps_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_dma_test: ob_dma_test.c $(SRC)/ob_dma.h
	$(CC) $(CFLAGS) -o $@ $<

ob_irq_test: ob_irq_test.c $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_rx_test: ob_rx_test.c $(SRC)/ob_rx.h $(SRC)/ob_dma.h
	$(CC) $(CFLAGS) -o $@ $<

ob_fw_test: ob_fw_test.c $(SRC)/ob_fw.h
	$(CC) $(CFLAGS) -o $@ $<

ob_ucode_test: ob_ucode_test.c $(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_initvals_test: ob_initvals_test.c $(SRC)/ob_initvals.h $(SRC)/ob_fw.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d3a0_test: ob_d3a0_test.c $(SRC)/ob_d3a0.h $(SRC)/ob_rx.h \
		$(SRC)/ob_dma.h $(SRC)/ob_ucode.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d3a1_test: ob_d3a1_test.c $(SRC)/ob_d3a1.h $(SRC)/ob_d3a0.h \
		$(SRC)/ob_rx.h $(SRC)/ob_dma.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_initvals.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ob_math_test ob_caps_test ob_dma_test ob_irq_test ob_rx_test \
	      ob_fw_test ob_ucode_test ob_initvals_test ob_d3a0_test \
	      ob_d3a1_test
