一、功能说明
AX7020 开发板 + ADC模块（双通道输入：2个单通道AD9226芯片）  
二、文件包含：
1.导出硬件：windows Vivado 编译生成bit文件，导入SDK ，将platform0 拷贝到ubuntu中
	注：在Vivado中Export Hardware时候要把bit文件一起导出，导出之后再Launch SDK;
		 vivado工程目录下生成linux.sdk，该文件下有system_wrapper_hw_platform_0，就是我们也就是petalinux需要的文件夹。
2.添加驱动
     （需要root权限）将 fpga的驱动包 
	 解压到 /opt/Xilinx/petalinux-v2015.4-final/components/linux-kernel/xlnx-4.0/ 路径下
		如果有自己IP的驱动需要放到这个位置。例如，把 ax_adc.c 文件复制到
				/opt/Xilinx/petalinx-v2015.4-final/components/linux-kernel/xlnx-4.0/drivers/staging/iio/adc/
			再修改当前路径下的Makefle，新增一行: obj-$(CONFIG_AX_ADC) += ax_adc.o
			再修改Kconfig，新增： config AX_ADC
										tristate "AX ADC driver" ....
3.创建petalinux工程
	需要root权限，并设置环境变量
	（在需要创建petalinux工程的路径下source）
	source /opt/Xilinx/petalinux-v2015.4-final/settings.sh
	source /opt/Xilinx/Vivado/2015.4/settings64.sh
	
	创建工程 petalinux-create --type project --template zynq --name ax7020_ad9226
4.配置petalinux 硬件相关信息
	进入创建的工程路径 cd ax7020_ad9226 ，再开始配置硬件相关信息，例如配置启动方式
	 petalinux-config --get-hw-description ../****_platform_0（拷贝过来的platform_0）/
	 在出现的界面里进行配置：
	 修改启动方式（默认SD卡）*-Subsystem AUTO Hardware Settings -> 
								*Advanced bootable images storage Settings ->
									boot image settings(设置 boot 的启动位置,如果设置为“primary flash”是从 QSPI flash 启动，
									如果设置为“primarysd”是从 SD 卡启动.为了调试方便我们设置为 SD 启动.), 
									kernel image settings(设置内核的启动位置)
									-> 保存并退出
									
									
5.修改设备树
	为了驱动一些外设，必须手动修改设备树文件，这里使用 gedit 工具修改生成的设备树文件“system-top.dts”。
	gedit subsystems/linux/configs/device-tree/system-top.dts
		修改内容见另一文档。
6.配置内核
	petalinux-config -c kernel

7.配置HDMI显示驱动、时钟驱动、配置UVC驱动、自定义ip驱动（例如ADC驱动）
	在配置页面中 ，配置HDMI编码器的驱动：
	Device Drivers -> Graphics support -> Direct Rendering Manager -> Digilent VGA/HDMI DRM Encoder Driver
	配置时钟驱动，HDMI显示需要的时钟驱动：
	Device Drivers -> Common Clock Framework -> Digilent axi_dynclk Driver
	配置UVC驱动：
	Device Drivers -> Multimedia support -> Media USB Adapters
	配置自定义IP的驱动，例如，配置AD9226驱动。
	Device Drivers -> Staging drivers -> IIO staging driver -> Analog to digital converts -> AX ADC driver
8.配置文件系统
	petalinux-config -c rootfs
	在Filesystem Packages -> base -> external-xilinx-toolchain 中配置 libstdc++6.
	配置这个选项是为了支持 C++程序的运行，例如 QT 程序.
	
9.编译工程
	petalinux-build
	编译不成功可能是有些包没有装。
	
10.合并BOOT文件
	合并 BOOT 文件需要安装 vivado，并运行 vivado 的环境变量的设置。
	合并完成在目录 images/linux 下可以看到 BOOT.BIN 和 image.ub，
	把这 2 个文件复制到 SD 卡即可运行，
	如果配置了 QSPI 启动，要把这 2 个文件烧到 flash 里。
	petalinux-package --boot --fsbl ./images/linux/zynq_fsbl.elf --fpga ./images/linux/system_wrapper.bit --uboot --force

11.合并完成后可以板上运行。	
	把这 2 个文件复制到 SD 卡即可运行驱动
	
12.运行QT程序
	保持板卡和虚拟机在同一局域网。
	开发板和ubuntu的mount
	虚拟机端命令：
	mount -t nfs -o nolock localhost:/home/ubuntuforalinx123456/Documents/nfs_server /mnt
 
	开发板端命令：
	mount -t nfs -o nolock 192.168.1.118:/home/ubuntuforalinx123456/Documents/nfs_server /mnt

二、指令说明：

挂载成功后：到路径运行
/mnt/qt_project***/build-qt_test-zynq-Debug 

注意：
1. 挂载不成功： 
	1.1 查看指令，在Secure CRT端复制粘贴也会也会有错。
	1.2 ubuntu端的nfs server 是否打开
		开启nfs服务器：/etc/init.d/nfs-kernel-server start  可以看到两个OK 若没有，可能是权限的问题
		若显示占用，则关掉nfs,指令：umount /mnt再重新开一下，
		
	     
2. 执行程序不成功：
开发板的QT环境变量：
/mnt/alinx_heijin_QT 中运行 qt_env_set.sh
再运行程序