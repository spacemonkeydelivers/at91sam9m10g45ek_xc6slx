#include "fpga-sk-at91sam9m10g45-xc6slx.h"

#define DEBUG

struct sk_fpga fpga;

static const struct file_operations fpga_fops = {
        .owner          = THIS_MODULE,
        .write          = sk_fpga_write,
        .read           = sk_fpga_read,
        .open           = sk_fpga_open,
        .release        = sk_fpga_close,
        .unlocked_ioctl = sk_fpga_ioctl,
};

static struct miscdevice sk_fpga_dev = {
        MISC_DYNAMIC_MINOR,
        "fpga",
        &fpga_fops
};

static int sk_fpga_open(struct inode *inode, struct file *file)
{
    if (fpga.opened) {
        return -EBUSY;
    } else {
        fpga.opened++;
    }
    return 0;
}

static int sk_fpga_close(struct inode *inodep, struct file *filp)
{
    if (fpga.opened) {
        fpga.opened--;
    }
    return 0;
}

static ssize_t sk_fpga_write(struct file *file, const char __user *buf,
                             size_t len, loff_t *ppos)
{
    // Write to fpga is only allowed when it's in programming state
    if (fpga.state == FPGA_READY_TO_PROGRAM) {
        uint16_t bytes_to_copy = (TMP_BUF_SIZE < len) ? TMP_BUF_SIZE : len;
        int res = copy_from_user(fpga.fpga_prog_buffer, buf, bytes_to_copy);
        _DBG("Copying to FPGA %d bytes, %d bytes left to copy", bytes_to_copy, res);
        sk_fpga_program(fpga.fpga_prog_buffer, (bytes_to_copy - res));
        return (bytes_to_copy - res);
    } else {
        return -ENOTTY;
    }
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

static long sk_fpga_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct sk_fpga_smc_timings timings = {0};
    enum fpga_state mode = FPGA_UNDEFINED;
    int pin_done_state = 0;
    switch (cmd)
    {
    // set current fpga ebi timings
    case SKFPGA_IOSSMCTIMINGS:
        if (copy_from_user(&timings, (int __user *)arg, sizeof(struct sk_fpga_smc_timings)))
            return -EFAULT;
        fpga.smc_timings.setup = timings.setup;
        fpga.smc_timings.pulse = timings.pulse;
        fpga.smc_timings.cycle = timings.cycle;
        fpga.smc_timings.mode  = timings.mode;
        if (!sk_fpga_setup_smc())
            return -EFAULT;
        break;
    // Get current fpga ebi timings
    case SKFPGA_IOQSMCTIMINGS:
        if (copy_to_user((int __user *)arg, &fpga.smc_timings, sizeof(struct sk_fpga_smc_timings)))
            return -EFAULT;
        break;
    // set current fpga mode
    case SKFPGA_IOSMODE:
        if (get_user(mode, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        if (mode == FPGA_READY_TO_PROGRAM) {
            if (sk_fpga_prepare_to_program())
                return -EFAULT;
        } else {
            return -EINVAL;
        }
        break;
    // Get current fpga mode
    case SKFPGA_IOQMODE:
        if (put_user(fpga.state, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        break;
    // call fpga prog finish when whole firmware has been programmed
    case SKFPGA_IOSPROG_DONE:
        if (sk_fpga_programming_done())
            return -EFAULT;
        break;
    // check status of done pin
    case SKFPGA_IOQPROG_DONE:
        // TODO: read actual done pin value
        pin_done_state = (fpga.state == FPGA_PROGRAMMED);
        if (put_user(pin_done_state, (int __user *)arg) == -EFAULT)
            return -EFAULT;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

// TODO: reuse existing atmel ebi interfaces
int sk_fpga_setup_smc(void)
{
    int ret = -EIO;
    uint32_t __iomem* smc = NULL;

    uint32_t __iomem* ADDR_SETUP = NULL;
    uint32_t __iomem* ADDR_PULSE = NULL;
    uint32_t __iomem* ADDR_CYCLE = NULL;
    uint32_t __iomem* ADDR_MODE  = NULL;

    if (!request_mem_region(SMC_ADDRESS, SMC_ADDRESS_WINDOW, "sk_fpga_smc0")) {
        _DBG(KERN_ERR"Failed to request mem region for smd\n");
        return ret;
    }
    smc = ioremap(SMC_ADDRESS, SMC_ADDRESS_WINDOW);
    if (!smc) {
        _DBG(KERN_ERR"Failed to ioremap mem region for smd\n");
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

int sk_fpga_fill_structure(struct platform_device *pdev)
{
    int ret = -EIO;

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

    // get fpga mem sizes
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-window-size", &fpga.fpga_mem_window_size);
    if (ret)
    {
        printk("Failed to obtain fpga cs memory window size from dtb\n");
        return -ENOMEM;
    }

    // get fpga start address
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-memory-start-address", &fpga.fpga_mem_phys_start);
    if (ret) {
        printk("Failed to obtain start phys mem start address from dtb\n");
        return -ENOMEM;
    }

    // get fpga smc setup
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-smc-setup", &fpga.smc_timings.setup);
    if (ret) {
        printk("Failed to obtain fpga smc timings for setup from dtb\n");
        return -ENOMEM;
    }

    // get fpga smc pulse
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-smc-pulse", &fpga.smc_timings.pulse);
    if (ret) {
        printk("Failed to obtain fpga smc timings for pulse from dtb\n");
        return -ENOMEM;
    }

    // get fpga smc cycle
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-smc-cycle", &fpga.smc_timings.cycle);
    if (ret) {
        printk("Failed to obtain fpga smc timings for cycle from dtb\n");
        return -ENOMEM;
    }

    // get fpga smc mode
    ret = of_property_read_u32(pdev->dev.of_node, "fpga-smc-mode", &fpga.smc_timings.mode);
    if (ret) {
        printk("Failed to obtain fpga smc timings for mode from dtb\n");
        return -ENOMEM;
    }

    fpga.fpga_mem_virt_start = NULL;

    return 0;
}

static int sk_fpga_probe (struct platform_device *pdev)
{
    int ret = -EIO;
    memset(&fpga, 0, sizeof(fpga));

    fpga.pdev = pdev;
    fpga.state = FPGA_UNDEFINED;

    _DBG("Loading FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");

    // register misc device
    ret = misc_register(&sk_fpga_dev);
    if (ret) {
        _DBG(KERN_ERR"Unable to register \"fpga\" misc device\n");
        return -ENOMEM;
    }

    // fill structure by dtb info
    ret = sk_fpga_fill_structure(fpga.pdev);
    if (ret) {
        _DBG(KERN_ERR"Failed to fill fpga structure out of dts\n");
        return -EINVAL;
    }

    // set reset pin to up
    gpio_request(fpga.fpga_pins.fpga_reset, "sk_fpga_reset_pin");
    gpio_direction_output(fpga.fpga_pins.fpga_reset, 1);
    gpio_set_value(fpga.fpga_pins.fpga_reset, 1);
    fpga.state = FPGA_RESET;

    // device is not yet opened
    fpga.opened = 0;

    return ret;
}

// TODO: try to adopt existing FPGA spi programming code in kernel
int sk_fpga_prepare_to_program(void)
{
    int ret = 0;
    _DBG("FPGA programming is started");
    // acquire pins to program FPGA
    ret = gpio_request(fpga.fpga_pins.fpga_prog, "sk_fpga_prog_pin");
    if (ret) {
        _DBG("Failed to allocate fpga prog pin");
        goto release_prog_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_prog, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_cclk, "sk_fpga_cclk_pin");
    if (ret) {
        _DBG("Failed to allocate fpga cclk pin");
        goto release_cclk_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_cclk, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_din, "sk_fpga_din_pin");
    if (ret) {
        _DBG("Failed to allocate fpga din pin");
        goto release_din_pin;
    }
    gpio_direction_output(fpga.fpga_pins.fpga_din, 1);
    ret = gpio_request(fpga.fpga_pins.fpga_done, "sk_fpga_done_pin");
    if (ret) {
        _DBG("Failed to allocate fpga done pin");
        goto release_done_pin;
    }
    gpio_direction_input(fpga.fpga_pins.fpga_done);

    // perform sort of firmware reset on fpga
    gpio_set_value(fpga.fpga_pins.fpga_prog, 0);
    gpio_set_value(fpga.fpga_pins.fpga_prog, 1);
    // allocate tmp buffer
    fpga.fpga_prog_buffer = kmalloc(TMP_BUF_SIZE, GFP_KERNEL);
    if (!fpga.fpga_prog_buffer) {
        _DBG("Failed to allocate memory for tmp buffer");
        return -ENOMEM;
    }
    // set fpga state to be programmed
    fpga.state = FPGA_READY_TO_PROGRAM;
    return 0;

release_done_pin:
    gpio_free(fpga.fpga_pins.fpga_done);
release_din_pin:
    gpio_free(fpga.fpga_pins.fpga_cclk);
release_cclk_pin:
    gpio_free(fpga.fpga_pins.fpga_cclk);
release_prog_pin:
    gpio_free(fpga.fpga_pins.fpga_prog);
    return -ENODEV;
}

int sk_fpga_programming_done(void)
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
            _DBG("Failed to get FPGA done pin as high");
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
        _DBG("FPGA programming is done");
    // release program pins
    gpio_free(fpga.fpga_pins.fpga_done);
    gpio_free(fpga.fpga_pins.fpga_cclk);
    gpio_free(fpga.fpga_pins.fpga_cclk);
    gpio_free(fpga.fpga_pins.fpga_prog);
    // release tmp buffer
    kfree(fpga.fpga_prog_buffer);
    // set fpga state as programmed
    fpga.state = state;
    return ret;
}

void sk_fpga_program(const uint8_t* buff, uint16_t bufLen)
{
    int i, j;
    unsigned char byte;
    unsigned char bit;
    _DBG("Programming %d bytes", bufLen);
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

static int sk_fpga_remove(struct platform_device *pdev)
{
    _DBG(KERN_ALERT"Removing FPGA driver for SK-AT91SAM9M10G45EK-XC6SLX\n");
    misc_deregister(&sk_fpga_dev);
    gpio_free(fpga.fpga_pins.fpga_reset);
    if (fpga.fpga_prog_buffer)
        kfree(fpga.fpga_prog_buffer);
    fpga.state = FPGA_UNDEFINED;
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
