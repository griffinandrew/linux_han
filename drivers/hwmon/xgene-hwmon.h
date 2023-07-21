#include <linux/hwmon.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <acpi/pcc.h>

enum xgene_hwmon_version {
        XGENE_HWMON_V1 = 0,
        XGENE_HWMON_V2 = 1,
};

struct slimpro_resp_msg {
        u32 msg;
        u32 param1;
        u32 param2;
} __packed;

struct xgene_hwmon_dev {
        struct device           *dev;
        struct mbox_chan        *mbox_chan;
        struct pcc_mbox_chan    *pcc_chan;
        struct mbox_client      mbox_client;
        int                     mbox_idx;

        spinlock_t              kfifo_lock;
        struct mutex            rd_mutex;
        struct completion       rd_complete;
        int                     resp_pending;
        struct slimpro_resp_msg sync_msg;

        struct work_struct      workq;
        struct kfifo_rec_ptr_1  async_msg_fifo;

        struct device           *hwmon_dev;
        bool                    temp_critical_alarm;

        phys_addr_t             comm_base_addr;
        void                    *pcc_comm_addr;
        u64                     usecs_lat;
};



int xgene_hwmon_get_cpu_pwr(struct xgene_hwmon_dev *ctx, u32 *val);



