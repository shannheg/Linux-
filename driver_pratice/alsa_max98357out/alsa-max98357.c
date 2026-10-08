#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static const struct of_device_id alsa_max98357_match[] = {
    {.compatible = "dp_industry,alsa_max98357"},
    {}
};

static int alsa_max98357_probe(struct platform_device *pdev){
    printk("----------------------alsa_max98357_probe----------------------------\n");
    struct device *dev = &pdev->dev;

}

static struct platform_device alsa_max98357_device = {
    .probe = alsa_max98357_probe,
    .driver = {
        .name = "alsa_max98357",
        .of_match_table = alsa_max98357_of_match,
    },
};