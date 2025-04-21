/** @file
 *  @brief IP Identification API.
 *
 *  Copyright (c) 2025 aesc silicon
 *
 *  SPDX-License-Identifier: Apache-2.0
 */
#ifndef INCLUDE_DRIVERS_IP_IDENTIFICATION_H_
#define INCLUDE_DRIVERS_IP_IDENTIFICATION_H_
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Each Elements IP core is equiped with a so-called IP identification table
 * at the beginning of each register map. This table helps to identify the
 * underlying hardware.
 *
 * header.id: ID of this IP core.
 * header.length: Total length of the IP identification table. Important to relocate the register map.
 * header.api_version: Version of this IP identification table.
 * version: Defines the version of this IP core with major.minor.patchlevel.
 */

struct elements_ip_id_header {
	uint32_t header;
	uint32_t version;
};

static inline unsigned int ip_id_get_major_version(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return (header->version >> 24) & 0xFF;
}

static inline unsigned int ip_id_get_minor_version(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return (header->version >> 16) & 0xFF;
}

static inline unsigned int ip_id_get_patchlevel(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return header->version & 0xFFFF;
}

static inline unsigned int ip_id_get_api_version(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return (header->header >> 24) & 0xFF;
}

static inline unsigned int ip_id_get_header_length(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return (header->header >> 16) & 0xFF;
}

static inline unsigned int ip_id_get_id(volatile uintptr_t *addr)
{
	volatile struct elements_ip_id_header *header =
		(struct elements_ip_id_header *)addr;
	return header->header & 0xFFFF;
}

static inline uintptr_t ip_id_relocate_driver(volatile uintptr_t *addr)
{
	return (uintptr_t)addr + ip_id_get_header_length(addr);
}

#ifdef __cplusplus
}
#endif
#endif /* INCLUDE_DRIVERS_IP_IDENTIFICATION_H_ */
