/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "glock.h"
#include "lm.h"
#include "super.h"

/**
 * gfs2_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if 1, don't complain if the FS isn't a GFS2 fs
 *
 * Returns: errno
 */

int gfs2_lm_mount(struct gfs2_sbd *sdp, int silent)
{
	char *proto = sdp->sd_proto_name;
	char *table = sdp->sd_table_name;
	int flags = 0;
	int error;

	if (sdp->sd_args.ar_spectator)
		flags |= LM_MFLAG_SPECTATOR;

	fs_info(sdp, "Trying to join cluster \"%s\", \"%s\"\n", proto, table);

	error = lm_mount(proto, table, sdp->sd_args.ar_hostdata,
			 gfs2_glock_cb, sdp,
			 GFS2_MIN_LVB_SIZE, flags,
			 &sdp->sd_lockstruct, &sdp->sd_kobj);
	if (error) {
		fs_info(sdp, "can't mount proto=%s, table=%s, hostdata=%s\n",
			proto, table, sdp->sd_args.ar_hostdata);
		goto out;
	}

	if (gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_lockspace) ||
	    gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_ops) ||
	    gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_lvb_size >=
				  GFS2_MIN_LVB_SIZE)) {
		lm_unmount(&sdp->sd_lockstruct);
		goto out;
	}

	if (sdp->sd_args.ar_spectator)
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.s", table);
	else
		snprintf(sdp->sd_fsname, GFS2_FSNAME_LEN, "%s.%u", table,
			 sdp->sd_lockstruct.ls_jid);

	fs_info(sdp, "Joined cluster. Now mounting FS...\n");

	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		sdp->sd_args.ar_localflocks = 1;
		sdp->sd_args.ar_localcaching = 1;
	}

 out:
	return error;
}

void gfs2_lm_others_may_mount(struct gfs2_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(sdp->sd_lockstruct.ls_lockspace);
}

void gfs2_lm_unmount(struct gfs2_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		lm_unmount(&sdp->sd_lockstruct);
}

int gfs2_lm_withdraw(struct gfs2_sbd *sdp, char *fmt, ...)
{
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return 0;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	fs_err(sdp, "about to withdraw from the cluster\n");
	if (sdp->sd_args.ar_debug)
		BUG();

	fs_err(sdp, "waiting for outstanding I/O\n");

	/* FIXME: suspend dm device so oustanding bio's complete
	   and all further io requests fail */

	fs_err(sdp, "telling LM to withdraw\n");
	lm_withdraw(&sdp->sd_lockstruct);
	fs_err(sdp, "withdrawn\n");

	return -1;
}

int gfs2_lm_get_lock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		     lm_lock_t **lockp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_get_lock(sdp->sd_lockstruct.ls_lockspace, name, lockp);
	return error;
}

void gfs2_lm_put_lock(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_put_lock(lock);
}

unsigned int gfs2_lm_lock(struct gfs2_sbd *sdp, lm_lock_t *lock,
			  unsigned int cur_state, unsigned int req_state,
			  unsigned int flags)
{
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(lock,
							 cur_state,
							 req_state, flags);
	return ret;
}

unsigned int gfs2_lm_unlock(struct gfs2_sbd *sdp, lm_lock_t *lock,
			    unsigned int cur_state)
{
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret =  sdp->sd_lockstruct.ls_ops->lm_unlock(lock, cur_state);
	return ret;
}

void gfs2_lm_cancel(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_cancel(lock);
}

int gfs2_lm_hold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char **lvbp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_hold_lvb(lock, lvbp);
	return error;
}

void gfs2_lm_unhold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_unhold_lvb(lock, lvb);
}

void gfs2_lm_sync_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_sync_lvb(lock, lvb);
}

int gfs2_lm_plock_get(struct gfs2_sbd *sdp, struct lm_lockname *name,
		      struct file *file, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock_get(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	return error;
}

int gfs2_lm_plock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		  struct file *file, int cmd, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, cmd, fl);
	return error;
}

int gfs2_lm_punlock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		    struct file *file, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_punlock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	return error;
}

void gfs2_lm_recovery_done(struct gfs2_sbd *sdp, unsigned int jid,
			   unsigned int message)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_recovery_done(sdp->sd_lockstruct.ls_lockspace, jid, message);
}

