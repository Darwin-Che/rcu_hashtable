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
	int o_invalid;
	struct rcu_head o_rh;

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

static int input_rdlen = 0;
module_param(input_rdlen, int, S_IRUSR);
MODULE_PARM_DESC(input_rdlen, "read sleep delay");

static void rwobject(rcut_object_t * objp) {
#ifdef ENABLE_LOG
	printk(KERN_INFO "rwobject %d\n", sum);
#endif
	mdelay(input_rdlen);
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
// rcu implementation
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
	synchronize_rcu();
	kfree(objp);

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

////////////////////////////////////////////////////////////
// call_rcu 
///////////////////////////////////////////////////////////

static int remove_callrcu(void * data)
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
static rcut_funcs_t callrculock = {
	insert_rcu,
	remove_callrcu,
	read_rcu,
	NULL,
};

static rcut_funcs_t * funcs = &rculock;

//////////////////////////////////////////////////////////
// manager thread
/////////////////////////////////////////////////////////

static struct semaphore manager_sem;

typedef struct rcut_mgrexec {
	rcut_funcs_t funcs;
	rcut_param_t param;
} rcut_mgrexec_t;

static int manager_entry(void *data) 
{
	unsigned char n;
	uint32_t id;
	rcut_param_t param;
	uint32_t * thread_id;

	thread_id = (uint32_t *) data;  
	get_random_bytes(&n, 1);
	id = 0;
	get_random_bytes(&id, 4);
	id &= (1 << input_hllen) - 1;
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
#ifdef ENABLE_LOG
	printk(KERN_INFO "EXIT %d\n", *thread_id);
#endif
	kfree(thread_id);
	up(&manager_sem);
	do_exit(0);
}


static int manager(unsigned workers, uint32_t limit) {
	// maintain limit number of worker, with certain prob of spawn 
	
	rcut_param_t param;
	struct task_struct * w;
	uint32_t thread_id;
	uint32_t * data;
	unsigned long start, end;

	sema_init(&manager_sem, workers);

	// populate every even position from 0 to ID_MAX-1
	strcpy(param.p_data, "p_data_original");
	for (param.p_id = 0; param.p_id < ID_MAX; param.p_id += 2) 
		(nolock.insert)(&param);
	
	if (limit <= 0) limit = 1;
	if (workers <= 0) workers = 1;

	// current strategy options:
	// 1. use a semaphore with blocking
	// 2. use a semaphore without blocking
	// 3. CAS
	// 4. wait queue
	
	printk(KERN_INFO "==========================\n"
			 "=======START TEST=========\n"
			 "==========================\n");
	
	thread_id = 0;
	start = jiffies;
	while (1) {
		down(&manager_sem);
		// this means that at least limit number of threads finished
		if (thread_id >= limit + workers) 
			break;
		data = kmalloc(sizeof(uint32_t), GFP_KERNEL);
		*data = thread_id;
		++thread_id;
#ifdef ENABLE_LOG
		printk(KERN_INFO "ENTER %d\n", *data);
#endif
		w = kthread_run(manager_entry, data, "mentry");			
	}
	end = jiffies;
	printk(KERN_INFO "==========================\n"
			 "=======END TEST===========\n");
	printk(KERN_ALERT "  TOTAL TIME : %d\n", jiffies_to_msecs(end - start));
	printk(KERN_INFO "==========================\n");

	
	return jiffies_to_msecs(end - start);
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

#define TEST_NUM 6

static int __init test_init(void)
{
	int i;
	unsigned results[TEST_NUM];

	if (input_strategy == NULL) {
		printk(KERN_ALERT "Please input strategy\n");
		return -1;
	} else if (0 == strcmp(input_strategy, "rcu")) {
		funcs = &rculock;
	} else if (0 == strcmp(input_strategy, "callrcu")) {
		funcs = &callrculock;
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
		msleep(500);
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
