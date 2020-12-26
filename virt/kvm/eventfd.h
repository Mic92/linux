/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_EVENTFD_H__
#define __KVM_EVENTFD_H__

#ifdef CONFIG_KVM_IOREGION
bool kvm_eventfd_collides(struct kvm *kvm, int bus_idx, u64 start, u64 size);
#else
static inline bool
kvm_eventfd_collides(struct kvm *kvm, int bus_idx, u64 start, u64 size)
{
	return false;
}
#endif
#endif
