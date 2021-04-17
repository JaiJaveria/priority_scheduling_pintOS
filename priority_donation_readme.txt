All test cases defined in src/test/threads except the mlfqs passed.

When a thread with more priority is created, the current thread should yield. Thus I have implemented thread preemption in thread_unblock().
To implement priority scheduling, I made the insertion in the ready list according to priority order.
When changing the priority of a thread I check if some other threads are waiting for a lock possed by the current thread and if so not change the priority immediately, only when the all the locks are released, even when the new priority to be set is higher.  
In lock acquire, if no one else has the lock, thte lock is immediately given. Otherwise it goes into waiting. I have a waiting locks list inside struct thread which contains all the locks the thread is waiting for. If the thread starts waiting for the lock, priority donation is done. The possibility of nested priority donations is also checked.
Priority is restored in lock_acquire.
For implementing priority in semaphores and conditional variables, before taking out a waiting thread, I sort the list to find the thread with max priority to the front of the queue.
 
