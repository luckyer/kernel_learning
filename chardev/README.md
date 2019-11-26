等待队列手动休眠：
1.DECLARE_WAITQUEUE(name, tsk) 创建一个等待队列
2.add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
3.set_current_state(TASK_INTERRUPTIBLE)
