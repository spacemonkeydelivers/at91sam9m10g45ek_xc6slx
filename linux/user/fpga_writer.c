//#include <linux-4.15/drivers/misc/fpga-sk-at91sam9m10g45-xc6slx.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <cassert>
#include <cerrno>

// TODO: merge ioctl defines with ones in kernel
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
#define SKFPGA_IOSPROG _IOR(SKFP_IOC_MAGIC, 5, uint8_t)
// ioctl to use reset
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


// TODO: merge data structures with ones in kernel
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

class Fpga
{
public:

    // TODO: merge state enum with one in kernel
    enum class FpgaState
    {
        FPGA_UNDEFINED = 0,    // undefined FPGA state when nothing yet happened
        FPGA_READY_TO_PROGRAM, // set FPGA to be ready to be programmed
        FPGA_PROGRAMMED,       // FPGA is programmed and ready to work
        FPGA_LAST,
    };

    // TODO: get that data from kernel
    // 25 address bits + 1 chip select equals to 64 megabytes addressable
    static constexpr uint8_t FPGA_ADDR_BITS = 26;
    static constexpr uint32_t MAX_FPGA_ADDR = (1 << FPGA_ADDR_BITS);
    Fpga() = delete;
    
    Fpga(const char* dev)
    {
        m_fd = open(dev, O_RDWR);
        fprintf(stderr, "Open %d\n", m_fd);
        assert(IsOpened());
    }
    
    ~Fpga()
    {
        assert(IsOpened());
        close(m_fd);
    }

    bool IsOpened() const
    {
        return m_fd > 0;
    }

    bool GetTimings(sk_fpga_smc_timings* t)
    {
        return(ioctl(m_fd, SKFPGA_IOGSMCTIMINGS, t) == -1);
    }

    bool SetTimings(sk_fpga_smc_timings* t)
    {
        return(ioctl(m_fd, SKFPGA_IOSSMCTIMINGS, t) == -1);
    }

    bool ReadShort(sk_fpga_data* d) const
    {
        assert(IsOpened());
        assert(d->address < MAX_FPGA_ADDR);
        return(ioctl(m_fd, SKFPGA_IOGDATA, d) == -1);
    }

    bool WriteShort(sk_fpga_data* d)
    {
        assert(IsOpened());
        assert(d->address < MAX_FPGA_ADDR);
        return(ioctl(m_fd, SKFPGA_IOSDATA, d) == -1);
    }

    void Write(const uint8_t* buf, uint32_t num)
    {
        if (!num)
        {
            return;
        }
        uint32_t bytesLeft = 0;
        do
        {
            ssize_t res = write(m_fd, (buf + bytesLeft), (num - bytesLeft));
            // fail occured
            if (res == -1)
            {
                return;
            }
            else
            {
                bytesLeft += res;
            }
        }
        while(bytesLeft != num);
    }

    void Read()
    {
        ;
    }

    void WriteMmap()
    {
        ;
    }

    void ReadMmap()
    {
        ;
    }

    void WriteDma()
    {
        ;
    }

    void ReadDma()
    {
        ;
    }

    void GetTimings()
    {
        ;
    }

    void SetTimings()
    {
        ;
    }

    void RegisterCallbackOnInterrupt()
    {
        // register signal SIGIO
        ;
    }

    bool SetReset(bool reset)
    {
        uint8_t res = reset ? 1 : 0;
        return(ioctl(m_fd, SKFPGA_IOSRESET, &res) == -1);
    }

    uint8_t GetReset()
    {
        uint8_t reset = -1;
        if (!(ioctl(m_fd, SKFPGA_IOGRESET, &reset) == -1))
        {
            return -1;
        }
        else
        {
            return reset;
        }
    }

    bool SetHostToFpgaIrq(bool val)
    {
        uint8_t res = val ? 1 : 0;
        return(ioctl(m_fd, SKFPGA_IOSHOSTIRQ, &res) == -1);
    }

    bool GetHostToFpgaIrq()
    {
        uint8_t val = -1;
        if (!(ioctl(m_fd, SKFPGA_IOGHOSTIRQ, &val) == -1))
        {
            return -1;
        }
        else
        {
            return val;
        }
    }

    bool GetFpgaToHostIrq()
    {
        uint8_t val = -1;
        if (!(ioctl(m_fd, SKFPGA_IOGFPGAIRQ, &val) == -1))
        {
            return -1;
        }
        else
        {
            return val;
        }
    }

private:
    int m_fd = -EFAULT;
};

int main (int argc, char* argv[])
{
    Fpga f("/dev/fpga");
    sk_fpga_data data = {0x0301, 0x1};
    sk_fpga_smc_timings timings = {0x01010101,0x0a0a0a0a, 0x000e000e, (0x3 | 1<<12)};
    sk_fpga_smc_timings rTimings;
    // set smc timings
    f.SetTimings(&timings);
    // read smc timings
    f.GetTimings(&rTimings);
    // compare smc timings
    if (memcmp(&timings, &rTimings, sizeof(timings)))
    {
        fprintf(stderr, "Timings don't match\n");
        assert(0);
    }
    // release reset
    f.SetReset(true);
    for (uint16_t i = 0; i < (1 << 23); i += 2)
    {
        data = {static_cast<uint32_t>(i << 1), 0x1};
        fprintf(stderr, "Reading data by %x address\n", (i << 1));
        f.ReadShort(&data);
        if (static_cast<uint16_t>(i << 1) == data.data)
        {
            fprintf(stderr, "Data read as expected\n");
        }
        else
        {
            fprintf(stderr, "Unexpected data read: address 0x%x data 0x%x\n", data.address, data.data);
            assert(0);
        }
    }
}
