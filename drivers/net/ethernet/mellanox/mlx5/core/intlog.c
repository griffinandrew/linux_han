//this file is meant for the creation of a seq_file interface to create the files to hold each per core log
//this modifications are largely taken from Han Dong https://github.com/handong32/linux/blob/6d082d375b40e34f6b1f620101fd99e6c55089a4/drivers/net/ethernet/intel/ixgbe/ixgbe_main.c
//and are being reworked to form a eventually generic system for logging with different drivers, currently working on mlx5 specfic implementaion


/*************************************************************************
 * intLog: to access intel C-state information, also for arm
 *************************************************************************/
#include <linux/cpuidle.h>

/*************************************************************************
 * intLog: to create procfs: /proc/ixgbe_stats/core/N //eventually
 *************************************************************************/
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <uapi/linux/stat.h> /* S_IRUSR, S_IWUSR  */
#include <linux/proc_fs.h>
#include <linux/seq_file.h> /* seq_read, seq_lseek, single_release */
#include <linux/time.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
//#include "en_stats.h"
#include "mlx5_core.h"
#include "en.h"
#include "intlog.h"
#include <linux/timecounter.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/threads.h>

//HWMON / SMPRO INCLUDE
#include <linux/hwmon.h>

#include "../../../../../hwmon/xgene-hwmon.h"

//#include "drivers/hwmon/xgene-hwmon.h"

/*************************************************************************
 * intLog: access tsc tick rate
 *************************************************************************/
//not sure why was extern 
unsigned int tsc_khz;
//= 0; //this should not be init to 0

//not sure why this had to be included as extern

/*************************************************************************
 * intLog: see definitions in intlog.h
 *************************************************************************/
// store RDTSC hardware information
unsigned int tsc_per_milli;
// per core data structure for logging
struct Log logs[NUM_CORES];
/*************************************************************************/

struct net_device *ndev;
struct mlx5e_priv *epriv;

//intlog function call extern
//extern int smpro_read_power(struct device *dev, u32 attr, int channel, long *val_pwr);

//extern int xgene_hwmon_get_cpu_pwr(struct xgene_hwmon_dev *ctx, u32 *val);
//extern struct xgene_hwmon_dev;

///////////////////////////////////////////////////////////////
struct proc_dir_entry *stats_dir;
struct proc_dir_entry *stats_core_dir;
////////////////////////////////////////////////////////////////

struct sys_swstats_stats sys_swstats_irq_stats = {
    .last_tx_nbytes = 0,
    .last_tx_npkts = 0,
	.last_rx_npkts = 0,
	.last_rx_nbytes = 0
};

struct poll_stats poll_irq_stats = {
	.tx_nbytes = 0,
  	.tx_npkts = 0,
  	.rx_nbytes = 0,
  	.rx_npkts = 0
};

struct smpro_pwr pwr = {
        .smpro_power = 0,
        .smpro_curr = 0,
};

/*********************************************************************************
 * intLog: seq_file interfaces needed to create procfs: /proc/ixgbe_stats/core/N
 *         (https://www.kernel.org/doc/html/latest/filesystems/seq\_file.html)
 *********************************************************************************/

static void *ct_start(struct seq_file *s, loff_t *pos)
{
	loff_t *spos;
  	struct Log *il;
  	unsigned long id = (unsigned long)s->private;

  	// id maps to the specific core
  	il= &logs[id];  

  	// clear out the entries in all the logs
  	spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
  	if (!spos || (unsigned int)(*pos) >= (unsigned int)(il->itr_cnt)) {
    	memset(logs[id].log, 0, (sizeof(union LogEntry) *  LOG_SIZE));
    	logs[id].itr_joules_last_tsc = 0;
    	logs[id].msix_other_cnt = 0; //there is no msix here? 
    	logs[id].itr_cookie = 0;
    	logs[id].non_itr_cnt = 0;
    	logs[id].itr_cnt = 0;
    	logs[id].perf_started = 0;    
    	return NULL;
  	}
  	*spos = *pos;
  	return spos;
}

// this gets called automatically as part of ct_show
static void *ct_next(struct seq_file *s, void *v, loff_t *pos)
{
  	loff_t *spos;
  	unsigned long id = (unsigned long)s->private;
  	struct Log *il;
  	il= &logs[id];
  
  	spos = v;
  	// to check we aren't printing a log entry beyond itr_cnt
  	if((unsigned int)(*pos) >= (unsigned int)(il->itr_cnt))
    	return NULL;

  	// increment to next log entry
  	*pos = ++*spos;
  
  	return spos;
}

/* Return 0 means success, SEQ_SKIP ignores previous prints, negative for error. */
static int ct_show(struct seq_file *s, void *v)
{
  	loff_t *spos;
  	unsigned long id = (unsigned long)s->private; // get the core id
  	struct Log *il;
  	union LogEntry *ile;
  
  	spos = v;  
  	il= &logs[id]; // for the core
  	ile = &il->log[(int)*spos]; // for the specific log entry in each core

  	// write to entry in procfs
  	if(ile->Fields.tsc != 0) {
    	//seq_printf(s, "%u %u %u %u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %u %u %u %u %llu\n", (unsigned int)*spos, ile->Fields.rx_desc, ile->Fields.rx_bytes, ile->Fields.tx_desc, ile->Fields.tx_bytes, ile->Fields.ninstructions, ile->Fields.ncycles, ile->Fields.nref_cycles, ile->Fields.nllc_miss, ile->Fields.c0, ile->Fields.c1, ile->Fields.c1e, ile->Fields.c3, ile->Fields.c6, ile->Fields.c7, ile->Fields.pwr, ile->Fields.curr,ile->Fields.tsc, ile->Fields.rx_bytes_stats, ile->Fields.tx_bytes_stats, ile->Fields.tx_desc_stats, ile->Fields.rx_desc_stats);
		seq_printf(s, "%u %u %u %u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %u %u %u %u\n", 
           (unsigned int)*spos, 
           ile->Fields.rx_desc_poll, 
           ile->Fields.rx_bytes_poll, 
           ile->Fields.tx_desc_poll, 
           ile->Fields.tx_bytes_poll, 
           (unsigned long long int)ile->Fields.ninstructions, 
           (unsigned long long int)ile->Fields.ncycles, 
           (unsigned long long int)ile->Fields.nref_cycles, 
           (unsigned long long int)ile->Fields.nllc_miss, 
           (unsigned long long int)ile->Fields.c0, 
           (unsigned long long int)ile->Fields.c1, 
           (unsigned long long int)ile->Fields.c1e, 
           (unsigned long long int)ile->Fields.c3, 
           (unsigned long long int)ile->Fields.c6, 
           (unsigned long long int)ile->Fields.c7, 
           (unsigned long long int)ile->Fields.pwr, 
           (unsigned long long int)ile->Fields.curr,
           (unsigned long long int)ile->Fields.tsc, 
           ile->Fields.rx_bytes_stats, 
           ile->Fields.tx_bytes_stats, 
           ile->Fields.tx_desc_stats, 
           ile->Fields.rx_desc_stats);
  	}
  	return 0;
}

static void ct_stop(struct seq_file *s, void *v)
{
  	kfree(v);  
}

/*************************************************************************************************/
/*************************** defining structs for reaping data ***********************************/
/*************************************************************************************************/

/*
const struct file_operations ct_file_ops_intlog =
{
	.owner   = THIS_MODULE,
	.open    = ct_open,
 	.read    = seq_read,
 	.llseek  = seq_lseek,
 	.release = seq_release
};
*/


static struct seq_operations my_seq_ops_intlog =
{
 	.next  = ct_next,
 	.show  = ct_show,
 	.start = ct_start,
 	.stop  = ct_stop,
};

static int ct_open(struct inode *inode, struct file *file)
{
  	int ret;
  
  	ret = seq_open(file, &my_seq_ops_intlog);
  	if(ret == 0) {
  		struct seq_file *m = file->private_data;
		//m->private = PDE_DATA(inode);
		m->private = pde_data(inode);
  	}
  
  	return ret; 
}

#ifdef HAVE_PROC_OPS
static struct proc_ops ct_file_ops_intlog = {
  .proc_open = ct_open,
  .proc_read = seq_read,
  .proc_lseek = seq_lseek,
  .proc_release = seq_release,
};
#else
static const struct file_operations ct_file_ops_intlog = {
  .owner = THIS_MODULE,
  .open = ct_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = seq_release,
};
#endif


/*************************************************************************************************/
//*********************************** ARCH SPEC NTI **********************************************
//used for non-temporal store instruction so our logging doesnt interfere with performance
/*************************************************************************************************/ 

//p is pointer to mem location to written, v value to overwrite mem location
static inline void write_nti64_intel(void *p, const uint64_t v) {
	asm volatile("movnti %0, (%1)\n\t"
		: 
		: "r"(v), "r"(p)
		: "memory");
}

static inline void store_int64_asm(void *p, const uint64_t v) {
	asm volatile("str %x[v], [%x[p]]"
        : [p] "+r" (p)
        : [v] "r" (v)
        : "memory");
}

static inline void store_int32_asm(void *p, const uint32_t v) {
    asm volatile("str %w[v], [%x[p]]"
        : [p] "+r" (p)
        : [v] "r" (v)
        : "memory");
}

static inline void write_nti32_intel(void *p, const uint32_t v) {
	asm volatile("movnti %0, (%1)\n\t"
		:
 		: "r"(v), "r"(p)
	    : "memory");
}


/*************************************************************************************************/
/********************************** arch specific asm getters ************************************/
/*************************************************************************************************/ 

/*
//function to get current RDTSC Timestamp intel
inline uint64_t get_rdtsc_intel(void){
	uint64_t tsc;
	asm volatile("rdtsc;" 
		     "shl $32,%%rdx;"
		     "or %%rdx,%%rax"
		     : "=a"(tsc)
		     : 
		     : "%rcx","rdx");
       return tsc;
}

*/


//possibly arm implementation for retreiving tsc
//this gets the physical timer val not the virtual

// CNTP_TVAL_EL0 from CNTP_TVAL
static inline uint64_t get_rdtsc_arm_phys(void){
	uint64_t tsc;
	asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r" (tsc));
	//printk(KERN_INFO "get time complete\n");
  	return tsc;
}

static inline uint64_t get_rdtsc_arm_virt(void) {
	uint64_t tsc;
	asm volatile("mrs %0, CNTV_CTL_EL0" : "=r" (tsc));
  	return tsc;
}

static inline void enable_and_reset_regs(void){
	uint32_t pmcr_val = 0;
	//pmcr_val |= (1 << 0);  // Enable all counters 
	pmcr_val |= (1 << 1);  // Reset all counters to 0 
	asm volatile("msr pmcr_el0, %0" : : "r" (pmcr_val));
	//printk(KERN_INFO "reset PMU complete\n");
}

static void reset_counters(void){
	uint32_t pmcr_val = 0;
	pmcr_val |= (1 << 1);  // Reset all counters to 0 
	asm volatile("msr pmcr_el0, %0" : : "r" (pmcr_val)); 
	printk(KERN_INFO "reset counters complete\n");
}


//newest config
void configure_pmu(void)
{
    printk(KERN_INFO "config_PMU begin\n");
    // Configure event codes for different counters
    uint32_t llc_misses_event = 0x37;
    uint32_t cycle_count_event = 0x11;
    uint32_t instruction_count_event = 0x08;
    
    // Configure counter 0
    asm volatile("msr PMSELR_EL0, %0" : : "r" (0));
    asm volatile("msr PMXEVTYPER_EL0, %0" : : "r" (llc_misses_event));
    
    // Configure counter 1
    asm volatile("msr PMSELR_EL0, %0" : : "r" (1));
    asm volatile("msr PMXEVTYPER_EL0, %0" : : "r" (cycle_count_event));
    
    // Configure counter 2
    asm volatile("msr PMSELR_EL0, %0" : : "r" (2));
    asm volatile("msr PMXEVTYPER_EL0, %0" : : "r" (instruction_count_event));
    
    // Enable all counters by setting the corresponding bits in PMCNTENSET_EL0
    uint32_t pmcntenset_val = 0x7;
    asm volatile("msr PMCNTENSET_EL0, %0" : : "r" (pmcntenset_val));

	asm volatile("isb"); // Ensure that all PMU configuration changes have completed
	printk(KERN_INFO "config_PMU end\n");
}

void log_counters(union LogEntry *ile)
{
	printk(KERN_INFO "log_counters begin\n");
    uint32_t pmxevcntr0_val, pmxevcntr1_val, pmxevcntr2_val;

    // Read from counter 0 LLC miss
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr0_val));
    
    // Read from counter 1 cycle count
    asm volatile("msr PMSELR_EL0, %0" : : "r" (1));
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr1_val));
    
    // Read from counter 2, instruction count
    asm volatile("msr PMSELR_EL0, %0" : : "r" (2));
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr2_val));

	store_int64_asm(&(ile->Fields.nllc_miss), pmxevcntr0_val);
	store_int64_asm(&(ile->Fields.ncycles), pmxevcntr1_val);
	store_int64_asm(&(ile->Fields.ninstructions), pmxevcntr2_val);
	printk(KERN_INFO "log_counters complete\n");
}


/*************************************************************************************************/
/******************************** GET / display IDLE STATES *******************************************/ 
/*************************************************************************************************/ 

void cpu_idle_states(void) {
    //struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
    //struct cpuidle_device *dev = cpuidle_get_device();
    //struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);  //not sure if this is valid way to get this
    struct cpuidle_device *dev;
    struct cpuidle_driver *drv;
    dev = this_cpu_read(cpuidle_devices);
    drv = cpuidle_get_cpu_driver(dev);
    
    //print states
    int i;
    printk(KERN_INFO "cpuidle stats state_count=%d\n", drv->state_count);
    for(i=0;i<drv->state_count;i++) {
        printk(KERN_INFO "i=%d name=%s exit_latency=%u target_residency=%u power_usage_mW=%i\n", i, drv->states[i].name, drv->states[i].exit_latency, drv->states[i].target_residency, drv->states[i].power_usage);
    }
}



void log_idle_states_usage(union LogEntry *ile) {
	struct cpuidle_device *idle_dev = __this_cpu_read(cpuidle_devices);
	//need to include all the sleep states
	struct cpuidle_state_usage usage_0 = idle_dev->states_usage[0];
	struct cpuidle_state_usage usage_1 = idle_dev->states_usage[1];
	struct cpuidle_state_usage usage_2 = idle_dev->states_usage[2];
	//log state usage
	store_int64_asm(&(ile->Fields.c0), usage_0.usage);
	store_int64_asm(&(ile->Fields.c1), usage_1.usage);
	store_int64_asm(&(ile->Fields.c1e), usage_2.usage);
	printk(KERN_INFO "log_idle_states_usage complete\n");
}

/*************************************************************************************************/
/******************************** ALLOC / DEALLOC LOGS *******************************************/ 
/*************************************************************************************************/ 
//without for bc C vers

// allocates memory for creation of log                                                                                                                                                                                            
int alloc_log_space(void) {                                                                                                                                                                                                        
	int i = 0;
    uint64_t now;
	int flag = 0;

    //cpu_idle_states();    
    printk(KERN_INFO "****************** intLog init *******************");                                                                                                                                                    
    while (i < NUM_CORES)   
    {                
		logs[i].log = (union LogEntry *)vmalloc(sizeof(union LogEntry) * LOG_SIZE);               
		printk(KERN_INFO "%d vmalloc size=%lu addr=%p\n", i, (sizeof(union LogEntry) * LOG_SIZE), (void*)(logs[i].log));  
		memset(logs[i].log, 0, (sizeof(union LogEntry) * LOG_SIZE));                                  
		if(!(logs[i].log))                 
		{                     
			printk(KERN_INFO "Fail to vmalloc logs[%d]->log\n", i); 
			flag = 1;                                                                                                                                  
		}
		logs[i].itr_joules_last_tsc = 0;
		logs[i].msix_other_cnt = 0;
		logs[i].itr_cookie = 0;
		logs[i].non_itr_cnt = 0;
		logs[i].itr_cnt = 0;
		logs[i].perf_started = 0;
		i++;
    }
	printk(KERN_INFO "Allocation complete\n");   
	tsc_per_milli = tsc_khz;       
	now = get_rdtsc_arm_phys();//possible func to get rdtsc            
	store_int64_asm(&(logs[0].log[0].Fields.tsc), now);   
	printk(KERN_INFO "tsc_khz = %u now = %llu tsc = %llu \n", tsc_khz, now, logs[0].log[0].Fields.tsc);
	//use cpu idle fun c to dipsplay idle states and stats
	//cpu_idle_states();
	
	//call func to get ndev and epriv globally?
	set_ndev_and_epriv();
	return flag;
}                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                                                                                                                                                                               
//deallocate memory for logs                                                                                                                                                                                                       
void dealloc_log_space(void){                                                                                                                                                                                                             
	int i = 0;
    while(i < NUM_CORES)
    {                                                                                                                                                                                        
	    if(logs[i].log){    
			vfree(logs[i].log); 
        }                                                                                                                                                                                                                                 
	    i++;
    }       
	printk(KERN_INFO "memory freed\n");                                                                                                                                                                                                                  
}    


/*************************************************************************************************/
/********************************* RECORD per irq info *******************************************/
/*************************************************************************************************/


void record_tx_poll_info(u16 npkts, u32 nbytes) { //gonna have to cast types
	poll_irq_stats.tx_nbytes += (unsigned int)nbytes;
	poll_irq_stats.tx_npkts += (unsigned int)npkts;
	printk(KERN_INFO "record_tx_poll_info info updated\n");
}

void record_rx_poll_info(uint64_t npkts, uint64_t nbytes) {
	poll_irq_stats.rx_nbytes += (unsigned int)nbytes;
	poll_irq_stats.rx_npkts += (unsigned int)npkts;
	printk(KERN_INFO "record_rx_poll_info info updated\n");
}

void reset_poll_irq_stats(void) {
	poll_irq_stats.tx_nbytes = 0;
	poll_irq_stats.tx_npkts = 0;
	poll_irq_stats.rx_nbytes = 0;
	poll_irq_stats.rx_npkts = 0;
	printk(KERN_INFO "reset_poll_irq_stats complete\n");
}


//not sure if this should be a piinter or not, know that I want to pass address to
void log_poll_irq_stats(union LogEntry *ile) {
	store_int64_asm(&(ile->Fields.tx_bytes_poll), poll_irq_stats.tx_nbytes);
	store_int64_asm(&(ile->Fields.rx_bytes_poll), poll_irq_stats.rx_nbytes);
	store_int64_asm(&(ile->Fields.tx_desc_poll), poll_irq_stats.tx_npkts);
	store_int64_asm(&(ile->Fields.rx_desc_poll), poll_irq_stats.rx_npkts);
	printk(KERN_INFO "log_poll_irq_stats complete\n");
}


//in case can't use sw_stats

/*************************************************************************************************/
/******************************** create dir /proc/arm_stats/core/N ******************************/
/*************************************************************************************************/

int create_dir(void) {
	stats_dir = proc_mkdir("arm_stats", NULL);
	unsigned long int i = 0;
	int ret = 0;
	if(!stats_dir) {
		printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/\n");
		return -ENOMEM;
	}
	stats_core_dir = proc_mkdir("core", stats_dir);
	if(!stats_core_dir) {
		printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/core/\n");
		return -ENOMEM; //memory error , not enough size
	}
	while(i<NUM_CORES) {
		char name [4]; 
       		sprintf(name, "%ld", i);
 		if(!proc_create_data(name, 0444, stats_core_dir, &ct_file_ops_intlog, (void *)i)) {
			printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/core/%ld\n", i);
			ret = 1;
		}
		i++;
	}
	printk(KERN_INFO "Successfully loaded /proc/arm_stats/\n");	
	return ret;  //return succsss if all wo
}

void remove_dir(void) {
	remove_proc_subtree("arm_stats", NULL);
	printk(KERN_INFO "Successfully removed /proc/arm_stats/\n");	
}

/*************************************************************************************************/
/*************** assign vals to global pointers for global access ********************************/
/*************************************************************************************************/

//trying to use this function to get once so dont need to constantly call on irq
void set_ndev_and_epriv(void){
	//possibly needs to be value of
	//ndev = dev_get_by_name(&init_net, "enP1p1s0f0np0");   (for beast)
	ndev = dev_get_by_name(&init_net, "enp1s0");

	epriv = netdev_priv(ndev);
	printk(KERN_INFO "************** netdev and epriv set ***************\n");	
}



/*************************************************************************************************/
/********************************* use sys stats *************************************************/
/*************************************************************************************************/

void init_sys_swstats_irq_stats(void) {
	struct mlx5e_sw_stats sw_stats = epriv->stats.sw;
	sys_swstats_irq_stats.last_rx_nbytes = sw_stats.rx_bytes;
	sys_swstats_irq_stats.last_rx_npkts = sw_stats.rx_packets;
	sys_swstats_irq_stats.last_tx_nbytes = sw_stats.tx_bytes;
	sys_swstats_irq_stats.last_tx_npkts = sw_stats.tx_packets;
	printk(KERN_INFO "init_sys_swstats_irq_stats complete\n");	
}


//in second thouight is not really needed
void record_curr_sys_swstats_irq_stats(void) {
	struct mlx5e_sw_stats sw_stats = epriv->stats.sw;
	sys_swstats_irq_stats.curr_rx_nbytes = sw_stats.rx_bytes;
	sys_swstats_irq_stats.curr_rx_npkts = sw_stats.rx_packets;
	sys_swstats_irq_stats.curr_tx_nbytes = sw_stats.tx_bytes;
	sys_swstats_irq_stats.curr_tx_npkts = sw_stats.tx_packets;
	printk(KERN_INFO "record_curr_sys_swstats_irq_stats complete\n");	
}

void diff_sys_swstats_irq_stats(void) {
	sys_swstats_irq_stats.diff_rx_nbytes = sys_swstats_irq_stats.curr_rx_nbytes - sys_swstats_irq_stats.last_rx_nbytes;
	sys_swstats_irq_stats.diff_tx_nbytes = sys_swstats_irq_stats.curr_tx_nbytes - sys_swstats_irq_stats.last_tx_nbytes;
	sys_swstats_irq_stats.diff_rx_npkts = sys_swstats_irq_stats.curr_rx_npkts - sys_swstats_irq_stats.last_rx_npkts;
	sys_swstats_irq_stats.diff_tx_npkts = sys_swstats_irq_stats.curr_tx_npkts - sys_swstats_irq_stats.last_tx_npkts;
	printk(KERN_INFO "diff_sys_swstats_irq_stats complete\n");	
}

void update_sys_swstats_irq_stats(void) {
	struct mlx5e_sw_stats sw_stats = epriv->stats.sw;
	sys_swstats_irq_stats.last_rx_nbytes = sw_stats.rx_bytes;
	sys_swstats_irq_stats.last_rx_npkts = sw_stats.rx_packets;
	sys_swstats_irq_stats.last_tx_nbytes = sw_stats.tx_bytes;
	sys_swstats_irq_stats.last_tx_npkts = sw_stats.tx_packets;
    printk(KERN_INFO "update_sys_swstats_irq_stats complete\n");
}

void log_sys_swstats_irq_stats(union LogEntry *ile) {
	store_int64_asm(&(ile->Fields.tx_bytes_stats), sys_swstats_irq_stats.diff_tx_nbytes);
	store_int64_asm(&(ile->Fields.rx_bytes_stats), sys_swstats_irq_stats.diff_rx_nbytes);
	store_int64_asm(&(ile->Fields.tx_desc_stats), sys_swstats_irq_stats.diff_tx_npkts);
	store_int64_asm(&(ile->Fields.rx_desc_stats), sys_swstats_irq_stats.diff_rx_npkts);
	printk(KERN_INFO "log_sys_swstats_irq_stats complete\n");
}

/*************************************************************************************************/
/*********************************SMPRO + XGENE **************************************************/
/*************************************************************************************************/


/*
int get_power_smpro() {
	struct mlx5_core_dev *core_dev = epriv->mdev;
	struct device *dev = core_dev->device;
	struct mlx5e_channels chs = epriv->channels;
	struct mlx5e_channel **ch = chs.c;
	int ix = (*ch)->ix; //channel number passed to read energy
	long val_pwr;
	u32 attr = hwmon_power_input;

	int result = smpro_read_power(dev, attr, ix, &val_pwr);
	if(result == 0){
		return val_pwr;
	}
	else{
		return -1; 
	}
}

*/


void log_power_xgene(union LogEntry *ile) {
	
	struct mlx5_core_dev *core_dev = epriv->mdev;
    struct device *dev = core_dev->device;
	printk(KERN_INFO "entered log_power_xgene\n");
	//this is the real way to get it, using dumby one to see what happens
	struct xgene_hwmon_dev *ctx = dev_get_drvdata(dev);

	//struct xgene_hwmon_dev ctx_instance; 
	//note: this is a static func, yet to change to extern, however from smpro doesnt appear to be working  :((((
	u32 cpu_pwr;
	int xgene_ret = xgene_hwmon_get_cpu_pwr(ctx, &cpu_pwr);
	//possibly some conditional logic for ret 

	store_int64_asm(&(ile->Fields.pwr), (long long) cpu_pwr);
	printk(KERN_INFO "log_power_xgene complete\n");
}

/*************************************************************************************************/
/********************************* RECORD LOG ***************************************************/
/*************************************************************************************************/

void record_log(){
	printk(KERN_INFO "*****************entered log******************\n"); 
	struct Log *il;
   	union LogEntry *ile;
   	uint64_t now = 0;
   	int icnt = 0;
	//struct cpuidle_device *cpu_idle_dev = __this_cpu_read(cpuidle_devices);
	//struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

	//struct mlx5_core_dev *core_dev = epriv->mdev;
	//use clock to record time and cycs
	//struct mlx5_clock clock = core_dev->clock;

	//alternative way to get cpu #
    int cpu = smp_processor_id(); //might need to use cpu idle instead here 
    printk(KERN_INFO "logging for cpu=%d\n", cpu);

	int num_cpus = num_online_cpus(); 
	printk(KERN_INFO "number of online cpus=%d\n", num_cpus);

   	il = &logs[cpu];
   	icnt = il->itr_cnt;

   	if(icnt < LOG_SIZE) 
	{ 
     	ile = &il->log[icnt];
     	now = get_rdtsc_arm_phys();
		il->itr_joules_last_tsc = now;
		store_int64_asm(&(ile->Fields.tsc), now);
		
		/* NOTE: alt way to get time and counts
		//might need semapores for safe access to timer
		struct mlx5_timer timer = clock.timer;
		struct timecounter time_count = timer.tc;
		uint64_t nsec = time_count.nsec;
		uint64_t nm_cycs = time_count.cycle_last; //these r abs tho 
		*/	

		//OPTION 1: stats: using sys sw_stats struct
		//get current sys stats
		record_curr_sys_swstats_irq_stats();
		//calcualte stast on per irq basus
		diff_sys_swstats_irq_stats();
		//log sys stats
		log_sys_swstats_irq_stats(ile);
		//set current stats to last recorded value
		update_sys_swstats_irq_stats();

		//OPTION 2:stats recorded at tx/rx polling, reset at end of log
		//log poll_irq_stats
		log_poll_irq_stats(ile);
		//reset counters to null
		//reset_poll_irq_stats();
		//was indeed to reset back to 0 

	    //first get the joules
		//log_power_xgene(ile);

     	if(il->perf_started) 
		{
			//log cycles, LLCM, instructions from the PMU
			log_counters(ile);
			//now reset counters
			reset_counters();
			//log sleep state usagee
			//log_idle_states_usage(ile);
		}
		if(il->perf_started == 0) 
		{
			//enable and reset PMU counters
			enable_and_reset_regs();
		    //configure PMU to record LLCM, ncyc, ninstr
			configure_pmu();
			//sets helper struct to current val of sys software stats
			init_sys_swstats_irq_stats();
			//set as stats being started
			il->perf_started = 1;
		}		
	//increment counter to keep track of # of log entries
	il->itr_cnt++;
	printk(KERN_INFO "************* log complete **************\n"); 
    }
}



