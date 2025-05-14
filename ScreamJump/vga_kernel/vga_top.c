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
#include "vga_top.h"

#define DRIVER_NAME "vga_top"

/* Device registers */
#define WRITE_TILE(x) (x)
#define WRITE_SPRITE(x) (x+4) // it's byte addressed

/*
 * Information about our device
 */
struct vga_top_dev {
	struct resource res; /* Resource: our registers */
	void __iomem *virtbase; /* Where registers can be accessed in memory */
} dev;


static void write_tile(unsigned char r, unsigned char c, unsigned char n)
{
  // 5bit r, 6bit c, 8bit n 
	iowrite32(((unsigned int) r << 14) + ((unsigned int) c << 8) + n, WRITE_TILE(dev.virtbase) );
}

static void write_sprite(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n)
{
	unsigned int r_mask = (1 << 9) - 1;
	unsigned int c_mask = (1 << 10) - 1;
	unsigned int n_mask = (1 << 5) - 1;
        printk("act:%i   r:%i  c:%i  n:%i  register_n:%i   address:%i\n", active, r, c, n, register_n, WRITE_SPRITE(dev.virtbase + register_n) - dev.virtbase);
        printk("Hex form: %x\n",   ((unsigned int) active << 24) + 
				((unsigned int) ((r & r_mask) << 15)) +
				((unsigned int) ((c & c_mask) << 5)) +
				((unsigned int) (n & n_mask)));
	// 1bit active, 9bit r, 10bit c, 5bit n
	iowrite32( ((unsigned int) active << 24) + 
				((unsigned int) ((r & r_mask) << 15)) +
				((unsigned int) ((c & c_mask) << 5)) +
				((unsigned int) (n & n_mask)), WRITE_SPRITE(dev.virtbase + register_n*4)); // byte addressed
}

/*
 * Handle ioctl() calls from userspace:
 * Read or write the segments on single digits.
 * Note extensive error checking of arguments
 */
static long vga_top_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
  vga_top_arg_t vlat;
  vga_top_arg_s vlas;
	switch (cmd) {
	  case VGA_TOP_WRITE_TILE:
		  if (copy_from_user(&vlat, (vga_top_arg_t *) arg, sizeof(vga_top_arg_t)))
			  return -EACCES;
		  write_tile(vlat.r, vlat.c, vlat.n);
		  break;
          case VGA_TOP_WRITE_SPRITE:
                  printk("writing sprite:  ");
                  if (copy_from_user(&vlas, (vga_top_arg_s *) arg, sizeof(vga_top_arg_s)))
			  return -EACCES;
		  write_sprite(vlas.active, vlas.r, vlas.c, vlas.n, vlas.register_n);
		  break;
	  default:
		  return -EINVAL;
	}

	return 0;
}

/* The operations our device knows how to do */
static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = vga_top_ioctl,
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

	/* Register ourselves as a misc device: creates /dev/vga_top */
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
static int vga_top_remove(struct platform_device *pdev)
{
	iounmap(dev.virtbase);
	release_mem_region(dev.res.start, resource_size(&dev.res));
	misc_deregister(&misc_device);
	return 0;
}

/* Which "compatible" string(s) to search for in the Device Tree */
#ifdef CONFIG_OF
static const struct of_device_id device_of_match[] = {
	{ .compatible = "csee4840,vga_top-1.0" },
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
	.remove	= __exit_p(vga_top_remove),
};

/* Called when the module is loaded: set things up */
static int __init vga_top_init(void)
{
	pr_info(DRIVER_NAME ": init\n");
	return platform_driver_probe(&driver, probe);
}

/* Calball when the module is unloaded: release resources */
static void __exit vga_top_exit(void)
{
	platform_driver_unregister(&driver);
	pr_info(DRIVER_NAME ": exit\n");
}

module_init(vga_top_init);
module_exit(vga_top_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhz");
MODULE_DESCRIPTION("vga_top driver");
