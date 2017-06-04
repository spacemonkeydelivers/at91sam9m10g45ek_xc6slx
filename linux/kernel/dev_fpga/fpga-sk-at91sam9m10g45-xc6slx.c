#include "fpga-sk-at91sam9m10g45-xc6slx.h"

struct sk_fpga fpga;

static int sk_fpga_mmap(struct file *file, struct vm_area_struct * vma)
{
    //NOTE: we should really protect these by mutexes and stuff...
	unsigned long start      = fpga.fpga_mem_phys_start;
	unsigned long len        = fpga.fpga_mem_window_size;
	unsigned long mmio_pgoff = PAGE_ALIGN((start & ~PAGE_MASK) + len) >> PAGE_SHIFT;

    int err = 0;

	if (vma->vm_pgoff >= mmio_pgoff) {
		vma->vm_pgoff -= mmio_pgoff;
	}

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

    //io_remap_pfn_range call...
    err = vm_iomap_memory(vma, start, len);
    if (err) {
        printk(KERN_ALERT"fpga mmap failed :(\n");
        return err;
    }
    return 0;
}

static long sk_fpga_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    int requested_mode = -1;
    switch (cmd)
    {
    case SKFP_IOCQSIZE:
        if (put_user(fpga.fpga_mem_window_size, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        break;
    case SKFP_IOCQCMODE:
        if (put_user(fpga.mmap_mode, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        break;
    case SKFP_IOCSCMODE:
        if (get_user(requested_mode, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        if (    (requested_mode != SKFP_MMAP_MODE_DEFAULT)
            &&  (requested_mode != SKFP_MMAP_MODE_WB)
            &&  (requested_mode != SKFP_MMAP_MODE_WC)
            &&  (requested_mode != SKFP_MMAP_MODE_WT))
        {
            return -EINVAL;
        }
        fpga.mmap_mode = requested_mode;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}
static int sk_fpga_open(struct inode *inode, struct file *file)
{
    unsigned long remap_flags = 0;
    if (!request_mem_region(fpga.fpga_mem_phys_start,
                            fpga.fpga_mem_window_size,
                            "sk_fpga_memory"))
    {
        printk(KERN_ERR"Failed to request mem region\n");
        return -EBUSY;
    }
    switch (fpga.mmap_mode)
    {
    case SKFP_MMAP_MODE_DEFAULT:
        fpga.fpga_mem_virt_start = ioremap(fpga.fpga_mem_phys_start,
                                           fpga.fpga_mem_window_size);
        if (!fpga.fpga_mem_virt_start)
        {
            printk(KERN_ERR"Failed to ioremap mem region\n");
            //TODO: we have a memory leak here...
            return -EBUSY;
        }
        break;
    case SKFP_MMAP_MODE_WB:
    case SKFP_MMAP_MODE_WC:
    case SKFP_MMAP_MODE_WT:
        if (fpga.mmap_mode == SKFP_MMAP_MODE_WB)
            remap_flags = MEMREMAP_WB;
        if (fpga.mmap_mode == SKFP_MMAP_MODE_WC)
            remap_flags = MEMREMAP_WC;
        if (fpga.mmap_mode == SKFP_MMAP_MODE_WT)
            remap_flags = MEMREMAP_WT;
        fpga.fpga_mem_virt_start = memremap(fpga.fpga_mem_phys_start,
                                            fpga.fpga_mem_window_size,
                                            remap_flags);
        if (!fpga.fpga_mem_virt_start)
        {
            printk(KERN_ERR"Failed to memremap map region... falgs = %ld\n", remap_flags);
            //TODO: we have a memory leak here too...
            return -EBUSY;
        }
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static int sk_fpga_close(struct inode *inodep, struct file *filp)
{
    if (fpga.fpga_mem_virt_start)
    {
        iounmap(fpga.fpga_mem_virt_start);
    }
    release_mem_region(fpga.fpga_mem_phys_start, fpga.fpga_mem_window_size);
    return 0;
}

static ssize_t sk_fpga_write(struct file *file, const char __user *buf,
		             size_t len, loff_t *ppos)
{
    uint16_t data = 0;
    uint16_t res = copy_from_user(&data, buf, sizeof(uint16_t));
    printk(KERN_ERR"Writing first short: %x\n", data);
    writew(data, fpga.fpga_mem_virt_start);
    return (sizeof(uint16_t) - res);
}

static ssize_t sk_fpga_read(struct file *file, char __user *buf,
		            size_t len, loff_t *ppos)
{
    uint16_t data = readw(fpga.fpga_mem_virt_start);
    uint16_t res = 0;
    printk(KERN_ERR"Reading first short: %x\n", data);
    res = copy_to_user(buf, &data, sizeof(data));
    return (sizeof(uint16_t) - res);
}

static const struct file_operations fpga_fops = {
        .owner          = THIS_MODULE,
        .write			= sk_fpga_write,
        .read			= sk_fpga_read,
        .open			= sk_fpga_open,
        .unlocked_ioctl = sk_fpga_ioctl,
        .release		= sk_fpga_close,
        .mmap           = sk_fpga_mmap
};

static struct miscdevice sk_fpga_dev = {
        MISC_DYNAMIC_MINOR,
        "fpga",
        &fpga_fops
};


//TODO: remove magic numbers
int sk_fpga_setup_smc(void)
{
    int ret = -EIO;
    uint32_t __iomem* smc = NULL;

    uint32_t __iomem* ADDR_SETUP = NULL;
    uint32_t __iomem* ADDR_PULSE = NULL;
    uint32_t __iomem* ADDR_CYCLE = NULL;
    uint32_t __iomem* ADDR_MODE  = NULL;

    if (!request_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW, "sk_fpga_smc0"))
    {
        printk(KERN_ERR"Failed to request mem region for smd\n");
        return ret;
    }
    smc = ioremap(SMC_ADDRESS, 0xff);
    if (!smc)
    {
        printk(KERN_ERR"Failed to ioremap mem region for smd\n");
        return ret;
    }

    ADDR_SETUP = smc;
    ADDR_PULSE = smc + 1;
    ADDR_CYCLE = smc + 2;
    ADDR_MODE  = smc + 3;

    iowrite32(SMC_SETUP_DATA,ADDR_SETUP );
    iowrite32(SMC_PULSE_DATA, ADDR_PULSE);
    iowrite32(SMC_CYCLE_DATA, ADDR_CYCLE);
    iowrite32(SMC_MODE_DATA, ADDR_MODE);
 
    iounmap(smc);
    release_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    
    return 0;    
}

int sk_fpga_fill_structure(struct platform_device *pdev)
{
    int ret = -EIO;

    // get fpga irq gpio
    fpga.fpga_irq_pin = of_get_named_gpio(pdev->dev.of_node, "fpga-irq-gpio", 0);
    if (!fpga.fpga_irq_pin) {
        dev_err(&pdev->dev, "Failed to obtain fpga irq pin\n");
        return ret;
    }

    // get fpga reset gpio
    fpga.fpga_reset_pin = of_get_named_gpio(pdev->dev.of_node, "fpga-reset-gpio", 0);
    if (!fpga.fpga_reset_pin) {
        dev_err(&pdev->dev, "Failed to obtain fpga reset pin\n");
        return ret;
    }

    // get fpga done gpio
    fpga.fpga_done = of_get_named_gpio(pdev->dev.of_node, "fpga-program-done", 0);
    if (!fpga.fpga_done) {
        dev_err(&pdev->dev, "Failed to obtain fpga done pin\n");
        return ret;
    }

    // get fpga cclk gpio
    fpga.fpga_cclk = of_get_named_gpio(pdev->dev.of_node, "fpga-program-cclk", 0);
    if (!fpga.fpga_cclk) {
        dev_err(&pdev->dev, "Failed to obtain fpga cclk pin\n");
        return ret;
    }

    // get fpga din gpio
    fpga.fpga_din = of_get_named_gpio(pdev->dev.of_node, "fpga-program-din", 0);
    if (!fpga.fpga_din) {
        dev_err(&pdev->dev, "Failed to obtain fpga din pin\n");
        return ret;
    }

    // get fpga prog gpio
    fpga.fpga_prog = of_get_named_gpio(pdev->dev.of_node, "fpga-program-prog", 0);
    if (!fpga.fpga_prog) {
        dev_err(&pdev->dev, "Failed to obtain fpga prog pin\n");
        return ret;
    }

    // get fpga mem sizes
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-window-size", &fpga.fpga_mem_window_size);
    if (ret != 0)
    {
        printk("Failed to obtain fpga cs memory window size from dtb\n");
        return -ENOMEM;
    }

    // get fpga start address
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-start-address", &fpga.fpga_mem_phys_start);
    if (ret != 0)
    {
        printk("Failed to obtain start phys mem start address from dtb\n");
        return -ENOMEM;
    }

    fpga.fpga_irq_num = -1;
    fpga.fpga_mem_virt_start = NULL;
    init_waitqueue_head(&fpga.fpga_wait_queue);

    // get fpga clk source
    fpga.fpga_clk = devm_clk_get(&pdev->dev, "mclk");
    if (IS_ERR(fpga.fpga_clk)) {
        dev_err(&pdev->dev, "Failed to get clk source for fpga from dtb\n");
        return ret;
    }

    fpga.mmap_mode = SKFP_MMAP_MODE_DEFAULT;
    return 0;
}

static int sk_fpga_probe (struct platform_device *pdev)
{
    int ret = -EIO;

    memset(&fpga, 0, sizeof(fpga));

    fpga.pdev = pdev;

    printk("Loading FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");

    // register misc device
    ret = misc_register(&sk_fpga_dev);
    if (ret)
    {
        printk(KERN_ERR"Unable to register \"fpga\" misc device\n");
        return -ENOMEM;
    }

    // fill structure by dtb info
    ret = sk_fpga_fill_structure(fpga.pdev);
    if (ret)
    {
        printk(KERN_ERR"Failed to fill fpga structure out of dts\n");
        return -EINVAL;
    }

    // run fpga clocking source
    ret = clk_set_rate(fpga.fpga_clk, SK_133MHZ_CLOCK_RATE);
    if (ret) 
    {
        dev_err(&pdev->dev, "Could not set fpga clk rate as %d\n", SK_133MHZ_CLOCK_RATE);
        return ret;
    }
    printk(KERN_ERR"Current clk rate: %ld\n", clk_get_rate(fpga.fpga_clk));
    ret = clk_prepare_enable(fpga.fpga_clk);
    if (ret)
    {
        dev_err(&pdev->dev, "Couldn't enable fpga clock\n");
    }

    // set reset ping to up   
    gpio_request(fpga.fpga_reset_pin, "sk_fpga_reset_pin");
    gpio_direction_output(fpga.fpga_reset_pin, 1);
    gpio_set_value(fpga.fpga_reset_pin, 1);

    ret = sk_fpga_setup_smc();
 
    return ret;
}

static int sk_fpga_remove(struct platform_device *pdev)
{
    printk(KERN_ALERT"Removing FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");
    // stop clocking source
    misc_deregister(&sk_fpga_dev);
    gpio_free(fpga.fpga_reset_pin);
    return 0;
}

static const struct of_device_id sk_fpga_of_match_table[] = {
    { .compatible = "sk,at91-xc6slx", },
    { /* end of list */ }
};
MODULE_DEVICE_TABLE(of, sk_fpga_of_match_table);

static struct platform_driver sk_fpga_driver = {
    .probe         = sk_fpga_probe,
    .remove        = sk_fpga_remove,
    .driver        = {
        .owner          = THIS_MODULE,
        .name           = "fpga",
        .of_match_table = of_match_ptr(sk_fpga_of_match_table),
    },
};

module_platform_driver(sk_fpga_driver);
MODULE_AUTHOR("Alexey Baturo <baturo.alexey@gmail.com>");
MODULE_DESCRIPTION("Driver for Xilinx Spartan6 xc6slx16 fpga for StarterKit AT91SAM9M10G45EK-XC6SLX board");
MODULE_LICENSE("GPL v2");
