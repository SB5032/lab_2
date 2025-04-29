// adapted from vga_ball.c

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
#include "fpga_audio.h"

#define DRIVER_NAME "fpga_audio"

/* Device registers */
#define BGM_PLAY(x) (x)
#define AUDIO_DATA_ADDR_REG(x) (x+1)

/*
 * Information about our device
 */
struct fpga_audio_dev {
	struct resource res; /* Resource: our registers */
	void __iomem *virtbase; /* Where registers can be accessed in memory */
} dev;


static void bgm_startstop(unsigned char s)
{
  //pr_info("writing %d\n", s);
	iowrite8(s, BGM_PLAY(dev.virtbase) );
}

static void set_audio_data_address(unsigned char addr)
{
  iowrite8(addr, AUDIO_DATA_ADDR_REG(dev.virtbase));
}

/*
 * Handle ioctl() calls from userspace:
 * Read or write the segments on single digits.
 * Note extensive error checking of arguments
 */
static long fpga_audio_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
  fpga_audio_arg_t vla;
	switch (cmd) {
	  case FPGA_AUDIO_BGM_STARTSTOP:
		  if (copy_from_user(&vla, (fpga_audio_arg_t *) arg, sizeof(fpga_audio_arg_t)))
			  return -EACCES;
		  bgm_startstop(vla.play);
		  break;
    case FPGA_AUDIO_SET_AUDIO_ADDR:
		  if (copy_from_user(&vla, (fpga_audio_arg_t *) arg, sizeof(fpga_audio_arg_t)))
			  return -EACCES;
      set_audio_data_address(vla.play);
      break;

	  default:
		  return -EINVAL;
	}

	return 0;
}

/* The operations our device knows how to do */
static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = fpga_audio_ioctl,
};

/* Information about our device for the "misc" framework -- like a char dev */
static struct miscdevice misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DRIVER_NAME,
	.fops		= &fops,
};

/*
 * Initialization code: get resources (registers) and display
 * a welcome message
 */
static int __init probe(struct platform_device *pdev)
{
	int ret;

	/* Register ourselves as a misc device: creates /dev/fpga_audio */
	ret = misc_register(&misc_device);

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
	misc_deregister(&misc_device);
	return ret;
}

/* Clean-up code: release resources */
static int fpga_audio_remove(struct platform_device *pdev)
{
	iounmap(dev.virtbase);
	release_mem_region(dev.res.start, resource_size(&dev.res));
	misc_deregister(&misc_device);
	return 0;
}

/* Which "compatible" string(s) to search for in the Device Tree */
#ifdef CONFIG_OF
static const struct of_device_id device_of_match[] = {
	{ .compatible = "csee4840,fpga_audio-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, device_of_match);
#endif

/* Information for registering ourselves as a "platform" driver */
static struct platform_driver driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(device_of_match),
	},
	.remove	= __exit_p(fpga_audio_remove),
};

/* Called when the module is loaded: set things up */
static int __init fpga_audio_init(void)
{
	pr_info(DRIVER_NAME ": init\n");
	return platform_driver_probe(&driver, probe);
}

/* Calball when the module is unloaded: release resources */
static void __exit fpga_audio_exit(void)
{
	platform_driver_unregister(&driver);
	pr_info(DRIVER_NAME ": exit\n");
}

module_init(fpga_audio_init);
module_exit(fpga_audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhz");
MODULE_DESCRIPTION("fpga_audio driver");
