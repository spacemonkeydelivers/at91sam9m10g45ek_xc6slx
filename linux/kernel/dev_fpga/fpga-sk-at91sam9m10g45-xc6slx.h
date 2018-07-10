#ifndef SK_FPGA_DRIVER_HEADER
#define SK_FPGA_DRIVER_HEADER

// TODO: I don't think we need so many includes

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <generated/utsrelease.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/ioctl.h>
#include <linux/platform_data/atmel.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>


#include <linux/module.h>  // Needed by all modules
#include <linux/kernel.h>  // Needed for KERN_INFO
#include <linux/fs.h>      // Needed by filp
#include <asm/uaccess.h>   // Needed by segment descriptors

#define SMC_ADDRESS 0xFFFFE800
#define SMC_ADDRESS_WINDOW 0xff
#define SMC_SETUP(num) (0x10 * num + 0x00)
#define SMC_SETUP_DATA (0x01010101)
#define SMC_PULSE(num) (0x10 * num + 0x04)
#define SMC_PULSE_DATA (0x0a0a0a0a)
#define SMC_CYCLE(num) (0x10 * num + 0x08)
#define SMC_CYCLE_DATA (0x000e000e)
#define SMC_MODE(num)  (0x10 * num + 0x0C)
#define SMC_MODE_DATA  (0x3 | 1 << 12)
#define SMC_DELAY1 0xC0
#define SMC_DELAY2 0xC4
#define SMC_DELAY3 0xC8
#define SMC_DELAY4 0xCC
#define SMC_DELAY5 0xD0
#define SMC_DELAY6 0xD4
#define SMC_DELAY7 0xD8
#define SMC_DELAY8 0xDC

#ifdef DEBUG
# define _DBG(fmt, args...) printk(KERN_ALERT "%s: " fmt "\n", __FUNCTION__, ##args)
#else
# define _DBG(fmt, args...) do { } while(0);
#endif

#define DEBUG

#define TMP_BUF_SIZE 4096
#define PROG_FILE_NAME_LEN 256
#define MAX_WAIT_COUNTER 8*2048

struct sk_fpga_smc_timings
{
    uint32_t setup; // setup ebi timings
    uint32_t pulse; // pulse ebi timings
    uint32_t cycle; // cycle ebi timings
    uint32_t mode;  // ebi mode
};

struct sk_fpga_data
{
    uint32_t address;
    uint16_t data;
};

struct sk_fpga_pins
{
    uint8_t fpga_cclk;                // pin to run cclk on fpga
    uint8_t fpga_din;                 // pin to set data to fpga
    uint8_t fpga_done;                // pin to read status done from fpga
    uint8_t fpga_prog;                // pin to set mode to prog on fpga
    uint8_t fpga_reset;               // pin to reset fpga internal state
    uint8_t fpga_irq;                 // pin to trigger irq on arm side
    uint8_t host_irq;                 // pin to trigger irq on fpga side
};

struct sk_fpga
{
    struct platform_device *pdev;
    // be aware that real window size is limited by 25 address lines
    uint32_t fpga_mem_window_size;    // phys mem size on any cs pin
    uint32_t fpga_mem_phys_start_cs0; // phys mapped addr of fpga mem on cs0
    uint32_t fpga_mem_phys_start_cs1; // phys mapped addr of fpga mem on cs1
    uint16_t __iomem* fpga_mem_virt_start_cs0;// virt mapped addr of fpga mem on cs0
    uint16_t __iomem* fpga_mem_virt_start_cs1;// virt mapped addr of fpga mem on cs1
    uint8_t opened;                   // fpga opened times
    struct sk_fpga_smc_timings smc_timings; // holds timings for ebi
    struct sk_fpga_pins        fpga_pins; // pins to be used to programm fpga or interact with it
    uint8_t* fpga_prog_buffer; // tmp buffer to hold fpga firmware
    uint32_t address;
    uint16_t transactionSize;
    struct clk* fpga_clk;
    uint32_t    fpga_freq;
};

// Maybe we want to hide some of these functions
static int     sk_fpga_remove (struct platform_device *pdev);
static int     sk_fpga_probe  (struct platform_device *pdev);
static int     sk_fpga_close  (struct inode *inodep, struct file *filp);
static int     sk_fpga_open   (struct inode *inode, struct file *file);
static ssize_t sk_fpga_write  (struct file *file, const char __user *buf,
                               size_t len, loff_t *ppos);
static ssize_t sk_fpga_read   (struct file *file, char __user *buf,
                               size_t len, loff_t *ppos);
static long    sk_fpga_ioctl  (struct file *f, unsigned int cmd, unsigned long arg);
int            sk_fpga_setup_smc (void);
int            sk_fpga_read_smc (void);
// TODO: add description
int sk_fpga_prepare_to_program (void);
int sk_fpga_programming_done   (void);
void sk_fpga_program (const uint8_t* buff, uint32_t bufLen);
int sk_fpga_prog(char* fName);


enum prog_state
{
    FPGA_PROG_PREPARE = 0,
    FPGA_PROG_FLUSH_BUF,
    FPGA_PROG_FINISH,
    FPGA_PROG_LAST,
};

#define SKFP_IOC_MAGIC 0x81
// ioctl to write data to FPGA
#define SKFPGA_IOSDATA _IOW(SKFP_IOC_MAGIC, 1, struct sk_fpga_data)
// ioctl to read data from FPGA
#define SKFPGA_IOGDATA _IOW(SKFP_IOC_MAGIC, 2, struct sk_fpga_data)
// ioctl to set SMC timings
#define SKFPGA_IOSSMCTIMINGS _IOW(SKFP_IOC_MAGIC, 3, struct sk_fpga_smc_timings)
// ioctl to request SMC timings
#define SKFPGA_IOGSMCTIMINGS _IOR(SKFP_IOC_MAGIC, 4, struct sk_fpga_smc_timings)
// ioctl to programm FPGA
#define SKFPGA_IOSPROG _IOR(SKFP_IOC_MAGIC, 5, char[PROG_FILE_NAME_LEN])
// ioctl to set reset pin level
#define SKFPGA_IOSRESET _IOR(SKFP_IOC_MAGIC, 6, uint8_t)
// ioctl to get reset pin level
#define SKFPGA_IOGRESET _IOR(SKFP_IOC_MAGIC, 7, uint8_t)
// ioctl to set arm-to-fpga pin level
#define SKFPGA_IOSHOSTIRQ _IOR(SKFP_IOC_MAGIC, 8, uint8_t)
// ioctl to get arm-to-fpga pin level
#define SKFPGA_IOGHOSTIRQ _IOR(SKFP_IOC_MAGIC, 9, uint8_t)
// TODO: implement later
// ioctl to set fpga-to-arm as irq
#define SKFPGA_IOSFPGAIRQ _IOR(SKFP_IOC_MAGIC, 10, uint8_t)
// ioctl to get fpga-to-arm pin level
#define SKFPGA_IOGFPGAIRQ _IOR(SKFP_IOC_MAGIC, 11, uint8_t)

// ioctl to set the current mode for the FPGA
//#define SKFPGA_IOSMODE _IOR(SKFP_IOC_MAGIC, 3, int)
// ioctl to get the current mode for the FPGA
//#define SKFPGA_IOQMODE _IOW(SKFP_IOC_MAGIC, 4, int)
// ioctl to set the current mode for the FPGA
//#define SKFPGA_IOSPROG_DONE _IOR(SKFP_IOC_MAGIC, 5, int)
// ioctl to get the current mode for the FPGA
//#define SKFPGA_IOQPROG_DONE _IOW(SKFP_IOC_MAGIC, 6, int)

#endif
