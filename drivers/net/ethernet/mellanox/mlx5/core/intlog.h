//attempting to create a header file for use in mlx5 and eventually generalized 
//most of these definitions are courtesy of Han Dong his work for ixgbe https://github.com/handong32/linux/tree/6d082d375b40e34f6b1f620101fd99e6c55089a4/drivers/net/ethernet/intel/ixgbe

//unsure of exactly which libraries will be needed

#include <linux/cpuidle.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include "en.h"


// a single LogEntry is a single row of data in the entire log
union LogEntry { 
  long long data[14];
  struct {
    long long tsc;             // rdtsc timestamp of when log entry was collected
    long long ninstructions;   // number of instructions
    long long ncycles;         // number of CPU cycles (will be impacted by CPU frequency changes, generally have it as a sanity check)
    long long nref_cycles;     // number of CPU cycles (counts at fixed rate, not impacted by CPU frequency changes)
    long long nllc_miss;       // number of last-level cache misses
    long long joules;          // current energy reading (Joules) from RAPL MSR register

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

extern struct Log logs[NUM_CORES]; //for the 80 cores
extern unsigned int tsc_per_milli;

extern const struct file_operations ct_file_ops_intlog;
extern struct seq_operations my_seq_ops_intlog; //both declared in c file

//function declarations

// ************************ SEQ FILE OPS *********************************
void *ct_start(struct seq_file *s, loff_t *pos);

void *ct_next(struct seq_file *s, void *v, loff_t *pos);

int ct_show(struct seq_file *s, void *v);

void ct_stop(struct seq_file *s, void *v);

int ct_open(struct inode *inode, struct file *file);

// ************************ ALLOC FUNCS *******************************
int alloc_log_space(struct mlx5e_priv *priv);

void dealloc_log_space(void);

// ************************** RECORD LOG ********************************
void record_log(struct mlx5e_priv *priv);


// *********************** CREATE / REMOVE PROCS_DIR ***********************
void remove_dir(void);

void create_dir(void);
