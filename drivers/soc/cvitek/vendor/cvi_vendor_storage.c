// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/soc/cvitek/cvi_vendor_storage.h>

int (*_vendor_read)(u32 id, void *pbuf, u32 size) = NULL;
int (*_vendor_write)(u32 id, void *pbuf, u32 size) = NULL;

int cvi_vendor_read(u32 id, void *pbuf, u32 size)
{
	if (_vendor_read)
		return _vendor_read(id, pbuf, size);
	return -1;
}
EXPORT_SYMBOL(cvi_vendor_read);

int cvi_vendor_write(u32 id, void *pbuf, u32 size)
{
	if (_vendor_write)
		return _vendor_write(id, pbuf, size);
	return -1;
}
EXPORT_SYMBOL(cvi_vendor_write);

int cvi_vendor_register(void *read, void *write)
{
	if (!_vendor_read) {
		_vendor_read = read;
		_vendor_write =  write;
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(cvi_vendor_register);

bool is_cvi_vendor_ready(void)
{
	if (_vendor_read && _vendor_write)
		return true;
	return false;
}
EXPORT_SYMBOL(is_cvi_vendor_ready);

MODULE_LICENSE("GPL");
