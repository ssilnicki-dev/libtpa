# libtpa TCP-only integration notes

This note summarizes what can be removed/rewired if an embedding application already:

- initializes DPDK EAL and ports itself, and
- runs its own queue poller threads.

## Keep (core TCP datapath)

- `src/tcp/tcp_input.c`, `src/tcp/tcp_output.c`, `src/tcp/tcp_timeout.c`
- `src/sock.c`, `src/event.c`, `src/port_alloc.c`, `src/worker.c`, `src/eth.c`
- `src/dev.c` (or equivalent glue exposing the same queue access helpers)
- `src/neigh.c` + `src/arp.c` (+ `src/ndp.c` if IPv6)
- common support: `src/stats.c`, `src/log.c`, `lib/utils.c`, timer/packet headers

Why: worker loop drives timers + RX parse + TCP output and depends on dev/neigh hooks.

## Safe to sacrifice first (if you do not need the features)

- Config parser framework: `src/cfg.c` (if you hardcode/tune values directly)
- Interactive shell and control plane: `src/shell.c`, `src/ctrl.c`
- Background daemon hook: `src/tpad.c` and the `tpad_init()` path
- Packet fuzzing: `src/pktfuzz/*`
- Archiving / diagnostics persistence: `src/archive.c`, `src/mem_file.c`
- Tracing helpers: `src/trace.c` (if you also remove trace callsites/macros)

## DPDK init to replace

The stock init path is:

- `tpa_init()` calls `dpdk_init(nr_worker)` then `worker_init(nr_worker)`.
- `dpdk_init()` performs cfg parse for `dpdk.*`, runs `rte_eal_init()`, builds mempools, and configures ports/queues.

If your app already owns EAL/ports/queues, replace this with a custom bootstrap that:

1. Skips `dpdk_init()` entirely.
2. Calls/keeps `worker_init(nr_worker)`.
3. Ensures `dev.*` globals/helpers return your pre-created RX/TX queues.
4. Keeps one libtpa worker per queue (worker id == queue id assumption in `worker->queue = id`).

## Poller ownership model

Use your threads as:

1. call `tpa_worker_init()` once per thread
2. repeatedly call `tpa_worker_run(worker)`

This matches libtpa's normal scheduling model while leaving thread creation and CPU pinning under your control.

## Critical coupling to watch

- L2/L3 parsing + ARP handling occurs in `eth_input()`.
- TCP tx path expects `dev_port_txq_enqueue()` / `dev_txq_flush()`.
- Neighbor resolution queue flushing is in worker loop (`flush_neigh_queue`).
- Timers are advanced via worker loop (`timer_process`).

If you rip out modules, keep these contracts intact or provide stubs that preserve semantics.
