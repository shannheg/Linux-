#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "led_opr.h"

static int major;
static struct class *led_class;
static struct led_operation *led_oprs[LED_MAX];
static DEFINE_MUTEX(led_lock);

static ssize_t led_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
    struct led_operation *opr = file->private_data;
    char value; // 用于将用户空间读取的值暂存
    int minor; // 从本次打开的字符设备获取设备号
    int status; // 本次 LED 的状态
    int ret;

    if (!count)
        return -EINVAL;

    if (copy_from_user(&value, buf, 1)) // 从用户空间读取 1 个数据到 value 中
        return -EFAULT;

    if (value == '1')
        status = 1;
    else if (value == '0')
        status = 0;
    else
        return -EINVAL;

    minor = iminor(file_inode(file));//获取当前打开的设备号
    if (!opr || !opr->control || minor < 0 || minor >= LED_MAX)
        return -ENODEV;

    ret = opr->control(minor, status);
    if (ret)
        return ret;

    return 1;
}

static int led_open(struct inode *inode, struct file *file)
{
    struct led_operation *opr;
    int minor = iminor(inode);
    int ret;

    if (minor < 0 || minor >= LED_MAX)
        return -ENODEV;

    mutex_lock(&led_lock);
    opr = led_oprs[minor];
    if (!opr || !opr->init || !opr->control || !try_module_get(opr->owner)) {
        mutex_unlock(&led_lock);
        return -ENODEV;
    }
    file->private_data = opr;
    mutex_unlock(&led_lock);

    // 根据当前打开的次设备号初始化对应 LED
    ret = opr->init(minor);
    if (ret) {
        file->private_data = NULL;
        module_put(opr->owner);
    }

    return ret;
}

static int led_release(struct inode *inode, struct file *file)
{
    struct led_operation *opr = file->private_data;

    if (opr)
        module_put(opr->owner);

    return 0;
}

static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .write = led_write,
};

int led_device_create(int minor, struct led_operation *opr)
{
    struct device *dev;

    if (minor < 0 || minor >= LED_MAX || !opr || !opr->init || !opr->control)
        return -EINVAL;

    mutex_lock(&led_lock);
    if (led_oprs[minor]) {
        mutex_unlock(&led_lock);
        return -EBUSY;
    }

    led_oprs[minor] = opr;
    // 创建具体的设备节点，minor 区分不同 LED
    dev = device_create(led_class, NULL, MKDEV(major, minor), NULL,
                        "leddrv-dpled%d", minor);
    if (IS_ERR(dev)) {
        led_oprs[minor] = NULL;
        mutex_unlock(&led_lock);
        return PTR_ERR(dev);
    }

    mutex_unlock(&led_lock);
    return 0;
}

void led_device_destroy(int minor)
{
    if (minor < 0 || minor >= LED_MAX)
        return;

    mutex_lock(&led_lock);
    if (led_oprs[minor]) {
        led_oprs[minor] = NULL;
        device_destroy(led_class, MKDEV(major, minor));
    }
    mutex_unlock(&led_lock);
}

int led_init(void)
{
    //入口函数
    // 注册字符设备主设备号，并将设备号范围与 file_operations 结构体绑定
    major = register_chrdev(0, "dp_led", &led_fops);
    if (major < 0)
        return major;

    // 创建同类设备 class，为后续 device_create 创建设备节点提供支持
    led_class = class_create(THIS_MODULE, "my_led");
    if (IS_ERR(led_class)) {
        int ret = PTR_ERR(led_class);

        unregister_chrdev(major, "dp_led");
        return ret;
    }

    return 0;
}

void led_exit(void)
{
    class_destroy(led_class);
    unregister_chrdev(major, "dp_led");
}
