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


/*************************************************************************
 * intLog: access tsc tick rate
 *************************************************************************/
extern unsigned int tsc_khz;
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

//struct mlx4_en_cq *cq;

////////////////////////////////////////////////////////////////
struct proc_dir_entry *stats_dir;
struct proc_dir_entry *stats_core_dir;
////////////////////////////////////////////////////////////////

/*********************************************************************************
 * intLog: seq_file interfaces needed to create procfs: /proc/ixgbe_stats/core/N
 *         (https://www.kernel.org/doc/html/latest/filesystems/seq\_file.html)
 *********************************************************************************/

void *ct_start(struct seq_file *s, loff_t *pos)
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
void *ct_next(struct seq_file *s, void *v, loff_t *pos)
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
int ct_show(struct seq_file *s, void *v)
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
    	seq_printf(s, "%u %u %u %u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", (unsigned int)*spos, ile->Fields.rx_desc, ile->Fields.rx_bytes, ile->Fields.tx_desc, ile->Fields.tx_bytes, ile->Fields.ninstructions, ile->Fields.ncycles, ile->Fields.nref_cycles, ile->Fields.nllc_miss, ile->Fields.c0, ile->Fields.c1, ile->Fields.c1e, ile->Fields.c3, ile->Fields.c6, ile->Fields.c7, ile->Fields.joules,ile->Fields.tsc);
  	}
  	return 0;
}

void ct_stop(struct seq_file *s, void *v)
{
  	kfree(v);  
}

int ct_open(struct inode *inode, struct file *file)
{
  	int ret;
  
  	ret = seq_open(file, &my_seq_ops_intlog);
  	if(ret == 0) {
  		struct seq_file *m = file->private_data;
		m->private = PDE_DATA(inode);
  	}
  
  	return ret; 
}

/*************************************************************************************************/
/*************************** defining structs for reaping data ***********************************/
/*************************************************************************************************/

const struct file_operations ct_file_ops_intlog =
{
	.owner   = THIS_MODULE,
	.open    = ct_open,
 	.read    = seq_read,
 	.llseek  = seq_lseek,
 	.release = seq_release
};

struct seq_operations my_seq_ops_intlog =
{
 	.next  = ct_next,
 	.show  = ct_show,
 	.start = ct_start,
 	.stop  = ct_stop,
};

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
  	return tsc;
}

//other possible implementaion to get timestamp
//gets virtual timer
//from CNTV_TVAL to CNTV_TVAL_EL0
static inline uint64_t get_rdtsc_arm_vir(void) {
	uint64_t tsc;
	asm volatile("mrs %0, CNTV_TVAL_EL0" : "=r" (tsc));
  	return tsc;
}

static inline void enable_and_reset_regs(void){
	uint32_t pmcr_val = 0;
	pmcr_val |= (1 << 0);  // Enable all counters 
	pmcr_val |= (1 << 1);  // Reset all counters to 0 
	asm volatile("msr pmcr_el0, %0" : : "r" (pmcr_val));
}

static void reset_counters(void){
	uint32_t pmcr_val = 0;
	pmcr_val |= (1 << 1);  // Reset all counters to 0 
	asm volatile("msr pmcr_el0, %0" : : "r" (pmcr_val)); 
}


//newest config
void configure_pmu(void)
{
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
}

void read_counters(uint64_t* values)
{
    uint32_t pmxevcntr0_val, pmxevcntr1_val, pmxevcntr2_val;

    // Read from counter 0 LLC miss
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr0_val));
    
    // Read from counter 1 cycle count
    asm volatile("msr PMSELR_EL0, %0" : : "r" (1));
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr1_val));
    
    // Read from counter 2, instruction count
    asm volatile("msr PMSELR_EL0, %0" : : "r" (2));
    asm volatile("mrs %0, PMXEVCNTR_EL0" : "=r" (pmxevcntr2_val));

    values[0] = pmxevcntr0_val;
    values[1] = pmxevcntr1_val;
    values[2] = pmxevcntr2_val;
}

//gets the number of ref cycles (phyiscal count register)
static inline long long get_refcyc_arm(void){
  	long long val;
  	asm volatile("msr %0, CNTPCT_EL0 " : "=r" (val));
  	return val; 
} //registeR CNTVCT_EL0 IS NON privilged virtual version of this

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
        : "memory"
    );
}

static inline void store_int32_asm(void *p, const uint32_t v) {
    asm volatile(
        "str %w[v], [%x[p]]"
        : [p] "+r" (p)
        : [v] "r" (v)
        : "memory"
    );
}

static inline void write_nti32_intel(void *p, const uint32_t v) {
	asm volatile("movnti %0, (%1)\n\t"
		     	 :
		     	 : "r"(v), "r"(p)
	             : "memory");
}


/*************************************************************************************************/
/******************************** GET / display IDLE STATES *******************************************/ 
/*************************************************************************************************/ 

void cpu_idle_states(void) {
    struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
    struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);
    //print states
    int i;
    printk(KERN_INFO "cpuidle stats state_count=%d\n", drv->state_count);
    for(i=0;i<drv->state_count;i++) {
        printk(KERN_INFO "i=%d name=%s exit_latency=%u target_residency=%u power_usage_mW=%i\n", i, drv->states[i].name, drv->states[i].exit_latency, drv->states[i].target_residency, drv->states[i].power_usage);
    }
}

/*************************************************************************************************/
/******************************** ALLOC / DEALLOC LOGS *******************************************/ 
/*************************************************************************************************/ 
//without for bc C vers

// allocates memory for creation of log                                                                                                                                                                                            
int alloc_log_space(void) {                                                                                                                                                                                                        
	int flag = 0;
	int i = 0;
    uint64_t now;                                                                                                                                                                                                                    
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

    
	/*************** clear sw_stats *****************/
	//struct mlx5e_stats stats = priv->stats;
	//struct mlx5e_sw_stats sw_stats= stats.sw;
	//one of these should be a pointer 

	//reset
	//sw_stats.tx_bytes = 0;
	//sw_stats.rx_bytes = 0;
	//sw_stats.tx_packets = 0; 
	//sw_stats.rx_packets = 0;

	tsc_per_milli = tsc_khz;       
	now = get_rdtsc_arm_phys();//possible func to get rdtsc            
	store_int64_asm(&(logs[0].log[0].Fields.tsc), now);   
	printk(KERN_INFO "tsc_khz = %u now = %llu tsc = %llu \n", tsc_khz, now, logs[0].log[0].Fields.tsc);
	//use cpu idle fun c to dipsplay idle states and stats
	cpu_idle_states();
	
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
}    


/*************************************************************************************************/
/********************************* RECORD per irq info *******************************************/
/*************************************************************************************************/

/*
void record_tx_poll_info(struct mlx5e_cq *cq, u64 npkts) {
	struct Log *il;
	struct mlx5e_channel *ch = cq->channel;
	int cpu = ch->cpu;
	il = &logs[cpu];
	il->per_itr_tx_packets += npkts;
}

void record_rx_poll_info(struct mlx5e_cq *cq, u64 npkts) {
	struct Log *il;
        struct mlx5e_channel *ch = cq->channel;
        int cpu = ch->cpu;
        il = &logs[cpu];
        il->per_itr_rx_packets += npkts;
}


*/
//I don't think this function is necessary
//espcailly if i am taking packets to be descriptors, just use sw_stats, why not?

/*************************************************************************************************/
/********************************* RECORD LOG ***************************************************/
/*************************************************************************************************/


//for this driver there is nothing passed to irqreturn_t func that can be used to extract the core atm
void record_log(){
	struct Log *il;
   	union LogEntry *ile;
   	uint64_t now = 0, last = 0;
   	int icnt = 0;
	uint64_t counters[3];
	//long long c0, c1, c2;
	//long long num_cycs;
	//long long n_instr;
    //long long num_ref_cycs;
    //long long energy;
	//long long num_miss;
	//struct cpuidle_device *cpu_idle_dev = __this_cpu_read(cpuidle_devices);
	//struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);
    //int power_usage;
	//int cpu_num;
	//not liking the looks of this one
	//struct mlx5e_rq rq = priv->drop_rq;
	//struct mlx5e_channel *ch = rq.channel;
	struct mlx5_core_dev *core_dev = priv->mdev; //(could be a &)


	//get mlx5e_stats
	//struct mlx5e_stats stats = priv->stats;
	//struct mlx5e_sw_stats sw_stats = stats.sw; 

	//use clock to record time and cycs
	struct mlx5_clock clock = core_dev->clock;
	
	//int cpu = ch->cpu;

	//alternative way to get cpu #
        int cpu = smp_processor_id(); // might be easier? but still need priv to get stats

   	il = &logs[cpu];
   	icnt = il->itr_cnt;

   	if(icnt < LOG_SIZE) 
	{ 
     	ile = &il->log[icnt];
     	now = get_rdtsc_arm_phys();

		//might need semapores for safe access
		struct timecounter time_count = clock.tc;
		uint64_t nsec = time_count.nsec;
		//let this take the place of now
		uint64_t nm_cycs = time_count.cycle_last;

		//possibly when initilizing these feilds need to zero 
		store_int64_asm(&(ile->Fields.tsc), now);
		//store_int64_asm(&(ile->Fields.tx_bytes), sw_stats.tx_bytes); //these r both u64 ints			
		//store_int64_asm(&(ile->Fields.rx_bytes), sw_stats.rx_bytes);
		//store_int64_asm(&(ile->Fields.tx_desc), sw_stats.tx_packets);
		//store_int64_asm(&(ile->Fields.rx_desc), sw_stats.rx_packets);

		//sw_stats.tx_bytes = 0;
		//sw_stats.rx_bytes = 0; //reset counters for next interrupt
		//sw_stats.tx_packets = 0;
		//sw_stats.rx_packets = 0; 

     	//get last rdtsc
     	last = il->itr_joules_last_tsc;
		
        if((now - last) > tsc_per_milli)
		{
		    //store current rdtsc
		    il->itr_joules_last_tsc = now;
	     	//first get the joules
			//store_int64_asm(&(ile->Fields.joules), energy);
	   
     		if(il->perf_started) 
			{
				//c stats, cycles, LLCM, instructions
				read_counters(counters);
				//num_miss = get_llcm_arm(); //this para is defined in header
			    store_int64_asm(&(ile->Fields.nllc_miss), counters[1]);
				//write_nti64_arm(&ile->Fields.nllc_miss, num_miss);
				//num_cycs = get_instr_count_arm();
		  		store_int64_asm(&(ile->Fields.ncycles), counters[2]);
				//write_nti64_arm_test(&(ile->Fields.ninstructions), num_cycs);      
			    store_int64_asm(&(ile->Fields.ninstructions), counters[3]);

				//now reset counters
				reset_counters();

			    //num_ref_cycs = get_refcyc_arm();
			    //store_int64_asm(&(ile->Fields.ncycles), num_ref_cycs);
		  		//write_nti64_arm_test(&(ile->Fields.ncycles), num_ref_cycs);
		  		//need to include all the sleep states
				//usage = cpu_idle_dev->states_usage;
			    //c0 = cpu_idle_dev->states_usage[0];
		  		//c1 = cpu_idle_dev->states_usage[1];
		  		//c2 = cpu_idle_dev->states_usage[2]; 
		  	    //c3 = cpu_idle_dev->states_usage[3];
		  		//log hardware stats here
		  		
		    }
			if(il->perf_started == 0) 
			{
		  		//initilaze performance counters
		        //init ins, cycles, and ref_cyss and then start
				enable_and_reset_regs();
		        configure_pmu();
				il->perf_started = 1;
			}		
	 	}
	//increment coutner to keep track of # of log entries
	il->itr_cnt++;
    }
}

/*************************************************************************************************/
/******************************** create dir /proc/arm_stats/core/N ******************************/
/*************************************************************************************************/

int create_dir(void) {
	stats_dir = proc_mkdir("arm_stats", NULL);
	unsigned long int i=0;
	if(!stats_dir) {
		printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/\n");
		return -ENOMEM;
	}
	stats_core_dir = proc_mkdir("core", stats_dir);
	if(!stats_core_dir) {
		printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/core/\n");
		return -ENOMEM; //memory error???
	}
	while(i<NUM_CORES) {
		char name [4]; //not sure why size 5
       		sprintf(name, "%ld", i);
 		if(!proc_create_data(name, 0444, stats_core_dir, &ct_file_ops_intlog, (void *)i)) {
			printk(KERN_ERR "Couldn't create base directory /proc/arm_stats/core/%ld\n", i);
		}
	i++;
	}
	printk(KERN_INFO "Successfully loaded /proc/arm_stats/\n");	
	return 0;
}

void remove_dir(void) {
	remove_proc_subtree("arm_stats", NULL);
}


//trying to use this function to get once so dont need to constantly call on irq
void set_ndev_and_epriv(void){
	ndev = dev_get_by_name(&init_net, "enP1p1s0f0np0");
	epriv = netdev_priv(ndev);
}
