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
#include <asm/smp.h>


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
#define HAVE_PROC_OPS
#endif

// a single LogEntry is a single row of data in the entire log
union LogEntry { 
  uint64_t data[20]; //there are 21 elements
  struct {
    uint64_t tsc;             // rdtsc timestamp of when log entry was collected
    uint64_t ninstructions;   // number of instructions
    uint64_t ncycles;         // number of CPU cycles (will be impacted by CPU frequency changes, generally have it as a sanity check)
    uint64_t nref_cycles;     // number of CPU cycles (counts at fixed rate, not impacted by CPU frequency changes)
    uint64_t nllc_miss;       // number of last-level cache misses
    uint64_t pwr;          // current energy reading (Joules) from RAPL MSR register
    uint64_t curr;

    //sleep states will be different across different processors
    uint64_t c0;              // C0 sleep state
    uint64_t c1;              // C1 sleep state
    uint64_t c1e;             // C1E sleep state
    uint64_t c3;              // C3 sleep state
    uint64_t c6;              // C6 sleep state
    uint64_t c7;              // C7 sleep state
    
    uint64_t rx_desc_poll;      // number of receive descriptors
    uint64_t rx_bytes_poll;     // number of receive bytes
    uint64_t tx_desc_poll;      // number of transmit descriptors
    uint64_t tx_bytes_poll;     // number of transmit bytes

    //the idea is to use the the manual counter here to compare the discrepancy btw the 2
    uint64_t rx_desc_stats;      // number of receive descriptors
    uint64_t rx_bytes_stats;     // number of receive bytes
    uint64_t tx_desc_stats;      // number of transmit descriptors
    uint64_t tx_bytes_stats;     // number of transmit bytes

  } __attribute((packed)) Fields;
} __attribute((packed));


#define CACHE_LINE_SIZE 64
// pre-allocate size for number of LogEntry struct
// Note: change this depending on your estimated log size entries, there are kernel limits for this too
#define LOG_SIZE 10000

#define NUM_CORES 20

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
struct poll_stats {
  uint64_t tx_nbytes;
  uint64_t tx_npkts;
  uint64_t rx_nbytes;
  uint64_t rx_npkts;
} __attribute((packed)); //stay close to other structs for used for this purpose


struct sys_swstats_stats {
  uint64_t last_tx_nbytes;
  uint64_t last_tx_npkts;
  uint64_t last_rx_nbytes;
  uint64_t last_rx_npkts;
  uint64_t curr_tx_nbytes;
  uint64_t curr_tx_npkts;
  uint64_t curr_rx_nbytes;
  uint64_t curr_rx_npkts;

  uint64_t diff_tx_nbytes;
  uint64_t diff_tx_npkts;
  uint64_t diff_rx_nbytes;
  uint64_t diff_rx_npkts;
} __attribute((packed)); //stay close to other structs for used for this purpose


struct smpro_pwr {
  uint64_t smpro_power; //declare var to hold power level
  uint64_t smpro_curr; //declare var to hold curr level
} __attribute((packed)); //stay close to other structs for used for this purpose

extern struct smpro_pwr pwr;
extern struct Log logs[NUM_CORES]; //for the 80 cores
//extern unsigned int tsc_per_milli;

extern struct poll_stats poll_irq_stats;
extern struct sys_swstats_stats sys_swstats_irq_stats;


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

void log_idle_states_usage(union LogEntry *ile);

// *********************** PMU things ***********************
void log_counters(union LogEntry *ile);

void configure_pmu(void);

void set_ndev_and_epriv(void); //perhaps could be static?

//*********************** smpro + xgene ********************

//int get_power_smpro(void);

void log_power_xgene(union LogEntry *ile);

//*********************** tracking of stats per irq ********************

void log_sys_swstats_irq_stats(union LogEntry *ile);

void cumulative_sys_swstats_irq_stats(union LogEntry *ile);

void record_tx_poll_info(u16 npkts, u32 nbytes);

void record_rx_poll_info(uint64_t npkts, uint64_t nbytes);

void reset_poll_irq_stats(void);

void log_poll_irq_stats(union LogEntry *ile);



