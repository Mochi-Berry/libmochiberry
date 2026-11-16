/*
 * baremetal_config.h — compile-time defaults for the MBJS (libmochiberry)
 * bare-metal / custom-OS port.
 *
 * On bare metal there is no OS to ask for memory. The engine's entire memory
 * budget is a caller-owned RAM region (the "memory management kosongan":
 * a naked bump arena, no malloc, no GC for the C side). Override any of these
 * with -D on the command line or before including mbjs_baremetal.h.
 */

#ifndef MBJS_BAREMETAL_CONFIG_H
#define MBJS_BAREMETAL_CONFIG_H

/* Minimum RAM region the bootstrap will accept (arena block header + one
 * allocation). 8 KiB is a safe floor. */
#ifndef MBJS_BAREMETAL_MIN_REGION
#define MBJS_BAREMETAL_MIN_REGION (8u * 1024u)
#endif

/* Default JS heap budget carved out of the region, in bytes. 0 = whole
 * region (minus the arena's own accounting). */
#ifndef MBJS_BAREMETAL_HEAP_LIMIT
#define MBJS_BAREMETAL_HEAP_LIMIT 0
#endif

/* JS stack limit in bytes (C stack frames the VM may use). */
#ifndef MBJS_BAREMETAL_STACK_LIMIT
#define MBJS_BAREMETAL_STACK_LIMIT (64u * 1024u)
#endif

/* Optional: name of the entry symbol called after mbjs_baremetal_init().
 * The demo app registers it with mbjs_baremetal_set_bootstrap(). */
#ifndef MBJS_BAREMETAL_BOOTSTRAP
#define MBJS_BAREMETAL_BOOTSTRAP mbjs_baremetal_bootstrap
#endif

#endif /* MBJS_BAREMETAL_CONFIG_H */
