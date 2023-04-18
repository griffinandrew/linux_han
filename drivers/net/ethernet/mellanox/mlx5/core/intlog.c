//this file is meant for the creation ofOA seq_file interface to create the files to hold each per core log
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

#include <linux/cpumask.h>
#include <linux/smp.h>
//#include "en_stats.h"
#include "mlx5_core.h"
#include "en.h"
#include "intlog.h"


/*************************************************************************
 * intLog: access tsc tick rate
 *************************************************************************/
extern unsigned int tsc_khz = 0; //this should not be init to 0

//not sure why this had to be included as extern

/*************************************************************************
 * intLog: see definitions in intlog.h
 *************************************************************************/
// store RDTSC hardware information
unsigned int tsc_per_milli;
// per core data structure for logging
struct Log logs[NUM_CORES];
/*************************************************************************/


//struct mlx4_en_cq *cq;

////////////////////////////////////////////////////////////////
struct proc_dir_entry *stats_dir;  //these were extern
struct proc_dir_entry *core_dir;
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
    seq_printf(s, "%u %u %u %u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
	       (unsigned int)*spos,
	       ile->Fields.rx_desc, ile->Fields.rx_bytes,
	       ile->Fields.tx_desc, ile->Fields.tx_bytes,
	       ile->Fields.ninstructions,
	       ile->Fields.ncycles,
	       ile->Fields.nref_cycles,
	       ile->Fields.nllc_miss,
	       ile->Fields.c0,
	       ile->Fields.c1,
	       ile->Fields.c1e,
	       ile->Fields.c3,
	       ile->Fields.c6,
	       ile->Fields.c7,
	       ile->Fields.joules,
	       ile->Fields.tsc);
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
static inline uint64_t get_rdtsc_arm(void){
	uint64_t tsc;
	asm volatile("mrs %0, cntvct_el0" : "=r" (tsc));
  	return tsc;
}


//other possible implementaion to get timestamp
static inline uint64_t get_rdtsc_arm_2(void) {
	uint64_t tsc;
 	asm volatile("mrs %[tsc], cntvct_el0\n"
	       	     :[tsc] "=r" (tsc)
	       	     :
	       	     :"memory");
  	return tsc;
}

/*
static void enable_llc_miss_counter(void) {
	//Set the event code to count last level cache misses (0x37)
	uint32_t event = 0x37;
	// Set the counter mask to enable the last level cache miss counter (bit 24)
	uint32_t mask = (1 << 24);                    
	// Write the event code and counter mask to PMSELR_EL0
	asm volatile("msr pmselr_el0, %0" :: "r"(event));
	asm volatile("msr pmccfiltr_el0, %0" :: "r"(mask));              
	//Enable the counter
	uint32_t val = 0x80000000;
	asm volatile("msr pmcntenset_el0, %0" :: "r"(val)); //i might be overwriting a different counter i use
}



//THIS IS incorrect, first this counter must be inited
//get last level cache miss arm
//llc using rdmsr command
inline uint64_t get_llcm_arm(void){
	uint64_t val;
  	uint32_t event = 0x37; //check this
  	asm volatile("msr %0, PMEVCNTR0_EL0" : "=r" (val) : "I" (event)); //there r 5 cntrs need to choose which one to enable (probs in open / alloc funcation)
  	return val;
}
*/
//gets the current instruction count
static inline long long get_cyc_count_arm(void){
	long long val;
	asm volatile("msr %0, PMCCNTR_EL0"
		     : "=r" (val));
  	return val;
}


//gets the number of ref cycles (phyiscal count register)
static inline long long get_refcyc_arm(void){
  	long long val;
  	asm volatile("msr %0, CNTPCT_EL0 " : "=r" (val));
  	return val; 
} //registeR CNTVCT_EL0 IS NON privilged version of this

//should return the current instruction count, need to verify this is correct one  
static inline long long get_instr_count_arm(void){
	long long val;
  	asm volatile("mrs %0, PMEVCNTR0_EL0" : "=r"(val));
  	return val;
}


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

/*
//attempt at re-writing in arm assembly
static inline void write_nti63_arm(void *p, const uint64_t v) {
	
	asm volatile("LDNP x8, %x[p]"
		     "stnp %x[v], %x[p], #0\n"
	             :[p] "+r" (p)
	             :[v] "r" (v)
	             : "memory");
}
*/

static inline void write_nti64_arm_test(void *p, const uint64_t v) {
    asm volatile("stp %x[v], %x[p]\n"
                 : [p] "+r" (p)
                 : [v] "r" (v)
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

//arm assembly implementaion
static inline void write_nti32_arm(void *p, const uint32_t v) {
	asm volatile("stnp %w[v], %x[p], #0\n"
	             :[p] "+r" (p)
	             :[v] "r" (v)
	             : "memory");
}

static inline void write_nti32_arm_test(void *p, const uint32_t v) {
    asm volatile("stlr %w[v], %x[p]\n"
                 : [p] "+r" (p)
                 : [v] "r" (v)
                 : "memory");
}

/*************************************************************************************************/
/******************************** ALLOC / DEALLOC LOGS *******************************************/ 
/*************************************************************************************************/ 



/*

// allocates memory for creation of log 
int alloc_log_space(void) {
        int flag = 0;
        uint64_t now;
        printk(KERN_INFO "****************** intLog init *******************");
	for(int i = 0; i < NUM_CORES; i++) 
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
	}
	tsc_per_milli = tsc_khz;
	now = get_rdtsc_arm();
	store_int64_asm(&(logs[0].log[0].Fields.tsc), now);
	printk(KERN_INFO "tsc_khz = %u now = %llu tsc = %llu \n", tsc_khz, now, logs[0].log[0].Fields.tsc);
	return flag;
}


//deallocate memory for logs
void dealloc_log_space(void){
        for(int i = 0; i < NUM_CORES; i++){ 
	      if(logs[i].log){
	              vfree(logs[i].log);
	      }
	}
}

*/
//without for 

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
	      tsc_per_milli = tsc_khz;       
	      now = get_rdtsc_arm();           
	      store_int64_asm(&(logs[0].log[0].Fields.tsc), now);   
	      printk(KERN_INFO "tsc_khz = %u now = %llu tsc = %llu \n", tsc_khz, now, logs[0].log[0].Fields.tsc);
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
/********************************* RECORD LOG ***************************************************/
/*************************************************************************************************/


//for this driver there is nothing passed to irqreturn_t func that can be used to extract the core atm
void record_log(struct mlx5e_priv *priv){
	  struct Log *il;
   	union LogEntry *ile;
   	uint64_t now = 0, last = 0;
   	int icnt = 0;
	  long long c0;
	  long long num_cycs;
    //long long num_ref_cycs;
    long long energy;
	
   	//struct cpuidle_device *cpu_idle_dev = __this_cpu_read(cpuidle_devices);
   	
	//not liking the looks of this one
	struct mlx5e_rq rq = priv->drop_rq;
	struct mlx5e_channel *ch = rq.channel;
	struct mlx5_core_dev *core_dev = priv->mdev; //(could be a &)


	//get mlx5e_stats
	struct mlx5e_stats *stats = priv->stats;
	struct mlx5e_sw_stats sw_stats = stats.sw; 

	//use clock to record time and cycs
	struct mlx5_clock clock = core_dev.clock;
	
	int cpu = ch->cpu;
	
   	il = &logs[cpu];
   	icnt = il->itr_cnt;

   	if(icnt < LOG_SIZE) 
	  {
     		ile = &il->log[icnt];
     		now = get_rdtsc_arm_2();

		//might need semapores for safe access
		struct timecounter time_count = clock->tc;
		u64 nsec = time_count.nsec;
		//let this take the place of now

		u64 nm_cycs = time_count.cycle_last;

		store_int64_asm(&(ile->Fields.tsc), now);
		store_int64_asm(&(ile->Fields.tx_bytes), sw_stats.tx_bytes); //these r both u64 ints
		store_int64_asm(&(ile->Fields.rx_bytes), sw_stats.rx_bytes);

		sw_stats.tx_bytes = 0;
		sw_stats.rx_bytes = 0; //reset counters for next interrupt

		
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
			          //num_miss = get_llcm_arm(); //this para is defined in header
			          //write_nti64_arm(&ile->Fields.nllc_miss, num_miss);
		 		
				  num_cycs = get_instr_count_arm();
		  		  store_int64_asm(&(ile->Fields.ninstructions), num_cycs);
				  //write_nti64_arm_test(&(ile->Fields.ninstructions), num_cycs);
		  
			          //	num_ref_cycs = get_refcyc_arm();
			          //	store_int64_asm(&(ile->Fields.ncycles), num_ref_cycs);
		  		      //write_nti64_arm_test(&(ile->Fields.ncycles), num_ref_cycs);

		  		      //need to include all the sleep states

				        //this is wrong the states usage struct doesnt tell time in certain state
				        //struct cpuidle_state_usage *usage;
				        //usage = cpu_idle_dev->states_usage;
				        //c0 = usage->usage;
				        //c0 = cpu_idle_dev->states_usage[0];
		  		      //c1 = cpu_idle_dev->states_usage[1];
		  		      //c2 = cpu_idle_dev->states_usage[2]; 
		  	      	//c3 = cpu_idle_dev->states_usage[3];
		  		      //log hardware stats here
		  		      // like c stats, cycles, LLCM, instructions
		       }
		
		if(il->perf_started == 0) 
		{
		  	//initilaze performance counters
		        //init ins, cycles, and ref_cyss and then start
		       il->per_started = 1;
		}		
		}
	//increment coutner to keep track of # of log entries
	il->itr_cnt++;
	  }
}
