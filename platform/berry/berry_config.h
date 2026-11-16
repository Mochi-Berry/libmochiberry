/*
 * berry_config.h — compile-time defaults for the MBJS (libmochiberry) port to
 * Berry OS class devices (ESP32, STM32, ...).
 *
 * Override any of these with -D on the command line or before including
 * mbjs_berry.h.
 */

#ifndef MBJS_BERRY_CONFIG_H
#define MBJS_BERRY_CONFIG_H

#ifndef MBJS_BERRY_HEAP_LIMIT
/* Hard budget handed to the engine's arena allocator, in bytes.
 * ESP32-class devices usually have 300-500 KiB of free heap; pick a value
 * your app can live with. 0 = use the platform default (see mbjs_berry.h). */
#define MBJS_BERRY_HEAP_LIMIT (512u * 1024u)
#endif

#ifndef MBJS_BERRY_STACK_LIMIT
/* JS stack budget in bytes (C stack frames the VM may use). */
#define MBJS_BERRY_STACK_LIMIT (256u * 1024u)
#endif

#ifndef MBJS_BERRY_ARENA_BLOCK
/* Arena block size: allocations are carved from blocks of this size. */
#define MBJS_BERRY_ARENA_BLOCK (16u * 1024u)
#endif

#ifndef MBJS_BERRY_GC_THRESHOLD
/* Bytes of JS objects that may be allocated before the internal cycle
 * collector runs. Tune per device; 0 = engine default. */
#define MBJS_BERRY_GC_THRESHOLD (256u * 1024u)
#endif

#ifndef MBJS_BERRY_DUMP_FLAGS
/* 0 = silent. See MBJS_DUMP_* in mbjs.h. */
#define MBJS_BERRY_DUMP_FLAGS 0
#endif

#endif /* MBJS_BERRY_CONFIG_H */
