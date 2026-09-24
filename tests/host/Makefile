# SPDX-License-Identifier: GPL-2.0-only
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -I. -I../../src
SRC     := ../../src

.PHONY: run clean
run: ob_math_test ob_caps_test ob_dma_test
	./ob_math_test
	./ob_caps_test
	./ob_dma_test

ob_math_test: ob_math_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_caps_test: ob_caps_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_dma_test: ob_dma_test.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f ob_math_test ob_caps_test ob_dma_test
