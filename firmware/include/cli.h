/**
 * CHRONOS-Rb Command Line Interface
 *
 * USB UART CLI for device configuration and status
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize CLI */
void cli_init(void);

/* Process CLI input - call from main loop */
void cli_task(void);

/**
 * Execute a CLI command and capture output to buffer
 * @param cmd Command string to execute
 * @param out_buf Buffer to write output (NULL to use printf)
 * @param out_len Size of output buffer
 * @return Number of characters written to buffer, or 0 if using printf
 */
int cli_execute(const char *cmd, char *out_buf, size_t out_len);

#endif /* CLI_H */
