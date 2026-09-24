#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static const struct of_device_id chip_gpio_of_match[] = {
    {.compatible = "dp_industry,dp_led"},
    {}
};

struct dp_gpio_led_data {
    struct gpio_desc *gpio;
    struct led_classdev led_cdev;
};

static int dp_led_set_brightness(struct led_classdev *led_cdev,
                                 enum led_brightness brightness)
{
    struct dp_gpio_led_data *led_data;

    led_data = container_of(led_cdev, struct dp_gpio_led_data, led_cdev);
    gpiod_set_value_cansleep(led_data->gpio, brightness != LED_OFF);

    return 0;
}

static int chip_gpio_probe(struct platform_device *pdev)
{
    printk("------------------chip_gpio_probe----------------------------\n");
    struct device *dev = &pdev->dev;
    struct dp_gpio_led_data *led_data;
    struct led_init_data init_data = {
        .fwnode = dev_fwnode(dev),
    };
    int ret;

    led_data = devm_kzalloc(dev, sizeof(*led_data), GFP_KERNEL);
    if (!led_data)
        return -ENOMEM;
    
    led_data->gpio = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led_data->gpio))
        return dev_err_probe(dev, PTR_ERR(led_data->gpio), "Failed to get GPIO for LED\n");

    led_data->led_cdev.name = "dp-led";
    led_data->led_cdev.max_brightness = 1;
    led_data->led_cdev.brightness_set_blocking = dp_led_set_brightness;

    ret = devm_led_classdev_register_ext(dev, &led_data->led_cdev,
                                         &init_data);
    if (ret)
        return dev_err_probe(dev, ret, "Failed to register LED class device\n");

    platform_set_drvdata(pdev, led_data);
    dev_info(dev, "LED registered as /sys/class/leds/dp-led\n");
    return 0;
}



static struct platform_driver chip_gpio_driver = {
    .driver = {
        .name = "chip_gpio",
        .of_match_table = chip_gpio_of_match,
    },
    .probe = chip_gpio_probe,
};

module_platform_driver(chip_gpio_driver);
MODULE_DEVICE_TABLE(of, chip_gpio_of_match);
MODULE_LICENSE("GPL");
