#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>

MODULE_LICENSE("GPL");

//#define ENABLE_LOG 

#define NUM_DATA 128
typedef struct rcut_object {
	uint32_t o_id; // the unique id for objects
	// use o_id % NUM_BUCKETS for hash value
	struct hlist_node o_node;
	spinlock_t o_lock;
	struct rcu_head o_rh;
	int o_invalid;

	char o_data[NUM_DATA];
} rcut_object_t;

#define NUM_BUCKETS 1
typedef struct rcut_hashtable {
	struct hlist_head buckets[NUM_BUCKETS];
} rcut_hashtable_t;

static rcut_hashtable_t hashtable;

static struct mutex global_mutex;

static long input_hllen = 14; 
module_param(input_hllen, long, S_IRUSR);
MODULE_PARM_DESC(input_hllen, "the max id's shift");
#define ID_MAX (1 << input_hllen)

typedef struct rcut_param {
	uint32_t p_id;
	char p_data[NUM_DATA];
} rcut_param_t;

static const unsigned FUNC_PROB[] = {12, 25, 255, 255}; // <= the number will match the case
typedef struct rcut_funcs {
	int (*insert)(void *);
	int (*remove)(void *);
	int (*read)(void *);
	int (*write)(void *);
} rcut_funcs_t;

static int input_rdlen = 0; // now in usecs
module_param(input_rdlen, int, S_IRUSR);
MODULE_PARM_DESC(input_rdlen, "read sleep delay");

static void rwobject(rcut_object_t * objp) {
#ifdef ENABLE_LOG
	printk(KERN_INFO "rwobject\n");
#endif
	udelay(input_rdlen);
}

static rcut_object_t * lookup(uint32_t lookupid)
{
	struct hlist_head * bucket;
	rcut_object_t * objp;
	int n;

	bucket = &hashtable.buckets[lookupid % NUM_BUCKETS];
	n = 0;
	hlist_for_each_entry(objp, bucket, o_node) {
		if (objp->o_id == lookupid)
			return objp;
	}
	return NULL;
}

////////////////////////////////////////////////////////////
// nolock implementation
// /////////////////////////////////////////////////////////

static int remove_nolock(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	param = (rcut_param_t *) data;
	objp = lookup(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "REMOVE id = %d | not found", param->p_id);
#endif
		return 0;
	}
	hlist_del(&objp->o_node);
#ifdef ENABLE_LOG
	printk(KERN_INFO "REMOVE id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	kfree(objp);

	return 0;
}
static int insert_nolock(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;
	struct hlist_head * bucket;

	param = (rcut_param_t *) data; 
	objp = lookup(param->p_id);
	if (objp != NULL) 
		return 0;
	objp = kmalloc(sizeof(rcut_object_t), GFP_KERNEL);
	objp->o_id = param->p_id;
	memcpy(objp->o_data, param->p_data, sizeof(objp->o_data));
	objp->o_data[NUM_DATA-1] = 0;
	bucket = &hashtable.buckets[objp->o_id % NUM_BUCKETS];
	hlist_add_head(&objp->o_node, bucket);
#ifdef ENABLE_LOG
	printk(KERN_INFO "INSERT id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	
	return 0;
}
static int read_nolock(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	param = (rcut_param_t *) data; 
	objp = lookup(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "READ id = %d | not found\n", param->p_id);
#endif
		return 0;
	}
	rwobject(objp);
#ifdef ENABLE_LOG
	printk(KERN_INFO "READ id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif

	return 0;
}

//////////////////////////////////////////////////////////////
// biglock implementation
//////////////////////////////////////////////////////////////

static int insert_biglock(void * data) {
	mutex_lock(&global_mutex);
	insert_nolock(data);
	mutex_unlock(&global_mutex);
	return 0;
}
static int remove_biglock(void * data) {
	mutex_lock(&global_mutex);
	remove_nolock(data);
	mutex_unlock(&global_mutex);
	return 0;
}

static int read_biglock(void * data) {
	mutex_lock(&global_mutex);
	read_nolock(data);
	mutex_unlock(&global_mutex);
	return 0;
}

/////////////////////////////////////////////////////////////
// old rcu implementation
/////////////////////////////////////////////////////////////

static rcut_object_t * lookup_rcu(uint32_t lookupid)
{
	rcut_object_t * objp;
	struct hlist_head * bucket;

	bucket = &hashtable.buckets[lookupid % NUM_BUCKETS];

	rcu_read_lock();
	hlist_for_each_entry_rcu(objp, bucket, o_node) {
		if (objp->o_id == lookupid) {
			spin_lock(&objp->o_lock);
			rcu_read_unlock();
			return objp;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static int insert_rcu(void * data) 
{
	rcut_param_t * param;
	rcut_object_t * objp;
	struct hlist_head * bucket;

	mutex_lock(&global_mutex);

	param = (rcut_param_t *) data; 
	objp = lookup(param->p_id);
	if (objp != NULL) {
		mutex_unlock(&global_mutex);
		return 0;
	}
	objp = kmalloc(sizeof(rcut_object_t), GFP_KERNEL);
	spin_lock_init(&objp->o_lock);
	rcu_head_init(&objp->o_rh);
	objp->o_id = param->p_id;
	memcpy(objp->o_data, param->p_data, sizeof(objp->o_data));
	objp->o_data[NUM_DATA-1] = 0;
	bucket = &hashtable.buckets[objp->o_id % NUM_BUCKETS];
	hlist_add_head_rcu(&objp->o_node, bucket);
#ifdef ENABLE_LOG
	printk(KERN_INFO "INSERT_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif

	mutex_unlock(&global_mutex);
	return 0;
}

static int remove_rcu(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	mutex_lock(&global_mutex);

	param = (rcut_param_t *) data;
	objp = lookup(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "REMOVE_RCU id = %d | not found", param->p_id);
#endif
		mutex_unlock(&global_mutex);
		return 0;
	}
#ifdef ENABLE_LOG
	printk(KERN_INFO "REMOVE_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	hlist_del_rcu(&objp->o_node);
	mutex_unlock(&global_mutex);
	kfree_rcu(objp, o_rh);

	return 0;
}

static int read_rcu(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	param = (rcut_param_t *) data; 
	objp = lookup_rcu(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "READ_RCU id = %d | not found\n", param->p_id);
#endif
		return 0;
	}
	rwobject(objp);
#ifdef ENABLE_LOG
	printk(KERN_INFO "READ_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	spin_unlock(&objp->o_lock);
	
	return 0;
}

/////////////////////////////////////////////////////////////
// new rcu implementation
/////////////////////////////////////////////////////////////

static rcut_object_t * lookup_newrcu(uint32_t lookupid)
{
	rcut_object_t * objp;
	struct hlist_head * bucket;

	bucket = &hashtable.buckets[lookupid % NUM_BUCKETS];

	rcu_read_lock();
	hlist_for_each_entry_rcu(objp, bucket, o_node) {
		if (objp->o_id == lookupid) {
			spin_lock(&objp->o_lock);
			if (objp->o_invalid) {
				spin_unlock(&objp->o_lock);
				rcu_read_unlock();
				return NULL;
			}
			rcu_read_unlock();
			return objp;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static int insert_newrcu(void * data) 
{
	rcut_param_t * param;
	rcut_object_t * objp;
	struct hlist_head * bucket;

	mutex_lock(&global_mutex);

	param = (rcut_param_t *) data; 
	objp = lookup(param->p_id);
	if (objp != NULL) {
		mutex_unlock(&global_mutex);
		return 0;
	}
	objp = kmalloc(sizeof(rcut_object_t), GFP_KERNEL);
	spin_lock_init(&objp->o_lock);
	rcu_head_init(&objp->o_rh);
	objp->o_id = param->p_id;
	memcpy(objp->o_data, param->p_data, sizeof(objp->o_data));
	objp->o_data[NUM_DATA-1] = 0;
	bucket = &hashtable.buckets[objp->o_id % NUM_BUCKETS];
	hlist_add_head_rcu(&objp->o_node, bucket);
#ifdef ENABLE_LOG
	printk(KERN_INFO "INSERT_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif

	mutex_unlock(&global_mutex);
	return 0;
}

static int remove_newrcu(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	mutex_lock(&global_mutex);

	param = (rcut_param_t *) data;
	objp = lookup(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "REMOVE_RCU id = %d | not found", param->p_id);
#endif
		mutex_unlock(&global_mutex);
		return 0;
	}
#ifdef ENABLE_LOG
	printk(KERN_INFO "REMOVE_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	hlist_del_rcu(&objp->o_node);
	mutex_unlock(&global_mutex);
	spin_lock(&objp->o_lock);
	objp ->o_invalid = true;
	spin_unlock(&objp->o_lock);
	kfree_rcu(objp, o_rh);

	return 0;
}


static int read_newrcu(void * data)
{
	rcut_param_t * param;
	rcut_object_t * objp;

	param = (rcut_param_t *) data; 
	objp = lookup_newrcu(param->p_id);
	if (objp == NULL) {
#ifdef ENABLE_LOG
		printk(KERN_INFO "READ_RCU id = %d | not found\n", param->p_id);
#endif
		return 0;
	}
	rwobject(objp);
#ifdef ENABLE_LOG
	printk(KERN_INFO "READ_RCU id = %d | data = %s\n", objp->o_id, objp->o_data);
#endif
	spin_unlock(&objp->o_lock);
	
	return 0;
}

////////////////////////////////////////////////////////////
// set rcut_funcs_t pointers
////////////////////////////////////////////////////////////

static rcut_funcs_t biglock = {
	insert_biglock,
	remove_biglock,
	read_biglock,
	NULL,
};
static rcut_funcs_t nolock = {
	insert_nolock,
	remove_nolock,
	read_nolock,
	NULL,
};
static rcut_funcs_t rculock = {
	insert_rcu,
	remove_rcu,
	read_rcu,
	NULL,
};
static rcut_funcs_t newrculock = {
	insert_newrcu,
	remove_newrcu,
	read_newrcu,
	NULL,
};

static rcut_funcs_t * funcs = &rculock;

//////////////////////////////////////////////////////////
// manager thread
/////////////////////////////////////////////////////////

typedef struct rcut_mgrexec {
	rcut_funcs_t funcs;
	rcut_param_t param;
} rcut_mgrexec_t;

static volatile int finished_cnt;
static spinlock_t finished_cnt_lock;
static struct task_struct * manager_thread;

static int manager_entry(void *data) 
{
	unsigned char n;
	uint32_t id;
	rcut_param_t param;
	uint64_t limit;
	uint64_t * retval;
	unsigned long start, end;

	limit = *((uint64_t *) data);

	printk(KERN_INFO "manager entry started with limit %llu\n", limit);

	++limit;
	start = jiffies;
	while (--limit > 0) {
		get_random_bytes(&n, 1);
		id = 0;
		get_random_bytes(&id, 4);
		id &= (1 << input_hllen) - 1;
		id = 0;
		if (n <= FUNC_PROB[0]) {
			param.p_id = id;
			strcpy(param.p_data, "p_data_inserted");
			funcs->insert(&param);
			
		} else if (n <= FUNC_PROB[1]) {
			param.p_id = id;
			funcs->remove(&param);
	
		} else if (n <= FUNC_PROB[2]) {
			param.p_id = id;
			funcs->read(&param);
	
		} else if (n <= FUNC_PROB[3]) {
			printk(KERN_ALERT "FATAL\n");
	
		} else {
			printk(KERN_ALERT "FATAL\n");
	
		}
	}
	end = jiffies;

	retval = (uint64_t *) data;
	retval[0] = end - start;
	retval[1] = start;

	printk(KERN_INFO "manager entry begin exit");
	spin_lock(&finished_cnt_lock);
	++finished_cnt;
	spin_unlock(&finished_cnt_lock);
	wake_up_process(manager_thread);
	printk(KERN_INFO "manager entry exited");

	do_exit(0);
}


static int manager(unsigned workers, uint32_t limit) {
	// maintain limit number of worker, with certain prob of spawn 
	
	rcut_param_t param;
	struct task_struct ** w;
	uint64_t * data;
	uint64_t sum;
	int i;
	int last_finished_cnt;

	printk(KERN_ALERT "manager starts: workers=%u, limit=%u\n",
			workers, limit);

	spin_lock_init(&finished_cnt_lock);

	// populate every even position from 0 to ID_MAX-1
	strcpy(param.p_data, "p_data_original");
	for (param.p_id = 0; param.p_id < ID_MAX; param.p_id += 2) 
		(nolock.insert)(&param);

	if (limit <= 0) limit = 1;
	if (workers <= 0) workers = 1;

	// initialize the worker pool, free w and data later
	w = kmalloc(sizeof(struct task_struct) * workers, GFP_KERNEL);
	data = kmalloc(2 * sizeof(uint64_t) * workers, GFP_KERNEL); // returned with dura jif and start jif
	for (i = 0; i < workers; ++i) {
		data[2 * i] = limit / workers;
		w[i] = kthread_create(manager_entry, &data[2 * i], "mentry");
	}

	printk(KERN_INFO "==========================\n"
			 "=======START TEST=========\n"
			 "==========================\n");

	last_finished_cnt = -1;
	finished_cnt = 0;
	manager_thread = current;
	set_current_state(TASK_INTERRUPTIBLE);

	for (i = 0; i < workers; ++i) {
		wake_up_process(w[i]);
	}
	
	// https://www.linuxjournal.com/article/8144
	spin_lock(&finished_cnt_lock);
	while (finished_cnt != workers) {
		if (finished_cnt == last_finished_cnt) {
			// when ctrl-C is invoked
			printk(KERN_ALERT "DEBUG\n");
			spin_unlock(&finished_cnt_lock);
			return 0;  // 0 indicates fail
		}
		last_finished_cnt = finished_cnt;
		printk(KERN_INFO "manager: finished_cnt = %u\n", finished_cnt);
		spin_unlock(&finished_cnt_lock);
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&finished_cnt_lock);
	}
	set_current_state(TASK_RUNNING);
	spin_unlock(&finished_cnt_lock);

	// all threads are finished, the time they spent is on data, take the average
	sum = 0;
	for (i = 0; i < workers; ++i) {
		sum += data[2 * i];
		printk(KERN_ALERT "worker %d : dura=%u, start=%u \n", 
				i,
				jiffies_to_msecs(data[2 * i]),
				jiffies_to_msecs(data[2 * i + 1]));
	}
	sum /= workers;

	printk(KERN_INFO "==========================\n"
			 "=======END TEST===========\n");
	printk(KERN_ALERT "  TOTAL TIME : %d\n", jiffies_to_msecs(sum));
	printk(KERN_INFO "==========================\n");

	kfree(data);
	kfree(w);
	
	return jiffies_to_msecs(sum);
}

///////////////////////////////////////////////////////////////
// entry point
///////////////////////////////////////////////////////////////

static long input_workers = 1;
static long input_limit = 10;
static char* input_strategy = "big";

module_param(input_workers, long, S_IRUSR);
MODULE_PARM_DESC(input_workers, "the number of concurrent workers");
module_param(input_limit, long, S_IRUSR);
MODULE_PARM_DESC(input_limit, "the number of total workers completed before stop");
module_param(input_strategy, charp, S_IRUSR);
MODULE_PARM_DESC(input_strategy, "the locking strategy: big or rcu or callrcu");

#define TEST_NUM 5

static int __init test_init(void)
{
	int i;
	unsigned results[TEST_NUM];

	if (input_strategy == NULL) {
		printk(KERN_ALERT "Please input strategy\n");
		return -1;
	} else if (0 == strcmp(input_strategy, "rcu")) {
		funcs = &rculock;
	} else if (0 == strcmp(input_strategy, "newrcu")) {
		funcs = &newrculock;
	} else if (0 == strcmp(input_strategy, "big")) {
		funcs = &biglock;
	} else {
		printk(KERN_ALERT "Please input strategy\n");
		return -1;
	}

	printk(KERN_INFO "TEST_INIT\n==============\n");
	
	mutex_init(&global_mutex);
	for (i = 0; i < NUM_BUCKETS; ++i) {
		INIT_HLIST_HEAD(&hashtable.buckets[i]);
	}

	for (i = 0; i < TEST_NUM; ++i) {
		results[i] = manager(input_workers, input_limit);
		if (results[i] == 0)
			return -1;
	}

	for (i = 0; i < TEST_NUM; ++i) {
		printk(KERN_INFO "TEST No. %d : %u", i, results[i]);
	}


	return 0;
}

static void __exit test_exit(void) 
{
	printk(KERN_INFO "==============\nTEST_EXIT\n");
}


module_init(test_init);
module_exit(test_exit);
