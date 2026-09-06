/*
 * binder_prio - MediaTek begonia binder priority optimization
 *
 * Adapted from Xiaomi's "binder_prio" (sheng-u-oss / bsp-klimt-v-oss
 * backports, commits 4f6c11e794a00529f1c2e6b3ed74333e26149511 and
 * bb0ec80ce647c533d8e4ce60851c7dc674465f6e) for the begonia 4.14.356
 * kernel. The Qualcomm display-composer-specific bits were removed;
 * only MIUI/Android process name matching that exists on begonia was
 * kept.
 *
 * Boosts select binder transaction targets to SCHED_FIFO (kernel prio
 * 98) during synchronous transactions, and keeps RT priority of bound
 * binder threads from being dropped back to normal.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <trace/hooks/binder.h>
#include <uapi/linux/android/binder.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/prio.h>

#include <../kernel/sched/sched.h>
#include "binder_prio_types.h"

static struct binder_priority home_saved_priority;
static bool home_saved_valid;

static const char *task_name[] = {
	"com.miui.home",
	".globallauncher",		/* com.mi.android.globallauncher */
	"ndroid.systemui",		/* com.android.systemui */
	"cameraserver",
	"rsonalassistant",		/* com.miui.personalassistant */
};

static int to_userspace_prio(int policy, int kernel_priority)
{
	if (fair_policy(policy))
		return PRIO_TO_NICE(kernel_priority);
	else
		return MAX_USER_RT_PRIO - 1 - kernel_priority;
}

static bool set_binder_rt_task(struct binder_transaction *t)
{
	int i;

	if (t && t->from && t->from->task && t->to_proc && t->to_proc->tsk &&
	    !(t->flags & TF_ONE_WAY)) {
		if (!rt_policy(t->from->task->policy))
			return false;

		if (!strncmp(t->from->task->group_leader->comm, "com.miui.home",
			     strlen("com.miui.home")) &&
		    !strncmp(t->from->task->comm, "RenderThread",
			     strlen("RenderThread")) &&
		    !strncmp(t->to_proc->tsk->comm, "surfaceflinger",
			     strlen("surfaceflinger")))
			return true;
		if (!strncmp(t->from->task->group_leader->comm, "surfaceflinger",
			     strlen("surfaceflinger")) &&
		    !strncmp(t->from->task->comm, "passBlur",
			     strlen("passBlur")))
			return true;
		if (!strncmp(t->from->task->group_leader->comm, "cameraserver",
			     strlen("cameraserver")) &&
		    !strncmp(t->from->task->comm, "C3Dev-",
			     strlen("C3Dev-")) &&
		    strstr(t->from->task->comm, "-ReqQ"))
			return true;
		if (!strncmp(t->from->task->comm, "wmshell.main",
			     strlen("wmshell.main")) ||
		    !strncmp(t->from->task->comm, "ll.splashscreen",
			     strlen("ll.splashscreen")))
			return true;
		if (t->from->task->pid == t->from->task->tgid)
			for (i = 0; i < ARRAY_SIZE(task_name); i++)
				if (!strncmp(t->from->task->comm, task_name[i],
					     strlen(task_name[i])))
					return true;
	}
	return false;
}

static void extend_surfacefinger_binder_set_priority_handler(void *data,
		struct binder_transaction *t, struct task_struct *task)
{
	struct sched_param params;
	struct binder_priority desired;
	unsigned int policy;
	struct binder_node *target_node = t->buffer->target_node;

	desired.prio = target_node->min_priority;
	desired.sched_policy = target_node->sched_policy;
	policy = desired.sched_policy;
	if (set_binder_rt_task(t)) {
		desired.sched_policy = SCHED_FIFO;
		desired.prio = 98;
		policy = desired.sched_policy;
	}
	if (rt_policy(policy) && task->policy != policy) {
		params.sched_priority = to_userspace_prio(policy, desired.prio);
		sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK,
					   &params);
	}
}

static void extend_surfacefinger_binder_trans_handler(void *data,
		struct binder_proc *target_proc, struct binder_proc *proc,
		struct binder_thread *thread,
		struct binder_transaction_data *tr)
{
	if (target_proc && target_proc->tsk) {
		if (!strncmp(target_proc->tsk->comm, "surfaceflinger",
			     strlen("surfaceflinger"))) {
			if (thread && proc && tr && thread->transaction_stack &&
			    !(thread->transaction_stack->flags & TF_ONE_WAY)) {
				target_proc->default_priority.sched_policy =
					SCHED_FIFO;
				target_proc->default_priority.prio = 98;
			}
		} else if (!strncmp(target_proc->tsk->comm, "com.miui.home",
				    strlen("com.miui.home"))) {
			if (rt_policy(target_proc->tsk->policy)) {
				if (!rt_policy(
					target_proc->default_priority.sched_policy)) {
					if (!home_saved_valid) {
						home_saved_priority =
							target_proc->default_priority;
						home_saved_valid = true;
					}
					target_proc->default_priority.sched_policy =
						SCHED_FIFO;
					target_proc->default_priority.prio = 98;
				}
			} else {
				if (rt_policy(
					target_proc->default_priority.sched_policy)) {
					if (home_saved_valid)
						target_proc->default_priority =
							home_saved_priority;
				}
			}
		}
	}
}

static void
extend_skip_binder_thread_priority_from_rt_to_normal_handler(void *data,
		struct task_struct *task, bool *skip)
{
	if (task && rt_policy(task->policy))
		*skip = true;
}

static int __init binder_prio_init(void)
{
	int ret;

	ret = register_trace_android_vh_binder_set_priority(
			extend_surfacefinger_binder_set_priority_handler, NULL);
	ret |= register_trace_android_vh_binder_trans(
			extend_surfacefinger_binder_trans_handler, NULL);
	ret |= register_trace_android_vh_binder_priority_skip(
			extend_skip_binder_thread_priority_from_rt_to_normal_handler,
			NULL);
	if (ret)
		pr_warn("failed to register some binder_prio hooks (%d)\n",
			ret);

	pr_info("binder_prio: module init!\n");
	return 0;
}

static void __exit binder_prio_exit(void)
{
	unregister_trace_android_vh_binder_set_priority(
			extend_surfacefinger_binder_set_priority_handler, NULL);
	unregister_trace_android_vh_binder_trans(
			extend_surfacefinger_binder_trans_handler, NULL);
	unregister_trace_android_vh_binder_priority_skip(
			extend_skip_binder_thread_priority_from_rt_to_normal_handler,
			NULL);

	pr_info("binder_prio: module exit!\n");
}

module_init(binder_prio_init);
module_exit(binder_prio_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xiaomi <knbn666@gmail.com>");
MODULE_DESCRIPTION("begonia/MIUI binder priority optimization");