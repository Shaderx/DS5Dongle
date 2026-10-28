/* __macro_flash_start uses the same SDK offset as the config and BT stores. */
ASSERT(__flash_binary_end <= __macro_flash_start, "Firmware overlaps the persistent macro slots")
/* Known audio/queue allocation requests total 84,685 bytes. Reserve additional
   space for allocator metadata, controller features and transient buffers. */
ASSERT(__HeapLimit - __end__ >= 96 * 1024, "Macro firmware leaves insufficient runtime heap")
