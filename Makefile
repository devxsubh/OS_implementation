CC := gcc
CFLAGS := -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lm

MODULAR_SRCS := src/main.c src/approx.c src/sim.c src/experiments.c src/tcp_cubic.c

.PHONY: single modular all run clean

single: apex_sim

modular: apex_sim_modular

all: single modular

apex_sim: apex_sim.c
	$(CC) $(CFLAGS) apex_sim.c $(LDFLAGS) -o apex_sim

apex_sim_modular: $(MODULAR_SRCS)
	$(CC) $(CFLAGS) $(MODULAR_SRCS) $(LDFLAGS) -o apex_sim_modular

run: apex_sim
	./apex_sim --experiment all

clean:
	rm -f apex_sim apex_sim_modular
