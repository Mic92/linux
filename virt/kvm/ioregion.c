// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <kvm/iodev.h>
#include <linux/kernel.h>
#include "eventfd.h"

/* Wire protocol */
struct ioregionfd_cmd {
	__u32 info;
	__u32 padding;
	__u64 user_data;
	__u64 offset;
	__u64 data;
};

struct ioregionfd_resp {
	__u64 data;
	__u8 pad[24];
};

#define IOREGIONFD_CMD_READ    0
#define IOREGIONFD_CMD_WRITE   1

#define IOREGIONFD_SIZE_8BIT   0
#define IOREGIONFD_SIZE_16BIT  1
#define IOREGIONFD_SIZE_32BIT  2
#define IOREGIONFD_SIZE_64BIT  3

#define IOREGIONFD_SIZE_OFFSET 4
#define IOREGIONFD_RESP_OFFSET 6
#define IOREGIONFD_SIZE(x) ((x) << IOREGIONFD_SIZE_OFFSET)
#define IOREGIONFD_RESP(x) ((x) << IOREGIONFD_RESP_OFFSET)

void
kvm_ioregionfd_init(struct kvm *kvm)
{
	INIT_LIST_HEAD(&kvm->ioregions_mmio);
	INIT_LIST_HEAD(&kvm->ioregions_pio);
}

struct ioregion {
	struct list_head     list;
	u64                  paddr;
	u64                  size;
	struct file         *rf;
	struct file         *wf;
	u64                  user_data;
	struct kvm_io_device dev;
	bool                 posted_writes;
};

static inline struct ioregion *
to_ioregion(struct kvm_io_device *dev)
{
	return container_of(dev, struct ioregion, dev);
}

/* assumes kvm->slots_lock held */
static void
ioregion_release(struct ioregion *p)
{
	fput(p->rf);
	fput(p->wf);
	list_del(&p->list);
	kfree(p);
}

static bool
pack_cmd(struct ioregionfd_cmd *cmd, u64 offset, u64 len, int opt, bool resp,
	 u64 user_data, const void *val)
{
	u64 size = 0;

	switch (len) {
	case 1:
		size = IOREGIONFD_SIZE_8BIT;
		*((u8 *)&cmd->data) = val ? *(u8 *)val : 0;
		break;
	case 2:
		size = IOREGIONFD_SIZE_16BIT;
		*((u16 *)&cmd->data) = val ? *(u16 *)val : 0;
		break;
	case 4:
		size = IOREGIONFD_SIZE_32BIT;
		*((u32 *)&cmd->data) = val ? *(u32 *)val : 0;
		break;
	case 8:
		size = IOREGIONFD_SIZE_64BIT;
		*((u64 *)&cmd->data) = val ? *(u64 *)val : 0;
		break;
	default:
		return false;
	}
	cmd->user_data = user_data;
	cmd->offset = offset;
	cmd->info |= opt;
	cmd->info |= IOREGIONFD_SIZE(size);
	cmd->info |= IOREGIONFD_RESP(resp);

	return true;
}

static int
ioregion_read(struct kvm_vcpu *vcpu, struct kvm_io_device *this, gpa_t addr,
	      int len, void *val)
{
	struct ioregion *p = to_ioregion(this);
	struct ioregionfd_cmd *cmd;
	struct ioregionfd_resp *resp;
	size_t buf_size;
	void *buf;
	int ret = 0;

	if ((p->rf->f_flags & O_NONBLOCK) || (p->wf->f_flags & O_NONBLOCK))
		return -EINVAL;
	if ((addr + len - 1) > (p->paddr + p->size - 1))
		return -EINVAL;

	buf_size = max_t(size_t, sizeof(*cmd), sizeof(*resp));
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	cmd = (struct ioregionfd_cmd *)buf;
	resp = (struct ioregionfd_resp *)buf;
	if (!pack_cmd(cmd, addr - p->paddr, len, IOREGIONFD_CMD_READ,
		      1, p->user_data, NULL)) {
		kfree(buf);
		return -EOPNOTSUPP;
	}

	ret = kernel_write(p->wf, cmd, sizeof(*cmd), 0);
	if (ret != sizeof(*cmd)) {
		kfree(buf);
		return (ret < 0) ? ret : -EIO;
	}
	memset(buf, 0, buf_size);
	ret = kernel_read(p->rf, resp, sizeof(*resp), 0);
	if (ret != sizeof(*resp)) {
		kfree(buf);
		return (ret < 0) ? ret : -EIO;
	}

	switch (len) {
	case 1:
		*(u8 *)val = (u8)resp->data;
		break;
	case 2:
		*(u16 *)val = (u16)resp->data;
		break;
	case 4:
		*(u32 *)val = (u32)resp->data;
		break;
	case 8:
		*(u64 *)val = (u64)resp->data;
		break;
	default:
		break;
	}

	kfree(buf);

	return 0;
}

static int
ioregion_write(struct kvm_vcpu *vcpu, struct kvm_io_device *this, gpa_t addr,
		int len, const void *val)
{
	struct ioregion *p = to_ioregion(this);
	struct ioregionfd_cmd *cmd;
	struct ioregionfd_resp *resp;
	size_t buf_size = 0;
	void *buf;
	int ret = 0;

	if ((p->rf->f_flags & O_NONBLOCK) || (p->wf->f_flags & O_NONBLOCK))
		return -EINVAL;
	if ((addr + len - 1) > (p->paddr + p->size - 1))
		return -EINVAL;

	buf_size = max_t(size_t, sizeof(*cmd), sizeof(*resp));
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	cmd = (struct ioregionfd_cmd *)buf;
	if (!pack_cmd(cmd, addr - p->paddr, len, IOREGIONFD_CMD_WRITE,
		      p->posted_writes ? 0 : 1, p->user_data, val)) {
		kfree(buf);
		return -EOPNOTSUPP;
	}

	ret = kernel_write(p->wf, cmd, sizeof(*cmd), 0);
	if (ret != sizeof(*cmd)) {
		kfree(buf);
		return (ret < 0) ? ret : -EIO;
	}

	if (!p->posted_writes) {
		memset(buf, 0, buf_size);
		resp = (struct ioregionfd_resp *)buf;
		ret = kernel_read(p->rf, resp, sizeof(*resp), 0);
		if (ret != sizeof(*resp)) {
			kfree(buf);
			return (ret < 0) ? ret : -EIO;
		}
	}
	kfree(buf);

	return 0;
}

/*
 * This function is called as KVM is completely shutting down.  We do not
 * need to worry about locking just nuke anything we have as quickly as possible
 */
static void
ioregion_destructor(struct kvm_io_device *this)
{
	struct ioregion *p = to_ioregion(this);

	ioregion_release(p);
}

static const struct kvm_io_device_ops ioregion_ops = {
	.read       = ioregion_read,
	.write      = ioregion_write,
	.destructor = ioregion_destructor,
};

static inline struct list_head *
get_ioregion_list(struct kvm *kvm, enum kvm_bus bus_idx)
{
	return (bus_idx == KVM_MMIO_BUS) ?
		&kvm->ioregions_mmio : &kvm->ioregions_pio;
}

/* check for not overlapping case and reverse */
inline bool
overlap(u64 start1, u64 size1, u64 start2, u64 size2)
{
	u64 end1 = start1 + size1 - 1;
	u64 end2 = start2 + size2 - 1;

	return !(end1 < start2 || start1 >= end2);
}

/* assumes kvm->slots_lock held */
bool
kvm_ioregion_collides(struct kvm *kvm, int bus_idx,
		      u64 start, u64 size)
{
	struct ioregion *_p;
	struct list_head *ioregions;

	ioregions = get_ioregion_list(kvm, bus_idx);
	list_for_each_entry(_p, ioregions, list)
		if (overlap(start, size, _p->paddr, _p->size))
			return true;

	return false;
}

/* assumes kvm->slots_lock held */
static bool
ioregion_collision(struct kvm *kvm, struct ioregion *p, enum kvm_bus bus_idx)
{
	if (kvm_ioregion_collides(kvm, bus_idx, p->paddr, p->size) ||
	    kvm_eventfd_collides(kvm, bus_idx, p->paddr, p->size))
		return true;

	return false;
}

static enum kvm_bus
get_bus_from_flags(__u32 flags)
{
	if (flags & KVM_IOREGION_PIO)
		return KVM_PIO_BUS;
	return KVM_MMIO_BUS;
}

int
kvm_set_ioregion(struct kvm *kvm, struct kvm_ioregion *args)
{
	struct ioregion *p;
	bool is_posted_writes;
	struct file *rfile, *wfile;
	enum kvm_bus bus_idx;
	int ret = 0;

	if (!args->memory_size)
		return -EINVAL;
	if ((args->guest_paddr + args->memory_size - 1) < args->guest_paddr)
		return -EINVAL;
	if (args->flags & ~KVM_IOREGION_VALID_FLAG_MASK)
		return -EINVAL;

	rfile = fget(args->rfd);
	if (!rfile)
		return -EBADF;
	wfile = fget(args->wfd);
	if (!wfile) {
		fput(rfile);
		return -EBADF;
	}
	if ((rfile->f_flags & O_NONBLOCK) || (wfile->f_flags & O_NONBLOCK)) {
		ret = -EINVAL;
		goto fail;
	}
	p = kzalloc(sizeof(*p), GFP_KERNEL_ACCOUNT);
	if (!p) {
		ret = -ENOMEM;
		goto fail;
	}

	INIT_LIST_HEAD(&p->list);
	p->paddr = args->guest_paddr;
	p->size = args->memory_size;
	p->user_data = args->user_data;
	p->rf = rfile;
	p->wf = wfile;
	is_posted_writes = args->flags & KVM_IOREGION_POSTED_WRITES;
	p->posted_writes = is_posted_writes ? true : false;
	bus_idx = get_bus_from_flags(args->flags);

	mutex_lock(&kvm->slots_lock);

	if (ioregion_collision(kvm, p, bus_idx)) {
		ret = -EEXIST;
		goto unlock_fail;
	}
	kvm_iodevice_init(&p->dev, &ioregion_ops);
	ret = kvm_io_bus_register_dev(kvm, bus_idx, p->paddr, p->size,
				      &p->dev);
	if (ret < 0)
		goto unlock_fail;
	kvm_get_bus(kvm, bus_idx)->ioregionfd_count++;
	list_add_tail(&p->list, get_ioregion_list(kvm, bus_idx));

	mutex_unlock(&kvm->slots_lock);

	return 0;

unlock_fail:
	mutex_unlock(&kvm->slots_lock);
	kfree(p);
fail:
	fput(rfile);
	fput(wfile);
	return ret;
}

static int
kvm_rm_ioregion(struct kvm *kvm, struct kvm_ioregion *args)
{
	struct ioregion         *p, *tmp;
	struct kvm_io_bus       *bus;
	enum kvm_bus             bus_idx;
	int                      ret = -ENOENT;
	struct list_head        *ioregions;

	if (args->rfd != -1 || args->wfd != -1)
		return -EINVAL;

	bus_idx = get_bus_from_flags(args->flags);
	ioregions = get_ioregion_list(kvm, bus_idx);

	mutex_lock(&kvm->slots_lock);

	list_for_each_entry_safe(p, tmp, ioregions, list) {
		if (p->paddr == args->guest_paddr  &&
		    p->size == args->memory_size) {
			kvm_io_bus_unregister_dev(kvm, bus_idx, &p->dev);
			bus = kvm_get_bus(kvm, bus_idx);
			if (bus)
				bus->ioregionfd_count--;
			ioregion_release(p);
			ret = 0;
			break;
		}
	}

	mutex_unlock(&kvm->slots_lock);

	return ret;
}

int
kvm_ioregionfd(struct kvm *kvm, struct kvm_ioregion *args)
{
	if (args->rfd == -1 || args->wfd == -1)
		return kvm_rm_ioregion(kvm, args);
	return kvm_set_ioregion(kvm, args);
}
