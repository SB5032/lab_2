/* * Device driver for the audio recognition hardware
 *
 * A Platform device implemented using the misc subsystem
 *
 * Stephen A. Edwards
 * Columbia University
 *
 * References:
 * Linux source: Documentation/driver-model/platform.txt
 *               drivers/misc/arm-charlcd.c
 * http://www.linuxforu.com/tag/linux-device-drivers/
 * http://free-electrons.com/docs/
 *
 * "make" to build
 * insmod aud.ko
 *
 * Check code style with
 * checkpatch.pl --file --no-tree aud.c
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "aud.h"

#define DRIVER_NAME "aud"

/* Device registers */
#define AUD_AMP(x) (x)       //0
#define MENU_BUTTON(x) ((x)+4)  //1 
#define SOUND(x) ((x)+8)   // 2

/*
 * Information about our device
 */
struct aud_dev {
	struct resource res; /* Resource: our registers */
	void __iomem *virtbase; /* Where registers can be accessed in memory */
	aud_mem_t memory;
} dev;

/*
 * Write segments of a single digit
 * Assumes digit is in range and the device information has been set up
 */
// static void read_audio(aud_amp_t *audio)
// {
// 	audio->amplitude = ioread32(AUD_AMP(dev.virtbase));
	// dev.audio = *audio;
// }


static void write_sound(aud_mem_t *memory)
{
	iowrite32(memory->sound, SOUND(dev.virtbase));
	dev.memory = *memory;
}


static void read_memory(aud_mem_t *memory)
{
	memory->data = ioread32(AUD_AMP(dev.virtbase));
}

static void read_button(aud_mem_t *memory){
	memory->button_value = ioread32(MENU_BUTTON(dev.virtbase));
}
	// dev.audio = *audio;
/*
 * Handle ioctl() calls from userspace:
 * Read or write the segments on single digits.
 * Note extensive error checking of arguments
 */
static long aud_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	aud_arg_t vla;
	switch (cmd) {
		case AUD_READ_DATA:
			read_memory(&vla.memory);
			if (copy_to_user((aud_arg_t *) arg, &vla, sizeof(aud_arg_t)))
				return -EACCES;
			break;	
		case AUD_WRITE_SOUND:
			if (copy_from_user(&vla, (aud_arg_t *) arg, sizeof(aud_arg_t)))
				return -EACCES;
			write_sound(&vla.memory);	
			break;
		case AUD_READ_BUTTON:
			read_button(&vla.memory);
			if (copy_to_user((aud_arg_t *) arg, &vla, sizeof(aud_arg_t)))
				return -EACCES;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

/* The operations our device knows how to do */
static const struct file_operations aud_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = aud_ioctl,
};

/* Information about our device for the "misc" framework -- like a char dev */
static struct miscdevice aud_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DRIVER_NAME,
	.fops		= &aud_fops,
};

/*
 * Initialization code: get resources (registers) and display
 * a welcome message
 */
static int __init aud_probe(struct platform_device *pdev)
{
	int ret;

	/* Register ourselves as a misc device: creates /dev/aud */
	ret = misc_register(&aud_misc_device);

	/* Get the address of our registers from the device tree */
	ret = of_address_to_resource(pdev->dev.of_node, 0, &dev.res);
	if (ret) {
		ret = -ENOENT;
		goto out_deregister;
	}

	/* Make sure we can use these registers */
	if (request_mem_region(dev.res.start, resource_size(&dev.res),
				DRIVER_NAME) == NULL) {
		ret = -EBUSY;
		goto out_deregister;
	}

	/* Arrange access to our registers */
	dev.virtbase = of_iomap(pdev->dev.of_node, 0);
	if (dev.virtbase == NULL) {
		ret = -ENOMEM;
		goto out_release_mem_region;
	}

	return 0;

out_release_mem_region:
	release_mem_region(dev.res.start, resource_size(&dev.res));
out_deregister:
	misc_deregister(&aud_misc_device);
	return ret;
}

/* Clean-up code: release resources */
static int aud_remove(struct platform_device *pdev)
{
	iounmap(dev.virtbase);
	release_mem_region(dev.res.start, resource_size(&dev.res));
	misc_deregister(&aud_misc_device);
	return 0;
}

/* Which "compatible" string(s) to search for in the Device Tree */
#ifdef CONFIG_OF
static const struct of_device_id aud_of_match[] = {
	{ .compatible = "csee4840,aud-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, aud_of_match);
#endif

/* Information for registering ourselves as a "platform" driver */
static struct platform_driver aud_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(aud_of_match),
	},
	.remove	= __exit_p(aud_remove),
};

/* Called when the module is loaded: set things up */
static int __init aud_init(void)
{
	pr_info(DRIVER_NAME ": init\n");
	return platform_driver_probe(&aud_driver, aud_probe);
}

/* Calball when the module is unloaded: release resources */
static void __exit aud_exit(void)
{
	platform_driver_unregister(&aud_driver);
	pr_info(DRIVER_NAME ": exit\n");
}

module_init(aud_init);
module_exit(aud_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("asy2126");
MODULE_DESCRIPTION("Audio Input driver");
