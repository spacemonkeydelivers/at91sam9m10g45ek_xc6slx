#include "fpga-sk-at91sam9m10g45-xc6slx.h"

#define DEBUG

struct sk_fpga fpga;

static const struct file_operations fpga_fops = {
        .owner          = THIS_MODULE,
        .open           = sk_fpga_open,
        .release        = sk_fpga_close,
        .write          = sk_fpga_write,
        .read           = sk_fpga_read,
        .unlocked_ioctl = sk_fpga_ioctl,
};

static struct miscdevice sk_fpga_dev = {
        MISC_DYNAMIC_MINOR,
        "fpga",
        &fpga_fops
};

// FIXME: is it optimal way to calculate the pointer?
// TODO: rework this function
uint16_t* sk_fpga_ptr_by_addr (uint32_t addr)
{
    uint32_t rem = 0;
    BUG_ON(addr >= fpga.fpga_mem_window_size * 2);
    rem = (addr % fpga.fpga_mem_window_size);
    if (addr / fpga.fpga_mem_window_size)
    {
        return (uint16_t*)((uint8_t*)fpga.fpga_mem_virt_start_cs1 + rem);
    }
    else
    {
        return (uint16_t*)((uint8_t*)fpga.fpga_mem_virt_start_cs0 + rem);
    }
}

static int sk_fpga_open (struct inode *inode, struct file *file)
{
    if (fpga.opened) 
    {
        return -EBUSY;
    } 
    else 
    {
        fpga.opened++;
    }
    return 0;
}

static int sk_fpga_close (struct inode *inode, struct file *file)
{
    if (fpga.opened) 
    {
        fpga.opened--;
    } 
    else 
    {
        return -ENODEV;
    }
    return 0;
}

// FIXME: sk_fpga_ptr_by_addr doesn't check for window crossing
static ssize_t sk_fpga_read (struct file *file, char __user *buf,
                    size_t len, loff_t *ppos)
{
    int i = 0;
    int res = 0;
    uint16_t bytes_to_read = (TMP_BUF_SIZE < len) ? TMP_BUF_SIZE : len;
    uint16_t* start = sk_fpga_ptr_by_addr(fpga.address);
    // 2 since byte vs short
    BUG_ON(bytes_to_read % 2);
    for (; i < bytes_to_read; i += 2)
    {
        fpga.fpga_prog_buffer[i] = ioread16(start + i);
    }
    res = copy_to_user(buf, fpga.fpga_prog_buffer, bytes_to_read);
    return (bytes_to_read - res);
}

// Write data to FPGA if FPGA is not being programmed
static ssize_t sk_fpga_write(struct file *file, const char __user *buf,
                             size_t len, loff_t *ppos)
{
    int i = 0;
    uint16_t bytes_to_copy = (TMP_BUF_SIZE < len) ? TMP_BUF_SIZE : len;
    int res = copy_from_user(fpga.fpga_prog_buffer, buf, bytes_to_copy);
    fpga.transactionSize = bytes_to_copy;
    if (fpga.state != FPGA_READY_TO_PROGRAM)
    {
        uint16_t* start = sk_fpga_ptr_by_addr(fpga.address);
        // 2 since byte vs short
        BUG_ON(bytes_to_copy % 2);
        for (; i < bytes_to_copy; i += 2)
        {
            iowrite16(fpga.fpga_prog_buffer[i], start + i);
        }
    }
    return (bytes_to_copy - res);
}

static long sk_fpga_ioctl (struct file *f, unsigned int cmd, unsigned long arg)
{
    struct sk_fpga_data data = {0};
    uint8_t action = 0;
    uint8_t value = 0;
    switch (cmd)
    {
    // set current fpga ebi timings
    case SKFPGA_IOSSMCTIMINGS:
        if (copy_from_user(&fpga.smc_timings, (int __user *)arg, sizeof(struct sk_fpga_smc_timings)))
            return -EFAULT;
        if (sk_fpga_setup_smc())
            return -EFAULT;
        break;

    // Get current fpga ebi timings
    case SKFPGA_IOGSMCTIMINGS:
        if (sk_fpga_read_smc())
            return -EFAULT;
        if (copy_to_user((int __user *)arg, &fpga.smc_timings, sizeof(struct sk_fpga_smc_timings)))
            return -EFAULT;
        break;

    // write short to FPGA
    case SKFPGA_IOSDATA:
        if (copy_from_user(&data, (int __user *)arg, sizeof(struct sk_fpga_data)))
            return -EFAULT;
        iowrite16(data.data, sk_fpga_ptr_by_addr(data.address));
        break;

    // read short from FPGA
    case SKFPGA_IOGDATA:
        if (copy_from_user(&data, (int __user *)arg, sizeof(struct sk_fpga_data)))
            return -EFAULT;
        data.data = ioread16(sk_fpga_ptr_by_addr(data.address));
        if (copy_to_user((int __user *)arg, &data, sizeof(struct sk_fpga_data)))
            return -EFAULT;
        break;

    // programm FPGA, flow:
    // prepare to be programmed
    // write buf - flush buf - repeat
    // programming done
    case SKFPGA_IOSPROG:
        if (copy_from_user(&action, (int __user *)arg, sizeof(uint8_t)))
            return -EFAULT;
        if (action == FPGA_PROG_PREPARE)
        {
            sk_fpga_prepare_to_program();
        }
        else if (action == FPGA_PROG_FLUSH_BUF)
        {
            sk_fpga_program(fpga.fpga_prog_buffer, fpga.transactionSize);
        }
        else if (action == FPGA_PROG_FINISH)
        {
            sk_fpga_programming_done();
        }
        else
        {
            return -EFAULT;
        }
        break;

    // toggle reset ping
    case SKFPGA_IOSRESET:
        if (copy_from_user(&value, (int __user *)arg, sizeof(uint8_t)))
            return -EFAULT;
        // FIXME: refactor?
        if (value)
        {
            gpio_set_value(fpga.fpga_pins.fpga_reset, 1);
        }
        else
        {
            gpio_set_value(fpga.fpga_pins.fpga_reset, 0);
        }
        break;

    case SKFPGA_IOGRESET:
        value = gpio_get_value(fpga.fpga_pins.fpga_reset);
        if (copy_to_user((int __user *)arg, &value, sizeof(uint8_t)))
            return -EFAULT;
        break;

    case SKFPGA_IOSHOSTIRQ:
        if (copy_from_user(&value, (int __user *)arg, sizeof(uint8_t)))
            return -EFAULT;
        if (value)
        {
            gpio_set_value(fpga.fpga_pins.host_irq, 1);
        }
        else
        {
            gpio_set_value(fpga.fpga_pins.host_irq, 0);
        }
        break;
    
    case SKFPGA_IOGHOSTIRQ:
        value = gpio_get_value(fpga.fpga_pins.host_irq);
        if (copy_to_user((int __user *)arg, &value, sizeof(uint8_t)))
            return -EFAULT;
        break;

    case SKFPGA_IOSFPGAIRQ:
        printk(KERN_ALERT"TODO: implement registering irq handler later");
        break;
    
    case SKFPGA_IOGFPGAIRQ:
        value = gpio_get_value(fpga.fpga_pins.fpga_irq);
        if (copy_to_user((int __user *)arg, &value, sizeof(uint8_t)))
            return -EFAULT;
        break;

    default:
        return -ENOTTY;
    }
    return 0;
}

// TODO: reuse existing atmel ebi interfaces and data from dtb
int sk_fpga_setup_smc (void)
{
    int ret = -EIO;
    uint32_t __iomem* smc = NULL;

    uint32_t __iomem* ADDR_SETUP = NULL;
    uint32_t __iomem* ADDR_PULSE = NULL;
    uint32_t __iomem* ADDR_CYCLE = NULL;
    uint32_t __iomem* ADDR_MODE  = NULL;

    if (!request_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW, "sk_fpga_smc0")) 
    {
        printk(KERN_ALERT"Failed to request mem region for smd\n");
        return ret;
    }
    smc = ioremap(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    if (!smc) 
    {
        printk(KERN_ALERT"Failed to ioremap mem region for smc\n");
        release_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
        return ret;
    }

    ADDR_SETUP = smc;
    ADDR_PULSE = smc + 1;
    ADDR_CYCLE = smc + 2;
    ADDR_MODE  = smc + 3;

    iowrite32(fpga.smc_timings.setup, ADDR_SETUP);
    iowrite32(fpga.smc_timings.pulse, ADDR_PULSE);
    iowrite32(fpga.smc_timings.cycle, ADDR_CYCLE);
    iowrite32(fpga.smc_timings.mode, ADDR_MODE);
 
    iounmap(smc);
    release_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    return 0;    
}

// TODO: reuse existing atmel ebi interfaces and data from dtb
int sk_fpga_read_smc (void)
{
    int ret = -EIO;
    uint32_t __iomem* smc = NULL;

    uint32_t __iomem* ADDR_SETUP = NULL;
    uint32_t __iomem* ADDR_PULSE = NULL;
    uint32_t __iomem* ADDR_CYCLE = NULL;
    uint32_t __iomem* ADDR_MODE  = NULL;

    if (!request_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW, "sk_fpga_smc0")) 
    {
        printk(KERN_ALERT"Failed to request mem region for smd\n");
        return ret;
    }

    smc = ioremap(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    if (!smc) 
    {
        printk(KERN_ALERT"Failed to ioremap mem region for smc\n");
        release_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
        return ret;
    }

    ADDR_SETUP = smc;
    ADDR_PULSE = smc + 1;
    ADDR_CYCLE = smc + 2;
    ADDR_MODE  = smc + 3;

    fpga.smc_timings.setup = ioread32(ADDR_SETUP);
    fpga.smc_timings.pulse = ioread32(ADDR_PULSE);
    fpga.smc_timings.cycle = ioread32(ADDR_CYCLE);
    fpga.smc_timings.mode  = ioread32(ADDR_MODE);
 
    iounmap(smc);
    release_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    return 0;    
}

// TODO: read smc settings from dtb and initialize timings with them
int sk_fpga_fill_structure(struct platform_device *pdev)
{
    int ret = -EIO;
    
    // get FPGA clk source
    fpga.fpga_clk = devm_clk_get(&pdev->dev, "mclk");
    if (IS_ERR(fpga.fpga_clk)) 
    {
        dev_err(&pdev->dev, "Failed to get clk source for fpga from dtb\n");
        return ret;
    }

    // get fpga reset gpio
    fpga.fpga_pins.fpga_reset = of_get_named_gpio(pdev->dev.of_node, "fpga-reset-gpio", 0);
    if (!fpga.fpga_pins.fpga_reset) {
        dev_err(&pdev->dev, "Failed to obtain fpga reset pin\n");
        return ret;
    }

    // get fpga done gpio
    fpga.fpga_pins.fpga_done = of_get_named_gpio(pdev->dev.of_node, "fpga-program-done", 0);
    if (!fpga.fpga_pins.fpga_done) {
        dev_err(&pdev->dev, "Failed to obtain fpga done pin\n");
        return ret;
    }

    // get fpga cclk gpio
    fpga.fpga_pins.fpga_cclk = of_get_named_gpio(pdev->dev.of_node, "fpga-program-cclk", 0);
    if (!fpga.fpga_pins.fpga_cclk) {
        dev_err(&pdev->dev, "Failed to obtain fpga cclk pin\n");
        return ret;
    }

    // get fpga din gpio
    fpga.fpga_pins.fpga_din = of_get_named_gpio(pdev->dev.of_node, "fpga-program-din", 0);
    if (!fpga.fpga_pins.fpga_din) {
        dev_err(&pdev->dev, "Failed to obtain fpga din pin\n");
        return ret;
    }

    // get fpga prog gpio
    fpga.fpga_pins.fpga_prog = of_get_named_gpio(pdev->dev.of_node, "fpga-program-prog", 0);
    if (!fpga.fpga_pins.fpga_prog) {
        dev_err(&pdev->dev, "Failed to obtain fpga prog pin\n");
        return ret;
    }
    
    // get fpga reset pin
    fpga.fpga_pins.fpga_reset = of_get_named_gpio(pdev->dev.of_node, "fpga-reset-gpio", 0);
    if (!fpga.fpga_pins.fpga_reset) {
        dev_err(&pdev->dev, "Failed to obtain fpga reset pin\n");
        return ret;
    }

    // get fpga irq pin
    fpga.fpga_pins.fpga_irq = of_get_named_gpio(pdev->dev.of_node, "fpga-irq-gpio", 0);
    if (!fpga.fpga_pins.fpga_irq) {
        dev_err(&pdev->dev, "Failed to obtain fpga irq pin\n");
        return ret;
    }
    // get host irq pin
    fpga.fpga_pins.host_irq = of_get_named_gpio(pdev->dev.of_node, "fpga-host-irq-gpio", 0);
    if (!fpga.fpga_pins.host_irq) {
        dev_err(&pdev->dev, "Failed to obtain host irq pin\n");
        return ret;
    }

    // get fpga mem sizes
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-window-size", &fpga.fpga_mem_window_size);
    if (ret)
    {
        printk(KERN_ALERT"Failed to obtain fpga cs memory window size from dtb\n");
        return -ENOMEM;
    }

    // get FPGA phys start address for cs0
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-start-address-cs0", &fpga.fpga_mem_phys_start_cs0);
    if (ret) {
        printk(KERN_ALERT"Failed to obtain start phys mem start address from dtb\n");
        return -ENOMEM;
    }

    // get FPGA phys start address for cs1
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-start-address-cs1", &fpga.fpga_mem_phys_start_cs1);
    if (ret) {
        printk(KERN_ALERT"Failed to obtain start phys mem start address from dtb\n");
        return -ENOMEM;
    }

    // get FPGA frequency
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-frequency", &fpga.fpga_freq);
    if (ret) {
        printk(KERN_ALERT"Failed to obtain start phys mem start address from dtb\n");
        return -ENOMEM;
    }
    
    fpga.fpga_mem_virt_start_cs0 = NULL;
    fpga.fpga_mem_virt_start_cs1 = NULL;

    return 0;
}

static int sk_fpga_probe (struct platform_device *pdev)
{
    int ret = -EIO;
    memset(&fpga, 0, sizeof(fpga));
    fpga.pdev = pdev;
    fpga.state = FPGA_UNDEFINED;

    printk(KERN_ALERT"Loading FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");

    // register misc device
    ret = misc_register(&sk_fpga_dev);
    if (ret) {
        printk(KERN_ALERT"Unable to register \"fpga\" misc device\n");
        return -ENOMEM;
    }

    // fill structure by dtb info
    ret = sk_fpga_fill_structure(fpga.pdev);
    if (ret) {
        printk(KERN_ALERT"Failed to fill fpga structure out of dts\n");
        ret = -EINVAL;
        goto misc_dereg;
    }
    
    // allocate tmp buffer
    fpga.fpga_prog_buffer = kmalloc(TMP_BUF_SIZE, GFP_KERNEL);
    if (!fpga.fpga_prog_buffer) {
        printk(KERN_ALERT"Failed to allocate memory for tmp buffer");
        ret = -ENOMEM;
        goto misc_dereg;
    }

    // map phys 2 virt for both windows
    if (!request_mem_region(fpga.fpga_mem_phys_start_cs0, fpga.fpga_mem_window_size, "sk_fpga_mem_window_cs0")) {
        printk(KERN_ALERT"Failed to request mem region for sk_fpga_mem_window_cs0\n");
        ret = -ENOMEM;
        goto free_buf;
    }

    fpga.fpga_mem_virt_start_cs0 = ioremap(fpga.fpga_mem_phys_start_cs0, fpga.fpga_mem_window_size);
    if (!fpga.fpga_mem_virt_start_cs0) {
        printk(KERN_ALERT"Failed to ioremap mem region for sk_fpga_mem_window_cs0\n");
        ret = -ENOMEM;
        goto release_window_cs0;
    }

    if (!request_mem_region(fpga.fpga_mem_phys_start_cs1, fpga.fpga_mem_window_size, "sk_fpga_mem_window_cs1")) {
        printk(KERN_ALERT"Failed to request mem region for sk_fpga_mem_window_cs1\n");
        ret = -ENOMEM;
        goto unmap_window_cs0;
    }

    fpga.fpga_mem_virt_start_cs1 = ioremap(fpga.fpga_mem_phys_start_cs1, fpga.fpga_mem_window_size);
    if (!fpga.fpga_mem_virt_start_cs1) {
        printk(KERN_ALERT"Failed to ioremap mem region for sk_fpga_mem_window_cs1\n");
        ret = -ENOMEM;
        goto release_window_cs1;
    }

    ret = clk_set_rate(fpga.fpga_clk, fpga.fpga_freq);
    if (ret)
    {
        printk(KERN_ALERT"Failed to set clk rate for FPGA to %d", fpga.fpga_freq);
        ret = -EIO;
        goto unmap_window_cs1;
    }
    
    ret = clk_prepare_enable(fpga.fpga_clk);
    if (ret)
    {
        dev_err(&pdev->dev, "Couldn't enable FPGA clock\n");
        printk(KERN_ALERT"PREPARE  STATUS: %d\n", ret);
        goto unmap_window_cs1;
    }

    ret = gpio_request(fpga.fpga_pins.fpga_reset, "sk_fpga_reset_pin");
    if (ret)
    {
        printk(KERN_ALERT"Failed to acqiure reset pin");
        ret = -EIO;
        goto unmap_window_cs1;
    }

    ret = gpio_direction_output(fpga.fpga_pins.fpga_reset, 1);
    if (ret)
    {
        printk(KERN_ALERT"Failed to set reset pin as output");
        ret = -EIO;
        goto release_reset_pin;
    }

    ret = gpio_request(fpga.fpga_pins.fpga_irq, "sk_fpga_irq_pin");
    if (ret)
    {
        printk(KERN_ALERT"Failed to acqiure fpga irq pin");
        ret = -EIO;
        goto release_reset_pin;
    }

    ret = gpio_direction_input(fpga.fpga_pins.fpga_irq);
    if (ret)
    {
        printk(KERN_ALERT"Failed to set fpga irq pin as input");
        ret = -EIO;
        goto release_irq_pin;
    }

    ret = gpio_request(fpga.fpga_pins.host_irq, "sk_host_irq_pin");
    if (ret)
    {
        printk(KERN_ALERT"Failed to acqiure host irq pin");
        ret = -EIO;
        goto release_irq_pin;
    }

    ret = gpio_direction_output(fpga.fpga_pins.host_irq, 0);
    if (ret)
    {
        printk(KERN_ALERT"Failed to set host irq pin as output");
        ret = -EIO;
        goto release_host_irq_pin;
    }

    // device is not yet opened
    fpga.opened = 0;

    return ret;

release_host_irq_pin:
    gpio_free(fpga.fpga_pins.host_irq);
release_irq_pin:
    gpio_free(fpga.fpga_pins.fpga_irq);
release_reset_pin:
    gpio_free(fpga.fpga_pins.fpga_reset);
unmap_window_cs1:
    iounmap(fpga.fpga_mem_virt_start_cs1);
release_window_cs1:
    release_mem_region(fpga.fpga_mem_phys_start_cs1, fpga.fpga_mem_window_size);
unmap_window_cs0:
    iounmap(fpga.fpga_mem_virt_start_cs0);
release_window_cs0:
    release_mem_region(fpga.fpga_mem_phys_start_cs0, fpga.fpga_mem_window_size);
free_buf:
    kfree(fpga.fpga_prog_buffer);
misc_dereg:
    misc_deregister(&sk_fpga_dev);
    return ret;
}

static int sk_fpga_remove (struct platform_device *pdev)
{
    printk(KERN_ALERT"Removing FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");
    misc_deregister(&sk_fpga_dev);
    kfree(fpga.fpga_prog_buffer);
    fpga.state = FPGA_UNDEFINED;
    iounmap(fpga.fpga_mem_virt_start_cs0);
    release_mem_region(fpga.fpga_mem_phys_start_cs0, fpga.fpga_mem_window_size);
    iounmap(fpga.fpga_mem_virt_start_cs1);
    release_mem_region(fpga.fpga_mem_phys_start_cs1, fpga.fpga_mem_window_size);
    gpio_free(fpga.fpga_pins.fpga_reset);
    gpio_free(fpga.fpga_pins.fpga_irq);
    gpio_free(fpga.fpga_pins.host_irq);
    return 0;
}

// TODO: try to adopt existing FPGA spi programming code in kernel
int sk_fpga_prepare_to_program (void)
{
    int ret = 0;
    printk(KERN_ALERT"FPGA programming is started");
    // acquire pins to program FPGA
    gpio_free(fpga.fpga_pins.fpga_done);
    gpio_free(fpga.fpga_pins.fpga_din);
    gpio_free(fpga.fpga_pins.fpga_cclk);
    gpio_free(fpga.fpga_pins.fpga_prog);
    ret = gpio_request(fpga.fpga_pins.fpga_prog, "sk_fpga_prog_pin");
    if (ret) {
        printk(KERN_ALERT"Failed to allocate fpga prog pin");
        goto release_prog_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_prog, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_cclk, "sk_fpga_cclk_pin");
    if (ret) {
        printk(KERN_ALERT"Failed to allocate fpga cclk pin");
        goto release_cclk_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_cclk, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_din, "sk_fpga_din_pin");
    if (ret) {
        printk(KERN_ALERT"Failed to allocate fpga din pin");
        goto release_din_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_din, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_done, "sk_fpga_done_pin");
    if (ret) {
        printk(KERN_ALERT"Failed to allocate fpga done pin");
        goto release_done_pin;
    }
    gpio_direction_input(fpga.fpga_pins.fpga_done);

    // perform sort of firmware reset on fpga
    gpio_set_value(fpga.fpga_pins.fpga_prog, 0);
    gpio_set_value(fpga.fpga_pins.fpga_prog, 1);
    // set fpga state to be programmed
    fpga.state = FPGA_READY_TO_PROGRAM;
    return 0;

release_done_pin:
    gpio_free(fpga.fpga_pins.fpga_done);
release_din_pin:
    gpio_free(fpga.fpga_pins.fpga_din);
release_cclk_pin:
    gpio_free(fpga.fpga_pins.fpga_cclk);
release_prog_pin:
    gpio_free(fpga.fpga_pins.fpga_prog);
    return -ENODEV;
}

// TODO: refactoring needed
void sk_fpga_program (const uint8_t* buff, uint16_t bufLen)
{
    int i, j;
    unsigned char byte;
    unsigned char bit;
    for (i = 0; i < bufLen; i++) {
        byte = buff[i];
        for (j = 7; j >= 0; j--) {
            bit = 1 << j;
            bit &= byte;
            if (bit) {
                gpio_set_value(fpga.fpga_pins.fpga_din, 1);
            } else {
                gpio_set_value(fpga.fpga_pins.fpga_din, 0);
            }
            gpio_set_value(fpga.fpga_pins.fpga_cclk, 1);
            gpio_set_value(fpga.fpga_pins.fpga_cclk, 0);
        }
    }
}

// TODO: refactoring needed
int sk_fpga_programming_done (void)
{
    int counter, i, done = 0;
    enum fpga_state state = FPGA_PROGRAMMED;
    int ret = 0;
    gpio_set_value(fpga.fpga_pins.fpga_din, 1);
    done = gpio_get_value(fpga.fpga_pins.fpga_done);
    counter = 0;
    // toggle fpga clock while done signal appears
    while (!done) {
        gpio_set_value(fpga.fpga_pins.fpga_cclk, 1);
        gpio_set_value(fpga.fpga_pins.fpga_cclk, 0);
        done = gpio_get_value(fpga.fpga_pins.fpga_done);
        counter++;
        if (counter > MAX_WAIT_COUNTER) {
            printk(KERN_ALERT"Failed to get FPGA done pin as high");
            ret = -EIO;
            // might want to set it to undefined
            state = FPGA_READY_TO_PROGRAM;
            goto finish;
        }
    }
    // toggle clock a little bit just to ensure nothing's wrong
    for (i = 0; i < 10; i++) {
        gpio_set_value(fpga.fpga_pins.fpga_cclk, 1);
        gpio_set_value(fpga.fpga_pins.fpga_cclk, 0);
    }

finish:
    if (!ret)
        printk(KERN_ALERT"FPGA programming is done");
    // release program pins
    gpio_free(fpga.fpga_pins.fpga_done);
    gpio_free(fpga.fpga_pins.fpga_din);
    gpio_free(fpga.fpga_pins.fpga_cclk);
    gpio_free(fpga.fpga_pins.fpga_prog);
    // set fpga state as programmed
    fpga.state = state;
    return ret;
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
