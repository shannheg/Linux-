#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/mutex.h>

#include <linux/uaccess.h>
#include <linux/device.h>

#define MIN(a,b) (a<b?a:b)
//主设备号
static int major = 0;
static char kernel_buf[1024];
static struct class *hello_class;



static ssize_t hello_read(struct file *file, char __user *buf, size_t size, loff_t *offset)//读取的文件 用户数组 大小 偏移
{
    int ret;
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    ret = copy_to_user(buf, kernel_buf, MIN(1024, size));
    if (ret)
        return -EFAULT;
    return MIN(1024, size);
};

static ssize_t hello_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)//读取的文件 用户数组 大小 偏移
{
    int ret;
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    ret = copy_from_user(kernel_buf, buf, MIN(1024, size));
    if (ret)
        return -EFAULT;
    return MIN(1024, size);
};

static int hello_open(struct inode *node, struct file *file)
{
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    printk("this is dp open\n");
    return 0;
};

static int hello_release(struct inode *node, struct file *file)
{
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    printk("this is dp release\n");
    return 0;
};




//这个驱动的结构体
static struct file_operations hello_drv = {
    .owner = THIS_MODULE,
    .open = hello_open,
    .read = hello_read,
    .write = hello_write,
    .release = hello_release,
};

static int __init hello_init(void)
{
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    major = register_chrdev(0, "hello", &hello_drv);
    //注册
    hello_class = class_create(THIS_MODULE, "hello_class");
    if(IS_ERR(hello_class)){
        printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
        unregister_chrdev(major, "hello");
        return -1;       
    }
    device_create(hello_class, NULL, MKDEV(major, 0), NULL, "hello");//创建设备节点

	return 0;
}
static void __exit hello_exit(void)
{
    printk("%s, %s, line %d \n ------------------make by dp\n", __FILE__, __func__, __LINE__);
    device_destroy(hello_class, MKDEV(major, 0));
    class_destroy(hello_class);
    unregister_chrdev(major, "hello");
}

module_init(hello_init);//把函数的入口和出口修饰，能够使用
module_exit(hello_exit);

MODULE_LICENSE("GPL");
