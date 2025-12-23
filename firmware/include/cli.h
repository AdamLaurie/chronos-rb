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

#endif /* CLI_H */
