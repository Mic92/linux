/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_IOREGION_H__
#define __KVM_IOREGION_H__

#ifdef CONFIG_KVM_IOREGION
inline bool overlap(u64 start1, u64 size1, u64 start2, u64 size2);
bool kvm_ioregion_collides(struct kvm *kvm, int bus_idx, u64 start, u64 size);
#else
static inline bool
kvm_ioregion_collides(struct kvm *kvm, int bus_idx, u64 start, u64 size)
{
	return false;
}
#endif
#endif
