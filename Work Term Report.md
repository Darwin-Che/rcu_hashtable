# RCU Hash table

## Background information

### RCU

Read-copy update (RCU) is a synchronization mechanism supporting concurrency between a single updater and multiple readers. The design distributes the work between read and update paths in such a way as to make read paths extremely fast.

For the sake of illustruation, assume the readers and writers share a global pointer to a tuple (x,y). For example, let the writer increment both x and y. If no protection is applied, then the reader could access an unstable version of the tuple: only x incremented, y not changed yet. Of course, we could use a single lock to ensure no overlap between the reader/writer critical sections (code that access/modify shared data). But there are other approaches to better utilize concurrent cpus. One example is Readers-writer lock (rw-lock). Rw-lock will allow concurrent reader-reader critical sections, but not reader-writer or writer-writer. 

Despite rw-lock, compared to a single lock, exploits the parallel execution of readers, there are two main drawbacks. Firstly, the rw-lock maintains a reader counter, so that every time a reader enters/exits the critical section, it must increment/decrement the counter. In a heavy workload system, this can cause cache line problem. Secondly, a writer must block until all existing exits the critical section, but a waiting writer cannot prevent new readers from entering the critical section, therefore, the writers could starve. 

RCU instead will both allow both reader-reader and reader-writer concurrency. To achieve the latter, RCU writer will first make a copy of the original data, and then modify the data to desired stable state, then atomically replace the old data pointer with the new data pointer. (Most morden architecture supports pointer type's atomic read/write.) "Read-copy update" also gains the name from the three phases writer. In this case, whether the concurrent reader gets the old data pointer or the new data pointer, the contained data will be in a stable state. 

In order to be able to safely free the old data, the writer needs to know when all reference to the old data pointer will be released. Thus, for the readers, the shared pointer can only be read once, and then use the gained pointer to access the data, and both of the previous operations should be protected in a single critical section. Therefore, after replaceing the data pointer, the writer can delete the old data once the completion of all read-side critical section that started before the moment of finishing replacing the data pointer, since all read-side critical sections started after that moment must gained the new data pointer. 

The functions for denoting the read-side critical section is `rcu_read_lock()` and `rcu_read_unlock()`. To gain a reference to the global pointer, Linux also provides `tmp = rcu_dereference(shared_pointer)`. This function already provides necessary memory barriers. Similarly, the user should also use `rcu_assign_pointer(shared_pointer, tmp)` for the writer. 

One important details is that `rcu_read_lock()` and `rcu_read_unlock()` incurrs very small overhead. In fact, . That is the reason why it is global instead of per-object. This way it can avoid read/write at all, and thus no cache . Therefore, RCU readers introduce very little extra cost, and solves the first problem. 

For the second problem, RCU has two solutions. `synchronize_rcu()` will block until the completion of all readers entering the critical section before it. Thus, the old data can be freed after this returns. Another method is `call_rcu()`, this will register a function from parameter, and return to the caller immediately, but another thread will invoke the registered callback function after the completion of all readers entering the critical section before it. 

Therefore an example of readers and writers to the shared global pointer is 

```c
void reader() {
	rcu_read_lock();
	struct tuple * tmp = rcu_dereference(shared_tuple);
	// read tuple
	rcu_read_unlock();
}

void writer() {
	global_lock(); // exclusion for writers
	struct tuple * nt = malloc(sizeof(struct tuple));
	struct tuple * t = shared_tuple;
	*nt = *shared_tuple;
	// modify nt
	rcu_assign_pointer(shared_tuple, nt);
	synchronize_rcu();
	free(t);
	global_unlock();
}
```



### `hlist`

```c
struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};
```

### `hlist_rcu`

```
hlist_for_each_entry_rcu

hlist_add_head_rcu

hlist_del_rcu
```



## Hash table

One of the common usage of hash table is to act as a cache. To store objects in the hash table, the program needs a hashing function. If the hashing function is uniform enough to avoid collisions at its best, then the program can speed up the look-up process as a result of ignoring most of the objects in the cache by first comparing the outcome of the hashing function. 

This report will focus on the hash table that serves as a cache. A basic C struct to organize hash table is as follows:

```c
struct hashtable {
	struct hlist_head buckets[NUM_BUCKETS];
};

struct object {
    uint32_t o_hash;
	struct id o_id; // the unique id for objects
	struct hlist_node o_node;
    
    // data
};
```

When a program interacts with a cache, the most common operations needed are

1) Add an object into the cache,
3) Delete an object from the cache,
4) Read/Modify an object from the cache. 

The implementations for these methods will be given based on the two locking schemes. 

## Lock

### One big lock

The global lock is held for each operation. Therefore, no two operations on the hash table will overlap. 

This locking scheme has easy implementation and is easy to reason about its correctness since no data race will ever happens. 

But a big problem is all things execute sequentially, so it will limit the program speed on a multithreaded cpu. 

First, a helper function to retrieve an object based on id. 

```c
// lookup must be called with the global lock held
struct object * lookup(struct id *searchid) {
	uint32_t searchhash = hashfunc(searchid);
    // select the bucket possibly containing searchhash
	struct object * objp;
	hlist_for_each_entry(objp, bucket, o_node) {
		if ((objp->o_hash == searchhash) && isequal(&objp->o_id, searchid))
			return objp;
	}
	return NULL;
}
```

#### Insert an object

```c
// Precondition:
//  obj is not visible to any other thread
void insert(struct object * obj) {
	// compute the hash and the corresponding bucket from id
    global_lock();
	hlist_add_head(obj, bucket);
    global_unlock();
}
```

#### Remove an object

```c
void remove(struct id * removeid) {
	// compute the hash and the corresponding bucket from id
    global_lock();
	struct object * objp = lookup(removeid);
	if (objp == NULL) {
        global_unlock();
        return NULL;
    }
    hlist_del(objp->o_node);
    free(objp);
    global_unlock();
}
```

#### Read/Modify an object

```c
void get(struct id * removeid) {
	// compute the hash and the corresponding bucket from id
    global_lock();
    struct object * objp = lookup(removeid);
    // retreive/modify information from objp
    global_unlock();
}
```

### RCU locking

Some potential improvements to the "one big lock" strategy are: 1) access to different objects do not interfere with each other, so they should not block each other, 2) by the characteristic of RCU, read-only walks of the hash lists should be able to execute in parallel as well. 

Thus, in the proposed RCU hashtable, a per-object lock is used to denote exclusive access to the object, and RCU is applied to protect the hash lists. Therefore, the program only needs to hold the RCU lock when walking the hash list and only the per-object lock when reading/writing to the object. The finer control over locks and critical sections allows better utilization of multi-threading performance. The implementation and the effects are explained below. 





In the following proposed RCU hashtable, we add a rcu lock and a per-object lock. The original global lock is now used to provide exclusions between modification to the `hlist`. In the process of searching along the `hlist`, the strategy is to use rcu lock to protect the existence of the walked nodes, so that the retrieved object will not be freed between getting the object pointer from the `hlist` and locking the object's `o_lock`. 

The intended improvement is 

1) Minimize the exclusion between concurrent access to different objects. 

Therefore, the direct application of RCU will be inefficient because it doesn't allow two writers to execute concurrently even though they are operating on different objects. 

The most suitable approach is to separate the data races between the hash table and the data in each object. 

To simply the locking in each object, a spinlock will protect the contents. RCU is applied to allow concurrent lookups to the hashlist. 

#### extra data

Add a spinlock in the `struct object` at `o_lock` as the per-object lock.

Add a flag field in the `struct object` at `o_invalid` as the per-object flag to denote the object should not be returned by look-ups. Note that `o_invalid` is protected by `o_lock`. 

#### Insert an object

```c
// Precondition:
//  obj is not visible to any other thread
void insert_rcu(struct object * obj) {
	// compute the hash and the corresponding bucket from id
    global_lock();
	hlist_add_head_rcu(obj, bucket); // is _rcu needed?
    global_unlock();
}
```

Add_rcu doesn't have much difference from add because it is essentially an atomic operation in the eyes of concurrent searches over the list. The only supported concurrent operation is to loop over the hlist in the forward direction, therefore, the moment when searchers begin to see the added object is when the hlist_head is written. Since the obj->next is set before the moment, the searches will continue without problem. Since the search will never access the pprev pointers, the pprev of the original first node can be set after the moment without a problem. 

The global lock is used to ensure the writer-writer exclusion. Use `hlist_add_head_rcu` instead of `hlist_add_head`to use `rcu_assign_pointer` to prevent any memory reordering between this update and the initialization of obj from the caller.

#### Remove an object

```c
void remove_rcu(struct id * removeid) {
	// compute the hash and the corresponding bucket from id
    global_lock();
	struct object * objp = lookup(removeid); // do we need _rcu?
	if (objp == NULL) {
        global_unlock();
        return NULL;
    }
    hlist_del_rcu(objp->o_node);
    global_unlock();
    spinlock_lock(objp->o_lock);
    objp->invalid = true;
    spinlock_release(objp->o_lock);
    syncronize_rcu();
    free(objp);
}
```

By holding the global writers lock, `remove_rcu` can ensure that no other writers like `insert_rcu` and `remove_rcu` can run at the same time. Therefore the hashlist is not subject to change, so can use normal `lookup`. Then remove the object from the hashlist by `hlist_del_rcu` to ensure that any reader holding this object can still access `next` to walk the list. Here, the global writers lock can be released because the operation on the hashlist structure is done. Then, take the object's spinlock and set the invalid flag to be true. The reason for this design will be explained when commenting on `lookup_rcu`. There is no deadlock because we release the per-object lock before syncronize, so that all readers can proceed to obtain the lock and see the flag and `rcu_unlock()`.

```c
void remove_rcu(struct id * removeid) {
	// compute the hash and the corresponding bucket from id
	struct object * objp = lookup_rcu(removeid); // do we need _rcu?
	if (objp == NULL || objp->invalid = true;) {
        return;
    }
    global_lock();
    hlist_del_rcu(objp->o_node);
    global_unlock();
    spinlock_release(objp->o_lock);
    syncronise_rcu();
    free(objp);
}
```





#### lookup_rcu

In the success of acquiring a reference to an object, the object is returned with o\_lock held. 

The lookuprcu will block until the last reference to the object is released. 

Rcu_read_lock is used to protect the object from being freed, now it won't so can access the fields without worry. 

Assumption : Chances are low that two operations on the same object will happen at the same time, so the spinlock ussually will be fast to acquire. 

```c
struct object * lookup_rcu(struct id *searchid) {
	// compute the bucket for searchid
	rcu_read_lock();
	struct object * objp;
	hlist_for_each_entry_rcu(objp, bucket, o_node) {
		if ((objp->o_hash == searchhash) && isequal(&objp->o_id, searchid)) {
            spinlock_lock(objp->o_lock); // try to get the obj lock
            if (objp->invalid) {
                spinlock_unlock(objp->o_lock);
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
```

`lookup_rcu` is a very important way of retrieving a reference to an object. Because at the moment, a spinlock is used per object, therefore the reference to each object is given until the previous reference is given up (spinlock released). This means that lookup_rcu will block until spinlock is get. Also note that while spinning, the `rcu_read_unlock()` cannot be called yet. Because doing so could allow the remove operation slip between rcu_unlock and gaining the spinlock, causing the object to be freed. 

However, in a cache, rarely two operations on the same object will happen at the same time, so the spinlock ussually will be fast to acquire. 

After gaining the spinlock, invalid flag is checked to see if the object is already marked deleted (removed from the cache). 

If remove_rcu executes in parallel, it could be in two positions: 

1. before invalid bit is set (before lock is gained), then remove_rcu cannot proceed until this lookup's caller releases the spinlock. 
2. After the invalid bit is set (after lock is released), here the remover is waiting for this lookup to exit the critical seciton. If the lookup still return the object, then the caller will work on a removed object. This is useless work and also blocks the remover from continueing. Therefore, I will exits the reader critical section immediately. 

In conclusion, the order property guarenteed is : 

1. Every lookup will not return an object not in the hashlist
2. Every lookup after the insert returns will find the the object

#### Read/Modify an object

```c
void read_rcu(struct id * getid) {
	// compute the hash and the corresponding bucket from id
    struct object * objp = lookup_rcu(removeid);
    if (objp == NULL) {
        return;
    }
    // read/modify the data in objp
    spinlock_release(objp->o_lock);
}
```

### Compare RCU with atmoic and mutex

Amotic : no knowledge about when the deleted object becomes unreferenced. 

Mutex : affect the performance when large amount of readers despite on different objects. 

## Rename an object inplace (if time allows)



Useful quotes

https://www.kernel.org/doc/Documentation/RCU/Design/Requirements/Requirements.html#Fundamental%20Requirements

In order to avoid fatal problems such as deadlocks, an RCU read-side critical section must not contain calls to `synchronize_rcu()`. Similarly, an RCU read-side critical section must not contain anything that waits, directly or indirectly, on completion of an invocation of `synchronize_rcu()`. 

```
tail /var/log/syslog
```

