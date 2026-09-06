/*
 * Copyright (c) Xiaomi Technologies Co., Ltd. 2020. All rights reserved.
 *
 * File name: oem_binder.c
 * Description: millet-binder-driver
 * Author: guchao1@xiaomi.com
 * Version: 1.0
 * Date:  2020/9/9
 */
#define pr_fmt(fmt) "millet-binder_gki: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/freezer.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/proc_fs.h>
#include <linux/sched/task.h>
#include <uapi/linux/android/binder.h>
#include "millet.h"
#include "binder_oem.h"
#include <trace/hooks/binder.h>

struct oem_binder_hook oem_binder_hook_set = {
	.oem_wahead_thresh = 0,
	.oem_wahead_space = 0,
	.oem_reply_hook = NULL,
	.oem_trans_hook = NULL,
	.oem_wait4_hook = NULL,
	.oem_query_st_hook = NULL,
	.oem_buf_overflow_hook = NULL,
};

/*
 * NoVA binder keeps the binder_proc/thread/transaction structures private to
 * binder.c, so this driver reaches them through the exported oem_binder_*
 * accessors instead of dereferencing them directly.
 */
extern struct task_struct *oem_binder_proc_tsk(struct binder_proc *proc);
extern int oem_binder_thread_pid(struct binder_thread *thread);
extern struct task_struct *oem_binder_alloc_owner(struct binder_alloc *alloc);
extern struct task_struct *oem_binder_wait4_task(struct binder_thread *thread,
						 int *caller_tid, bool *oneway,
						 unsigned int *code);
extern bool oem_binder_uid_procs_idle(int uid);

void query_binder_app_stat(int uid)
{
	enum BINDER_STAT stat;

	if (!oem_binder_hook_set.oem_query_st_hook)
		return;

	stat = oem_binder_uid_procs_idle(uid) ? BINDER_IN_IDLE : BINDER_IN_BUSY;
	oem_binder_hook_set.oem_query_st_hook(uid, current, 0, current->pid, stat);
}
EXPORT_SYMBOL_GPL(query_binder_app_stat);

static struct task_struct *binder_buff_owner(struct binder_alloc *alloc)
{
	return oem_binder_alloc_owner(alloc);
}

void mi_binder_alloc_new_buf_locked(void *data, size_t size,
				    struct binder_alloc *alloc, int is_async)
{
	if (oem_binder_hook_set.oem_buf_overflow_hook && is_async) {
		struct task_struct *owner = binder_buff_owner(alloc);

		if (owner)
			oem_binder_hook_set.oem_buf_overflow_hook(owner, current,
					current->pid, false, 0);
	}
}

void mi_binder_replay(void *data, struct binder_proc *target_proc,
		      struct binder_proc *proc, struct binder_thread *thread,
		      struct binder_transaction_data *tr)
{
	struct task_struct *dst = oem_binder_proc_tsk(target_proc);
	struct task_struct *src = oem_binder_proc_tsk(proc);

	if (oem_binder_hook_set.oem_reply_hook && dst)
		oem_binder_hook_set.oem_reply_hook(dst, src,
				oem_binder_thread_pid(thread),
				tr->flags & TF_ONE_WAY, tr->code);
}

void mi_binder_transaction(void *data, struct binder_proc *target_proc,
			   struct binder_proc *proc, struct binder_thread *thread,
			   struct binder_transaction_data *tr)
{
	struct task_struct *dst = oem_binder_proc_tsk(target_proc);
	struct task_struct *src = oem_binder_proc_tsk(proc);

	if (oem_binder_hook_set.oem_trans_hook && dst)
		oem_binder_hook_set.oem_trans_hook(dst, src,
				oem_binder_thread_pid(thread),
				tr->flags & TF_ONE_WAY, tr->code);
}

void mi_binder_wait_for_work(void *data, bool do_proc_work,
			     struct binder_thread *thread,
			     struct binder_proc *proc)
{
	struct task_struct *dst;
	int caller_tid = 0;
	bool oneway = false;
	unsigned int code = 0;

	dst = oem_binder_wait4_task(thread, &caller_tid, &oneway, &code);
	if (!dst)
		return;

	if (oem_binder_hook_set.oem_wait4_hook)
		oem_binder_hook_set.oem_wait4_hook(dst,
				oem_binder_proc_tsk(proc),
				caller_tid, oneway, code);
	put_task_struct(dst);
}

void oem_register_binder_hook(struct oem_binder_hook *set)
{
	if (!set)
		return;

	oem_binder_hook_set.oem_wahead_thresh = set->oem_wahead_thresh;
	oem_binder_hook_set.oem_wahead_space = set->oem_wahead_space;
	oem_binder_hook_set.oem_reply_hook = set->oem_reply_hook;
	oem_binder_hook_set.oem_trans_hook = set->oem_trans_hook;
	oem_binder_hook_set.oem_wait4_hook = set->oem_wait4_hook;
	oem_binder_hook_set.oem_query_st_hook = set->oem_query_st_hook;
	oem_binder_hook_set.oem_buf_overflow_hook = set->oem_buf_overflow_hook;
}
EXPORT_SYMBOL_GPL(oem_register_binder_hook);

static int __init init_binder_gki(void)
{
	pr_err("enter init_binder_gki func!\n");
	register_trace_android_vh_binder_reply(mi_binder_replay, NULL);
	register_trace_android_vh_binder_trans(mi_binder_transaction, NULL);
	register_trace_android_vh_binder_wait_for_work(mi_binder_wait_for_work, NULL);

	return 0;
}

module_init(init_binder_gki);

MODULE_LICENSE("GPL");