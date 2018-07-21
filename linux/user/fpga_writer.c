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
#include <ctime>
#include <signal.h>

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
#define SKFPGA_IOSPROG _IOR(SKFP_IOC_MAGIC, 5, char[256])
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
// ioctl to set address space selector
#define SKFPGA_IOSADDRSEL _IOR(SKFP_IOC_MAGIC, 12, uint8_t)
// ioctl to get address space selector
#define SKFPGA_IOGADDRSEL _IOR(SKFP_IOC_MAGIC, 13, uint8_t)
// ioctl to initiate DMA transfer
#define SKFPGA_IOSDMA _IOR(SKFP_IOC_MAGIC, 14, struct sk_fpga_dma_transaction)
// ioctl to set pid
#define SKFPGA_IOSPID _IOR(SKFP_IOC_MAGIC, 15, int)

enum class addr_selector
{
    FPGA_ADDR_UNDEFINED = 0,
    FPGA_ADDR_CS0,
    FPGA_ADDR_CS1,
    FPGA_ADDR_DMA,
    FPGA_ADDR_LAST,
};

enum class dma_dir
{
    DMA_ARM_TO_FPGA,
    DMA_FPGA_TO_ARM,
    DMA_LAST,
};

struct sk_fpga_dma_transaction
{
    uint32_t addr;
    uint32_t len;
    uint8_t  dir;
    uint8_t  sync;
};

// TODO: merge data structures with ones in kernel
struct sk_fpga_smc_timings
{
    uint32_t setup; // setup ebi timings
    uint32_t pulse; // pulse ebi timings
    uint32_t cycle; // cycle ebi timings
    uint32_t mode;  // ebi mode
    uint8_t  num;
};

struct sk_fpga_data
{
    uint32_t address;
    uint16_t data;
};

class Fpga
{
public:

    enum class ProgState
    {
        FPGA_PROG_PREPARE = 0,
        FPGA_PROG_FLUSH_BUF,
        FPGA_PROG_FINISH,
        FPGA_PROG_LAST,
    };

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
    static constexpr uint8_t FPGA_ADDR_BITS = 25;
    static constexpr uint8_t FPGA_WINDOW_NUM = 2;
    static constexpr uint32_t FPGA_WINDOW_MAX_ADDR = (1 << FPGA_ADDR_BITS);
    static constexpr uint32_t FPGA_MAX_ADDR = FPGA_WINDOW_MAX_ADDR * FPGA_WINDOW_NUM;
    static constexpr uint32_t DMA_BUF_SIZE  = 65536;
    Fpga() = delete;
    
    Fpga(const char* dev)
    {
        m_fd = open(dev, O_RDWR);
        assert(IsOpened());
        int pid = getpid();
        fcntl(m_fd, F_SETOWN, getpid());
        int oflags = fcntl(m_fd, F_GETFL);
        fcntl(m_fd, F_SETFL, oflags | FASYNC);
        ioctl(m_fd, SKFPGA_IOSPID, &pid);
    }

    // return true in case of error, wtf?!
    bool ProgramFpga(const char* fw)
    {
        char tmp[256];
        strcpy(tmp, fw);
        return (ioctl(m_fd, SKFPGA_IOSPROG, &tmp) == -1);
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
        assert(d->address < FPGA_MAX_ADDR);
        return(ioctl(m_fd, SKFPGA_IOGDATA, d) == -1);
    }

    bool WriteShort(sk_fpga_data* d)
    {
        assert(IsOpened());
        assert(d->address < FPGA_MAX_ADDR);
        return(ioctl(m_fd, SKFPGA_IOSDATA, d) == -1);
    }

    bool TestDMA(uint32_t addr, uint32_t len, enum dma_dir d, bool sync)
    {
        assert(addr < FPGA_MAX_ADDR);
        assert(len <= DMA_BUF_SIZE);
        assert(d == dma_dir::DMA_ARM_TO_FPGA || d == dma_dir::DMA_FPGA_TO_ARM);
        sk_fpga_dma_transaction tran = {0x10000000 + addr, len, static_cast<uint8_t>(d), (sync) ? 1u : 0u};
        return(ioctl(m_fd, SKFPGA_IOSDMA, &tran) == -1);
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

    bool SetAddrSpace(addr_selector sel)
    {
        assert((sel == addr_selector::FPGA_ADDR_CS0) || (sel == addr_selector::FPGA_ADDR_CS1) || (sel == addr_selector::FPGA_ADDR_DMA));
        uint8_t res = static_cast<uint8_t>(sel);
        return(ioctl(m_fd, SKFPGA_IOSADDRSEL, &res) == -1);
    }

    addr_selector GetAddrSpace()
    {
        uint8_t sel = 0;
        if (ioctl(m_fd, SKFPGA_IOGADDRSEL, &sel) == -1)
        {
            return addr_selector::FPGA_ADDR_UNDEFINED;
        }
        else
        {
            addr_selector selRes = static_cast<addr_selector>(sel);
            assert((selRes == addr_selector::FPGA_ADDR_CS0) || (selRes == addr_selector::FPGA_ADDR_CS1));
            return selRes;
        }
    }

    bool SetReset(bool reset)
    {
        uint8_t res = reset ? 1 : 0;
        return(ioctl(m_fd, SKFPGA_IOSRESET, &res) == -1);
    }

    uint8_t GetReset()
    {
        uint8_t reset = -1;
        if (ioctl(m_fd, SKFPGA_IOGRESET, &reset) == -1)
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

    uint8_t GetHostToFpgaIrq()
    {
        uint8_t val = -1;
        if (ioctl(m_fd, SKFPGA_IOGHOSTIRQ, &val) == -1)
        {
            return -1;
        }
        else
        {
            return val;
        }
    }

    uint8_t SetFpgaToHostIrq(bool set)
    {
        uint8_t val = set ? 1 : 0;
        return (ioctl(m_fd, SKFPGA_IOSFPGAIRQ, &val) == -1);
    }

    bool Mmap()
    {
        addr_selector curSel = GetAddrSpace();
        SetAddrSpace(addr_selector::FPGA_ADDR_CS0);
        // use fpga mem window size
        m_mmapCs0 = static_cast<uint16_t*>(mmap(nullptr, FPGA_WINDOW_MAX_ADDR, PROT_WRITE|PROT_READ, MAP_SHARED, m_fd, 0));
        SetAddrSpace(addr_selector::FPGA_ADDR_CS1);
        // use fpga mem window size
        m_mmapCs1 = static_cast<uint16_t*>(mmap(nullptr, FPGA_WINDOW_MAX_ADDR, PROT_WRITE|PROT_READ, MAP_SHARED, m_fd, 0));
        SetAddrSpace(addr_selector::FPGA_ADDR_DMA);
        m_dma = static_cast<uint16_t*>(mmap(nullptr, DMA_BUF_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, m_fd, 0));
        SetAddrSpace(curSel);
        if ((m_mmapCs0 == MAP_FAILED) || (m_mmapCs1 == MAP_FAILED)) 
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    uint16_t* GetFpgaMemCs0()
    {
        return m_mmapCs0;
    }

    uint16_t* GetFpgaMemCs1()
    {
        return m_mmapCs1;
    }

    void* GetFpgaDmaBuf()
    {
        return m_dma;
    }

    void DmaHandler()
    {
        uint16_t* dmaPtr = static_cast<uint16_t*>(GetFpgaDmaBuf());
        for (int i = 0; i < 10; i++)
        {
            fprintf(stderr, "Read dma buf 0x%x : 0x%x\n", (i << 1), *dmaPtr);
            dmaPtr++;
        }
    }

    void IrqHandler()
    {
        ;
    }

private:
    int m_fd = -EFAULT;
    uint16_t* m_mmapCs0 = nullptr;
    uint16_t* m_mmapCs1 = nullptr;
    void*     m_dma     = nullptr;
};

volatile bool stop = false;

void UsrSig1Handler(int data)
{
    fprintf(stderr, "DMA SIGNAL ARRIVED %x\n", data);
    stop = true;
}

void UsrSig2Handler(int data)
{
    fprintf(stderr, "TIMER SIGNAL ARRIVED %x\n", data);
    stop = true;
}

int main (int argc, char* argv[])
{
    struct sigaction usr_action;
    sigset_t block_mask;
    sigfillset (&block_mask);
    usr_action.sa_handler = UsrSig1Handler;
    usr_action.sa_mask = block_mask;
    usr_action.sa_flags = SA_NODEFER;
    sigaction (SIGUSR1, &usr_action, NULL);
    usr_action.sa_handler = UsrSig2Handler;
    sigaction (SIGUSR2, &usr_action, NULL);

    Fpga f("/dev/fpga");

    f.ProgramFpga("./simple_debug.bit");

    // register irq handler
    if (f.SetFpgaToHostIrq(true))
    {
        assert(0);
    }

    sk_fpga_smc_timings timings = {0x00000000, 0x04040404, 0x00050005, (0x3 | 1<<12), 0};
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
    timings.num = 1;
    f.SetTimings(&timings);
    // read smc timings
    f.GetTimings(&rTimings);
    // compare smc timings
    if (memcmp(&timings, &rTimings, sizeof(timings)))
    {
        fprintf(stderr, "Timings don't match\n");
        assert(0);
    }

    uint32_t sAddr = 0x2000;
    uint16_t sData = 0x1055;
    // release reset
    f.SetReset(true);
    f.SetAddrSpace(addr_selector::FPGA_ADDR_CS1);
    // Check non-RAM address, should return 16 bits of address requested
    sk_fpga_data d  = {(sAddr + 64u), static_cast<uint16_t>(sData + 32u)};
    f.ReadShort(&d);
    fprintf(stderr, "Valid check: %x : %x\n", d.address, d.data);
    assert(d.data == (d.address | 1));

    // RAM is mapped to 0x2000 - 0x2040 addresses
    // Write 32 cells 16 bits
    f.SetAddrSpace(addr_selector::FPGA_ADDR_CS0);
    for (uint16_t i = 0; i < 32*2; i+=2)
    {
        sk_fpga_data d  = {sAddr + i, static_cast<uint16_t>(sData + i)};
        f.WriteShort(&d);
        fprintf(stderr, "Writing %x : %x\n", sAddr + i, sData + i);
    }

    // Verify written values
    for (uint16_t i = 0; i < 32 * 2; i+=2)
    {
        sk_fpga_data d  = {sAddr + i, static_cast<uint16_t>(sData + i)};
        f.ReadShort(&d);
        fprintf(stderr, "Reading %x : %x\n", d.address, d.data);
        assert(d.data == (sData + i));
    }

    if (f.Mmap())
    {
        assert(0);
    }

    clock_t begin = clock();
    uint16_t* cs1 = f.GetFpgaMemCs1();
    volatile uint16_t readVal = 0;;
    for (int i = 0; i < Fpga::FPGA_WINDOW_MAX_ADDR / sizeof(uint16_t); i++)
    {
        readVal = *cs1;
        uint16_t expectedVal = static_cast<uint16_t>(i << 1) | 1;
        assert(readVal == expectedVal);
        cs1++;
    }
    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    fprintf(stderr, "Reading %x bytes, %d transactions in %f seconds at %lx clocks_per_sec: %f mb/s\n", (1 << 25), (1 << 25) / sizeof(uint16_t), elapsed_secs, CLOCKS_PER_SEC, ((1 << 25) / 1024 / 1024 / elapsed_secs));

    f.SetAddrSpace(addr_selector::FPGA_ADDR_CS0);
    d  = {(sAddr + 64u), static_cast<uint16_t>(sData + 32u)};
    f.ReadShort(&d);
    fprintf(stderr, "Valid check: %x : %x\n", d.address, d.data);
    assert(d.data == d.address);

    memset(f.GetFpgaDmaBuf(), 0, Fpga::DMA_BUF_SIZE);
    f.TestDMA(0, Fpga::DMA_BUF_SIZE, dma_dir::DMA_FPGA_TO_ARM, false);
    // waiting for dma irq
    while(!stop)
    {
        ;
    }
    f.DmaHandler();
    // waiting for timer irq
    stop = false;
    while(!stop)
    {
        ;
    }
}
