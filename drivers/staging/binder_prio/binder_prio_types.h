/*
 * Binder internal type shim for binder_prio.
 *
 * The begonia 4.14.356 binder keeps its core object structs private to
 * binder.c. This file reproduces them VERBATIM (same layout, same
 * preprocessor state, via binder_internal.h which drives every #ifdef)
 * so the binder_prio vendor-hook module can inspect Binder transactions
 * without relocating the whole binder core.
 *
 * KEEP IN SYNC with drivers/android/binder.c definitions.
 */
#ifndef _BINDER_PRIO_TYPES_H
#define _BINDER_PRIO_TYPES_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <uapi/linux/android/binder.h>

/* resolved via the kernel's -I$(srctree)/include search path */
#include <../drivers/android/binder_internal.h>
#include <../drivers/android/binder_alloc.h>

struct binder_context;
struct binder_ref;
struct cred;
struct dentry;
struct files_struct;
struct task_struct;

/* --- verbatim from binder.c: enum_stat --- */
enum binder_stat_types {
	BINDER_STAT_PROC,
	BINDER_STAT_THREAD,
	BINDER_STAT_NODE,
	BINDER_STAT_REF,
	BINDER_STAT_DEATH,
	BINDER_STAT_TRANSACTION,
	BINDER_STAT_TRANSACTION_COMPLETE,
	BINDER_STAT_COUNT
};

/* --- verbatim from binder.c: binder_stats --- */
struct binder_stats {
	atomic_t br[_IOC_NR(BR_ONEWAY_SPAM_SUSPECT) + 1];
	atomic_t bc[_IOC_NR(BC_REPLY_SG) + 1];
	atomic_t obj_created[BINDER_STAT_COUNT];
	atomic_t obj_deleted[BINDER_STAT_COUNT];
};

/* --- verbatim from binder.c: binder_work --- */
struct binder_work {
	struct list_head entry;

	enum binder_work_type {
		BINDER_WORK_TRANSACTION = 1,
		BINDER_WORK_TRANSACTION_COMPLETE,
		BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT,
		BINDER_WORK_RETURN_ERROR,
		BINDER_WORK_NODE,
		BINDER_WORK_DEAD_BINDER,
		BINDER_WORK_DEAD_BINDER_AND_CLEAR,
		BINDER_WORK_CLEAR_DEATH_NOTIFICATION,
	} type;
};

/* --- verbatim from binder.c: binder_error --- */
struct binder_error {
	struct binder_work work;
	uint32_t cmd;
};

/* --- verbatim from binder.c: binder_priority --- */
struct binder_priority {
	unsigned int sched_policy;
	int prio;
};

/* --- verbatim from binder.c: binder_node --- */
struct binder_node {
	int debug_id;
	spinlock_t lock;
	struct binder_work work;
	union {
		struct rb_node rb_node;
		struct hlist_node dead_node;
	};
	struct binder_proc *proc;
	struct hlist_head refs;
	int internal_strong_refs;
	int local_weak_refs;
	int local_strong_refs;
	int tmp_refs;
	binder_uintptr_t ptr;
	binder_uintptr_t cookie;
	struct {
		/*
		 * bitfield elements protected by
		 * proc inner_lock
		 */
		u8 has_strong_ref:1;
		u8 pending_strong_ref:1;
		u8 has_weak_ref:1;
		u8 pending_weak_ref:1;
	};
	struct {
		/*
		 * invariant after initialization
		 */
		u8 sched_policy:2;
		u8 inherit_rt:1;
		u8 accept_fds:1;
		u8 txn_security_ctx:1;
		u8 min_priority;
	};
	bool has_async_transaction;
	struct list_head async_todo;
#ifdef BINDER_WATCHDOG
	char name[MAX_SERVICE_NAME_LEN];
#endif
};

/* --- verbatim from binder.c: binder_proc --- */
struct binder_proc {
	struct hlist_node proc_node;
	struct rb_root threads;
	struct rb_root nodes;
	struct rb_root refs_by_desc;
	struct rb_root refs_by_node;
	struct list_head waiting_threads;
	int pid;
	struct task_struct *tsk;
	struct files_struct *files;
	struct mutex files_lock;
	const struct cred *cred;
	struct hlist_node deferred_work_node;
	int deferred_work;
	int outstanding_txns;
	bool is_dead;
	bool is_frozen;
	bool sync_recv;
	bool async_recv;
	wait_queue_head_t freeze_wait;

	struct list_head todo;
	struct binder_stats stats;
	struct list_head delivered_death;
	u32 max_threads;
	int requested_threads;
	int requested_threads_started;
	int tmp_ref;
	struct binder_priority default_priority;
	struct dentry *debugfs_entry;
	struct binder_alloc alloc;
	struct binder_context *context;
	spinlock_t inner_lock;
	spinlock_t outer_lock;
	struct dentry *binderfs_entry;
	bool oneway_spam_detection_enabled;
};

/* --- verbatim from binder.c: binder_thread --- */
struct binder_thread {
	struct binder_proc *proc;
	struct rb_node rb_node;
	struct list_head waiting_thread_node;
	int pid;
	int looper;              /* only modified by this thread */
	bool looper_need_return; /* can be written by other thread */
	struct binder_transaction *transaction_stack;
	struct list_head todo;
	bool process_todo;
	struct binder_error return_error;
	struct binder_error reply_error;
	wait_queue_head_t wait;
	struct binder_stats stats;
	atomic_t tmp_ref;
	bool is_dead;
	struct task_struct *task;
};

/* --- verbatim from binder.c: binder_transaction --- */
struct binder_transaction {
	int debug_id;
	struct binder_work work;
	struct binder_thread *from;
	struct binder_transaction *from_parent;
	struct binder_proc *to_proc;
	struct binder_thread *to_thread;
	struct binder_transaction *to_parent;
	unsigned need_reply:1;
	/* unsigned is_dead:1; */	/* not used at the moment */

	struct binder_buffer *buffer;
	unsigned int	code;
	unsigned int	flags;
	struct binder_priority	priority;
	struct binder_priority	saved_priority;
	bool    set_priority_called;
	kuid_t	sender_euid;
	binder_uintptr_t security_ctx;
	/**
	 * @lock:  protects @from, @to_proc, and @to_thread
	 *
	 * @from, @to_proc, and @to_thread can be set to NULL
	 * during thread teardown
	 */
	spinlock_t lock;
#ifdef BINDER_WATCHDOG
	enum wait_on_reason wait_on;
	enum wait_on_reason bark_on;
	struct rb_node rb_node;         /* by bark_time */
	struct timespec bark_time;
	struct timespec exe_timestamp;
	char service[MAX_SERVICE_NAME_LEN];
	pid_t fproc;
	pid_t fthrd;
	pid_t tproc;
	pid_t tthrd;
	unsigned int log_idx;
#endif
#ifdef BINDER_USER_TRACKING
	struct timespec timestamp;
	struct timeval tv;
#endif
#ifdef CONFIG_MTK_TASK_TURBO
	struct task_struct *inherit_task;
#endif

};

#endif /* _BINDER_PRIO_TYPES_H */
