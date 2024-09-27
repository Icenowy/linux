/*
 * Copyright (C) 2022 Alibaba Group. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __RSVMEM_POOL_H_
#define __RSVMEM_POOL_H_

#define MAX_RSVMEM_REGION_COUNT 16
typedef struct rsvmem_pool_info
{
    struct gen_pool *pool;   // NULL means unavalible
    resource_size_t base;
    resource_size_t size;
    bool res_marked_nosave;
} rsvmem_pool_info_t;

int rsvmem_pool_create(struct device *dev);
void rsvmem_pool_destroy(void);
unsigned long rsvmem_pool_alloc(int region_id, size_t size);
void rsvmem_pool_free(int region_id, size_t size, unsigned long addr);

#endif /* __RSVMEM_POOL_H_ */
