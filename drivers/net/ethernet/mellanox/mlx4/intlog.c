//this file is meant for the creation ofOA seq_file interface to create the files to hold each per core log
//this modifications are largely taken from Han Dong https://github.com/handong32/linux/blob/6d082d375b40e34f6b1f620101fd99e6c55089a4/drivers/net/ethernet/intel/ixgbe/ixgbe_main.c
//and are being reworked to form a eventually generic system for logging with different drivers, currently working on mlx5 specfic implementaion


/*************************************************************************
 * intLog: to access intel C-state information, again this is just for intel, trying to expand so probs not valid
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

#include "intlog.h"
#include <linux/cpumask.h>
#include <linux/smp.h>


/*************************************************************************
 * intLog: access tsc tick rate
 *************************************************************************/
extern unsigned int tsc_khz;

/*************************************************************************
 * intLog: see definitions in intlog.h
 *************************************************************************/
// store RDTSC hardware information
unsigned int tsc_per_milli;
// per core data structure for logging
struct Log logs[NUM_CORES];
/*************************************************************************/


////////////////////////////////////////////////////////////////
//these are not valid, for this implementation, trying to rework 
static struct proc_dir_entry *ixgbe_stats_dir; //there is no dir only files
static struct proc_dir_entry *ixgbe_core_dir; //i am in the core dir? if that correct? 
////////////////////////////////////////////////////////////////

/*********************************************************************************
 * intLog: seq_file interfaces needed to create procfs: /proc/ixgbe_stats/core/N
 *         (https://www.kernel.org/doc/html/latest/filesystems/seq\_file.html)
 *********************************************************************************/

static void *ct_start(struct seq_file *s, loff_t *pos)
{
  loff_t *spos;
  struct IxgbeLog *il;
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

static void ct_stop(struct seq_file *s, void *v)
{
  kfree(v);  
}

/*static struct seq_operations my_seq_ops =
{
 .next  = ct_next,
 .show  = ct_show,
 .start = ct_start,
 .stop  = ct_stop,
};
*/ //currently in the header file 

static int ct_open(struct inode *inode, struct file *file)
{
  int ret;
  
  ret = seq_open(file, &my_seq_ops);
  if(ret == 0) {
    struct seq_file *m = file->private_data;
    m->private = PDE_DATA(inode);
  }
  
  return ret; 
}

/*static const struct file_operations ct_file_ops =
{
 .owner   = THIS_MODULE,
 .open    = ct_open,
 .read    = seq_read,
 .llseek  = seq_lseek,
 .release = seq_release
};
*/ // in the header 

/*************************************************************************************************/
/********************************** arch specific asm getters ************************************/
/*************************************************************************************************/ 

//function to get current RDTSC Timestamp intel
static inline uint64_t get_rdtsc_intel(void){
	unit64_t tsc;
	asm volatile("rdtsc;" 
		     "shl $32,%%rdx;"
		     "or %%rdx,%%rax"
		     : "=a"(tsc)
		     : 
		     : "%rcx","rdx");
       return tsc;
}

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

//get last level cache miss arm
//llc using rdmsr command
static inline uint64_t get_llcm_arm(void){
  uint64_t val;
  uint32_t event = 0x37; //check this
  asm volatile("msr %0, PMEVCNTR%d_EL0" : "=r" (val) : "I" (event));
  return val;
}

//gets the current instruction count
static inline uint64_t get_cyc_count_arm(void){
  unit64_t val;
  asm volatile("msr %0, PMCCNTR_EL0"
	       : "=r" (val));
  return val;
}

//gets the number of ref cycles 
static inline unit64_t get_refcyc_arm(void){
  unit64_t val;
  asm volatile("msr %0, CNTPCT_EL0"
	       : "=r" (val));
  return val;
}


//should return the current instruction count, need to verify this is correct one  
static inline uint64_t get_instr_count(void){
  uint64_t val;
  asm volatile("mrs %0, PMEVCNTR0_EL0" : "=r"(val));
  return val;
}




/*************************************************************************************************/
/********************************** GETTER TO EXTARCT CORE # *************************************/
/*************************************************************************************************/

//to be called before record log inorder to then pass number to record_log                                                                                                                                                                                              
static int get_core_number(struct mlx4_en_cq *cq)
{
        int core_num = cpumask_any(&cpu_online_mask);
        if (cq && cq->dev)
        {
          core_num = cpumask_any(&dev_to_cpumask(cq->dev)->cpumask);
        }
        return core_num;
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


//attempt at re-writing in arm assembly
static inline void write_nti64_arm(void *p, const uint64_t v) {
  asm volatile("stnp %x[v], %x[p], #0\n"
	       :[p] "+r" (p)
	       :[v] "r" (v)
	       : "memory");

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

/*
static inline uint32_t get_instruction_count(void)
{
    uint32_t count;
    __asm__ __volatile__("mrc p15, 0, %0, c9, c13, 0" : "=r" (count));
    return count;
}
*/


/*************************************************************************************************/
/******************************** ALLOC / DEALLOC LOGS *******************************************/ 
/*************************************************************************************************/ 


// allocates memory for creation of log 
static int alloc_log_space(struct net_device *dev) { //this might also be the wrong parameter / it is not even being used
  int flag = 0;
	printk(KERN_INFO "****************** intLog init *******************"
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
	now = get_rdtsc();
	write_nti64_arm(&(logs[0].log[0].Fields.tsc), now); //instruction must be changed based on arch
	printk(KERN_INFO "tsc_khz = %u now = %llu tsc = %llu \n", tsc_khz, now, logs[0].log[0].Fields.tsc);
	return flag;
}


//deallocate memory for logs
static void dealloc_log_space(struct net_device *dev)
{
	  for (int i = 0; i < NUM_CORES; i++)
	  {  // number of cores here should be a macro that can easily be changed across systems
	    if(logs[i].log)
	      {
	      vfree(logs[i].log);
	      }
	  }
}

/*************************************************************************************************/
/********************************* RECORD LOG ***************************************************/
/*************************************************************************************************/
	
//why not just have a function that records the log for us and we just call to it?

 static void record_log(struct mlx4_en_cq *cq)
 {
   struct Log *il;
   struct LogEntry *ile;
   struct mlx4_en_priv *priv = netdev_priv(cq->dev);
   uint64_t now = 0, last = 0;
   int icnt = 0;
   struct cpuidle_device *cpu_idle_dev = __this_cpu_read(cpuidle_devices);
   int core_n =	get_core_number(cq);
   
   il = &logs[core_n];
   icnt = il->itr_cnt;

   if (icnt < LOG_SIZE) {
     ile = &il->log[icnt];
     now = get_rdtsc_arm2();
     write_nti64_arm(&ile->Fields.tsc, now);
     write_nti32_arm(&(ile->Fields.tx_bytes), priv->pf_stats->tx_bytes);
     write_nti32_arm(&(ile->Fields.rx_bytes), priv->pf_stats->rx_bytes);

     //get last rdtsc
     last = il->itr_joules_last_tsc;

     if ( (now - last) > tsc_per_mill){
	     //first get the joules
	     
	     //store current rdtsc
		il->itr->joules_last_tsc = now;

       // in here architecture specific rdmsrl(src,dest) to gather other stats

     		if(il->perf_started) {
		  uint64_t num_miss = get_llcm_arm(); //this para is defined in header
		  write_nti64_arm(&ile->Fields.nllc_miss, num_miss);
		  uint64_t num_cycs = get_instr_count_arm();
		  write_nti64_arm(&ile->Fields.ninstructions, num_cycs);
		  uint64_t num_ref_cycs = get_refcyc_arm();
		  write_nti64_arm(&ile->Fields.ncycles, num_ref_cycles);

		  //need to include all the sleep states
		  //not sure why he created his own array within the idle struct to store intel sleep states

		  uint64_t c0 = cpu_idle_dev->idle_states_usage[0];
		  uint64_t c1 =	cpu_idle_dev->idle_states_usage[1];
		  uint64_t c2 =	cpu_idle_dev->idle_states_usage[2]; 
		  uint64_t c3 =	cpu_idle_dev->idle_states_usage[3];

		  write_nti64_arm(&ile->Fields.c0, c0); //these sleep states are not accurate to arm type
		  write_nti64_arm(&ile->Fields.c1, c1);
		  write_nti64_arm(&ile->Fields.c1e, c2);
		  write_nti64_arm(&ile->Fields.c3, c3);

		  
		  //log hardware stats here
		  // like c stats, cycles, LLCM, instructions
			
		}

		if (il->perf_started == 0) {
		  //initilaze performance counters
     }
   }
 }
  
	  
