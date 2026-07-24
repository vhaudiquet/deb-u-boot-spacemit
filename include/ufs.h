/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _UFS_H
#define _UFS_H

struct udevice;

/**
 * ufs_probe() - initialize all devices in the UFS uclass
 *
 * Return: 0 if Ok, -ve on error
 */
int ufs_probe(void);

/**
 * ufs_probe_dev() - initialize a particular device in the UFS uclass
 *
 * @index: index in the uclass sequence
 *
 * Return: 0 if successfully probed, -ve on error
 */
int ufs_probe_dev(int index);

/**
 * ufs_prepare_dev_for_flash() - prepare a UFS device for flashing
 *
 * The default implementation only probes the controller. Platform drivers may
 * override this to enforce provisioning requirements before flashing starts.
 *
 * @index: index in the UFS uclass sequence
 *
 * Return: 0 if the device is ready for flashing, -ve on error
 */
int ufs_prepare_dev_for_flash(int index);

/*
 * ufs_scsi_bind() - Create a new scsi device as a child of the UFS device and
 *		     bind it to the ufs_scsi driver
 * @ufs_dev: UFS device
 * @scsi_devp: Pointer to scsi device
 *
 * Return: 0 if Ok, -ve on error
 */
int ufs_scsi_bind(struct udevice *ufs_dev, struct udevice **scsi_devp);
#endif
