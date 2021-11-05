/*
 * Copyright (c) 2021 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <ipc/ipc_rpmsg.h>

static void rpmsg_service_unbind(struct rpmsg_endpoint *ep)
{
	rpmsg_destroy_ept(ep);
}

static void ns_bind_cb(struct rpmsg_device *rdev, const char *name, uint32_t dest)
{
	struct rpmsg_virtio_device *p_rvdev;
	struct ipc_rpmsg_instance *instance;
	struct ipc_rpmsg_ept *ept;
	int err;

	p_rvdev = CONTAINER_OF(rdev, struct rpmsg_virtio_device, rdev);
	instance = CONTAINER_OF(p_rvdev->shpool, struct ipc_rpmsg_instance, shm_pool);

	for (size_t i = 0; i < NUM_ENDPOINTS; i++) {
		ept = &instance->endpoint[i];

		if (strcmp(name, ept->name) == 0) {
			err = rpmsg_create_ept(&ept->ep, rdev, name, RPMSG_ADDR_ANY,
					       dest, instance->cb, rpmsg_service_unbind);
			if (err != 0) {
				return;
			}

			ept->bound = true;
			if (instance->bound_cb) {
				instance->bound_cb(ept);
			}
		}
	}
}

int ipc_rpmsg_register_ept(struct ipc_rpmsg_instance *instance, unsigned int role,
			   struct ipc_rpmsg_ept *ept)
{
	struct rpmsg_device *rdev;

	if (!instance || !ept) {
		return -EINVAL;
	}

	rdev = rpmsg_virtio_get_rpmsg_device(&instance->rvdev);

	if (role == RPMSG_REMOTE) {
		return rpmsg_create_ept(&ept->ep, rdev, ept->name, RPMSG_ADDR_ANY,
					RPMSG_ADDR_ANY, instance->cb, rpmsg_service_unbind);
	}

	return RPMSG_SUCCESS;
}

int ipc_rpmsg_init(struct ipc_rpmsg_instance *instance,
		   unsigned int role,
		   struct metal_io_region *shm_io,
		   struct virtio_device *vdev,
		   void *shb, size_t size,
		   rpmsg_ns_bind_cb p_bind_cb)
{
	rpmsg_ns_bind_cb bind_cb = p_bind_cb;

	if (!instance || !shb) {
		return -EINVAL;
	}

	if (p_bind_cb == NULL) {
		bind_cb = ns_bind_cb;
	}

	if (role == RPMSG_MASTER) {
		rpmsg_virtio_init_shm_pool(&instance->shm_pool, shb, size);
		return rpmsg_init_vdev(&instance->rvdev, vdev, bind_cb,
				       shm_io, &instance->shm_pool);
	} else {
		return rpmsg_init_vdev(&instance->rvdev, vdev, bind_cb, shm_io, NULL);
	}
}
