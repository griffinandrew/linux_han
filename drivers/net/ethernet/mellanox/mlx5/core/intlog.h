//attempting to create a header file for use in mlx5 and eventually generalized 
//most of these definitions are courtesy of Han Dong his work for ixgbe https://github.com/handong32/linux/tree/6d082d375b40e34f6b1f620101fd99e6c55089a4/drivers/net/ethernet/intel/ixgbe

//unsure of exactly which libraries will be needed

#include <linux/cpuidle.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/proc_fs.h>
#include "en.h"
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/version.h>


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
#define HAVE_PROC_OPS
#endif

// a single LogEntry is a single row of data in the entire log
union LogEntry { 
  long long data[20]; //there are 21 elements
  struct {
    long long tsc;             // rdtsc timestamp of when log entry was collected
    long long ninstructions;   // number of instructions
    long long ncycles;         // number of CPU cycles (will be impacted by CPU frequency changes, generally have it as a sanity check)
    long long nref_cycles;     // number of CPU cycles (counts at fixed rate, not impacted by CPU frequency changes)
    long long nllc_miss;       // number of last-level cache misses
    long long pwr;          // current energy reading (Joules) from RAPL MSR register
    long long curr;

    //sleep states will be different across different processors
    long long c0;              // C0 sleep state
    long long c1;              // C1 sleep state
    long long c1e;             // C1E sleep state
    long long c3;              // C3 sleep state
    long long c6;              // C6 sleep state
    long long c7;              // C7 sleep state
    
    unsigned int rx_desc;      // number of receive descriptors
    unsigned int rx_bytes;     // number of receive bytes
    unsigned int tx_desc;      // number of transmit descriptors
    unsigned int tx_bytes;     // number of transmit bytes

    //the idea is to use the the manual counter here to compare the discrepancy btw the 2
    unsigned int rx_desc_stats;      // number of receive descriptors
    unsigned int rx_bytes_stats;     // number of receive bytes
    unsigned int tx_desc_stats;      // number of transmit descriptors
    unsigned int tx_bytes_stats;     // number of transmit bytes

  } __attribute((packed)) Fields;
} __attribute((packed));


#define CACHE_LINE_SIZE 64
// pre-allocate size for number of LogEntry struct
// Note: change this depending on your estimated log size entries, there are kernel limits for this too
#define LOG_SIZE 1000000  

#define NUM_CORES 80

// a global data structure for each core
struct Log {
  union LogEntry *log;  
  u64 itr_joules_last_tsc;    // stores the last RDTSC timestamp to check of 1 millisecond has passed
  u32 msix_other_cnt;         
  u32 itr_cookie;
  u32 non_itr_cnt;   
  u32 itr_cnt;                // this keeps track of number of LogEntry in *log
  u32 perf_started;
} __attribute__((packed, aligned(CACHE_LINE_SIZE)));

//this is my own counting handy work, could be garbage tho, relies on  rq stats for rx bytes / packs
struct txrx_stats {
  unsigned int tx_nbytes;
  unsigned int tx_npkts;
  unsigned int rx_nbytes;
  unsigned int rx_npkts;
} __attribute((packed)); //stay close to other structs for used for this purpose


struct sys_txrx_stats {
  unsigned int last_tx_nbytes;
  unsigned int last_tx_npkts;
  unsigned int last_rx_nbytes;
  unsigned int last_rx_npkts;
  unsigned int curr_tx_nbytes;
  unsigned int curr_tx_npkts;
  unsigned int curr_rx_nbytes;
  unsigned int curr_rx_npkts;

  unsigned int diff_tx_nbytes;
  unsigned int diff_tx_npkts;
  unsigned int diff_rx_nbytes;
  unsigned int diff_rx_npkts;
} __attribute((packed)); //stay close to other structs for used for this purpose


struct smpro_pwr {
  int smpro_power; //declare var to hold power level
  int smpro_curr; //declare var to hold curr level
} __attribute((packed)); //stay close to other structs for used for this purpose

extern struct smpro_pwr pwr;
extern struct Log logs[NUM_CORES]; //for the 80 cores
//extern unsigned int tsc_per_milli;

extern struct txrx_stats per_irq_stats;
extern struct sys_txrx_stats sys_per_irq_stats;


//extern const struct file_operations ct_file_ops_intlog;
//extern struct seq_operations my_seq_ops_intlog; //both declared in c file

//function declarations

// ************************ SEQ FILE OPS *********************************
/*
void *ct_start(struct seq_file *s, loff_t *pos);

void *ct_next(struct seq_file *s, void *v, loff_t *pos);

int ct_show(struct seq_file *s, void *v);

void ct_stop(struct seq_file *s, void *v);

int ct_open(struct inode *inode, struct file *file);
*/ //now all static
// ************************ ALLOC FUNCS *******************************
int alloc_log_space(void);

void dealloc_log_space(void);

// ************************** RECORD LOG ********************************
void record_log(void);

// *********************** CREATE / REMOVE PROCS_DIR ***********************
void remove_dir(void);

int create_dir(void);

// *********************** IDLE STATES ***********************
void cpu_idle_states(void);

// *********************** PMU things ***********************
void read_counters(uint64_t* values);

void configure_pmu(void);

void set_ndev_and_epriv(void); //perhaps could be static?

//*********************** tracking of stats per irq ********************
void diff_sys_stats(void);

void record_curr_sys_irq_stats(void);

void init_sys_irq_stats(void);

void update_sys_stats(void);

void record_tx_poll_info(u16 npkts, u32 nbytes);

void record_rx_poll_info(uint64_t npkts, uint64_t nbytes);

void reset_per_irq_stats(void);
