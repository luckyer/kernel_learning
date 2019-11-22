#include <linux/init.h>
#include <linux/module.h>

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello World init\n");
    
    return 0;
}

module_init(hello_init);

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Hello world exit");
}
module_exit(hello_exit);


MODULE_LICENSE("GPL v2");
