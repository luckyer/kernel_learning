#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define GLOBALMEM_SIZE  0x1000
#define GLOBAL_MAGIC 'g'
#define MEM_CLEAR       _IO(GLOBAL_MAGIC, 0)
#define GLOBALMEM_MAJOR 230  //主设备号

const char *device_name = "globalmem";

static int globalmem_major = GLOBALMEM_MAJOR;
module_param(globalmem_major, int, S_IRUGO);

struct globalmem_dev{
    struct cdev cdev;
    unsigned int current_len;
    unsigned char mem[GLOBALMEM_SIZE];
    struct mutex mutex;
    wait_queue_head_t r_wait;  //read wait
    wait_queue_head_t w_wait;  //write wait
};
struct globalmem_dev *globalmem_devp ;
struct class *globalmem_class;
struct device *globalmem_device;

static ssize_t globalmem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;   //读取的位置相对文件开头的位移
    unsigned int count = size; //读取的大小
    int ret = 0;
    struct globalmem_dev *dev = filp->private_data;
    //定义等待队列
    DECLARE_WAITQUEUE(wait, current);
    //如果偏移越界则直接返回
    if (p >= GLOBALMEM_SIZE)
        return 0;
    mutex_lock(&dev->mutex);
    add_wait_queue(&dev->r_wait, &wait);
    while(dev->current_len == 0){
        if (filp->f_flags & O_NONBLOCK){
            ret = -EAGAIN;
            goto out;
        }
        //手动休眠
        __set_current_state(TASK_INTERRUPTIBLE);
        mutex_unlock(&dev->mutex);
        //进程调度
        schedule();
        //检查唤醒源是否为中断，如果是中断则返回-ERESTARTSYS
        if (signal_pending(current)){
            ret = -ERESTARTSYS;
            goto out2;
        }
        mutex_lock(&dev->mutex);
    }
    
    //读取的最大长度不可超过当前内容的长度
    if (count > dev->current_len)
        count = dev->current_len;
        
    //copy_to_user成功会返回0，失败则会返回没有拷贝成功的字节数，和memcpy有很大区别
    if (copy_to_user(buf, dev->mem, count)){
        ret = -EFAULT;
        goto out;
    }else{
        //从缓存中去掉已经读取掉的内容
        memcpy(dev->mem, dev->mem + count, dev->current_len - count);
        dev->current_len -= count;
        printk(KERN_INFO "read %d bytes(s), current_len:%d \n", count, dev->current_len);
        //唤醒可能被阻塞的写进程
        wake_up_interruptible(&dev->w_wait);
        
        ret = count;
    }
out:
    mutex_unlock(&dev->mutex);
out2:
    remove_wait_queue(&dev->r_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

static ssize_t globalmem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;
    unsigned int count = size ;
    int ret = 0;
    struct globalmem_dev *dev = filp->private_data;
    DECLARE_WAITQUEUE(wait, current);
    
    if (p > GLOBALMEM_SIZE)
        return 0;
    if (count > GLOBALMEM_SIZE -p)
        count = GLOBALMEM_SIZE - p;
        
    mutex_lock(&dev->mutex);
    add_wait_queue(&dev->w_wait, &wait);
    while(dev->current_len == GLOBALMEM_SIZE){
        if (filp->f_flags & O_NONBLOCK){
            ret = -EAGAIN;
            goto out;
        }
        __set_current_state(TASK_INTERRUPTIBLE);
        mutex_unlock(&dev->mutex);
        schedule();
        if (signal_pending(current)){
            ret = -ERESTARTSYS;
            goto out2;
        }
        mutex_lock(&dev->mutex);
    }
    if (count > GLOBALMEM_SIZE - dev->current_len){
        count = GLOBALMEM_SIZE - dev->current_len;
    }
    
    if (copy_from_user(dev->mem + dev->current_len, buf, count)){
        ret = -EFAULT;
        goto out;
    }else{
        dev->current_len += count;
        printk(KERN_INFO "written %d bytes(s), current_len:%d \n", count, dev->current_len);
        //唤醒可能被阻塞的读进程
        wake_up_interruptible(&dev->r_wait);
        
        ret = count;
    }
    
out:
    mutex_unlock(&dev->mutex);
out2:
    remove_wait_queue(&dev->w_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

static loff_t globalmem_llseek(struct file *filp, loff_t offset, int orig)
{
    loff_t ret = 0;
    switch(orig){
        case 0: //从文件开始位置进行位移
            if (offset < 0){
                ret = -EINVAL;
                break;
            }
            if ((unsigned int)offset > GLOBALMEM_SIZE){
                ret = -EINVAL;
                break;
            }
            filp->f_pos = (unsigned int)offset;
            ret = filp->f_pos;
            break;
        case 1:
            if ((filp->f_pos + offset) > GLOBALMEM_SIZE){
                ret = -EINVAL;
                break;
            }
            if ((filp->f_pos + offset) < 0){
                ret = -EINVAL;
                break;
            }
            filp->f_pos += offset;
            ret = filp->f_pos;
            break;
        default:
            ret = -EINVAL;
            break;
    }
    return ret ;
}

static long globalmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct globalmem_dev *dev = filp->private_data;
    
    switch(cmd){
        case MEM_CLEAR:
            memset(dev->mem, 0, GLOBALMEM_SIZE);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static int globalmem_open(struct inode *inode, struct file *filp)
{
    filp->private_data = globalmem_devp;
    return 0;
}

static int globalmem_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations globalmem_fops = {
    .owner = THIS_MODULE,
    .llseek = globalmem_llseek,
    .read = globalmem_read,
    .write = globalmem_write,
    .unlocked_ioctl = globalmem_ioctl,
    .open = globalmem_open,
    .release = globalmem_release,
};

static void globalmem_setup_cdev(struct globalmem_dev *dev, int index)
{
    int err, devno;
    devno = MKDEV(globalmem_major, index);
    cdev_init(&dev->cdev, &globalmem_fops);
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_NOTICE "Error %d adding globalmem%d", err, index);
}

static int __init globalmem_init(void)
{
    int ret ;
    dev_t devno = MKDEV(globalmem_major, 0);
    
    if (globalmem_major)
        ret = register_chrdev_region(devno, 1, device_name);
    else{
        //alloc_chrdev_region会自动分配空闲的设备号给当前设备节点，可以避免设备号冲突的问题
        //获取的设备号会存储在devno中，通过MAJOR可以获取到设备的主设备号
        ret = alloc_chrdev_region(&devno, 0, 1, device_name);
        globalmem_major = MAJOR(devno);
    }
    if (ret < 0)
        return ret;
    //kzalloc会将申请到的内存清零处理
    globalmem_devp = kzalloc(sizeof(struct globalmem_dev), GFP_KERNEL);
    if (!globalmem_devp){
        ret = -ENOMEM;
        goto fail_malloc;
    }
    //初始化互斥锁
    mutex_init(&globalmem_devp->mutex);
    //初始化等待队列
    init_waitqueue_head(&globalmem_devp->r_wait);
    init_waitqueue_head(&globalmem_devp->w_wait);
    globalmem_setup_cdev(globalmem_devp, 0);
    globalmem_class = class_create(THIS_MODULE, device_name);
    if (NULL == globalmem_class){
        ret = -EFAULT;
        goto fail_create_class;
    }
    globalmem_device = device_create(globalmem_class, NULL, devno, NULL, device_name);
    if (NULL == globalmem_device){
        ret = -EFAULT;
        goto fail_create_device;
    }
    
    return 0;
fail_create_device:
    class_destroy(globalmem_class);
fail_create_class:
    kfree(globalmem_devp);
fail_malloc:
    unregister_chrdev_region(devno, 1);
    return ret ;
}

module_init(globalmem_init);

static void __exit globalmem_exit(void)
{
    device_destroy(globalmem_class, MKDEV(globalmem_major, 0));
    class_destroy(globalmem_class);
    cdev_del(&globalmem_devp->cdev);
    kfree(globalmem_devp);
    unregister_chrdev_region(MKDEV(globalmem_major,0), 1);
}
module_exit(globalmem_exit);


MODULE_LICENSE("GPL v2");
