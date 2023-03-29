# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_MLX4_CORE)		+= mlx4_core.o

mlx4_core-y :=	alloc.o catas.o cmd.o cq.o eq.o fw.o fw_qos.o icm.o intf.o \
		main.o mcg.o mr.o pd.o port.o profile.o qp.o reset.o sense.o \
		srq.o resource_tracker.o crdump.o

obj-$(CONFIG_MLX4_EN)               += mlx4_en.o

mlx4_en-y := 	en_main.o en_tx.o en_rx.o en_ethtool.o en_port.o en_cq.o \
		en_resources.o en_netdev.o en_selftest.o en_clock.o intlog.o
mlx4_en-$(CONFIG_MLX4_EN_DCB) += en_dcb_nl.o


#ccflags-y := -std=gnu99 -march=armv8-a 


#-std=gcc-arm-linux-gnu-12.1.1-2.el8.aarch64

#-std=gnu99 -march=armv8-a

#-std=gnu99
