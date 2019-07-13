/*
 * Alinx AD9226 ADC driver
 *
 * Copyright 2017 Alinx Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/delay.h>

#include <linux/dma-mapping.h>

#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/dma-buf.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/errno.h>	/* error codes */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>

#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pci.h>

#define DEVICE_NUM        16

#define ADC_MEM_PAGE_NUM	128

#define ADC_BUFF_SIZE		16384            
#define ADC_MEM_BUFF_SIZE (ADC_BUFF_SIZE * ADC_MEM_PAGE_NUM)     


#define AX_ADC_MAGIC 	'x'  //定义幻数
#define AX_ADC_MAX_NR 	 2   //定义命令的最大序数，

#define COMM_GET_CURPAGENUM 	_IO(AX_ADC_MAGIC, 1)
#define COMM_SET_TEST		 	_IO(AX_ADC_MAGIC, 0)

#define AX_MAJOR 181

#define AX_DEVICES 3



struct ax_adc {
	struct class 	*cls;
	int 			major;
	char            devname[16];
	u8				*mem_vraddr1;
	u32 			mem_phyaddr1;
	u8				*mem_vraddr2;
	u32 			mem_phyaddr2;
	int          	index;
	struct mutex	mutex;
	int             mijor;
	int             ax_bufState;
	u8              *ax_mem_msg_buf;
	int             ax_mem_curpage_num;
	int             irq;
	void __iomem	*base_address;	
	struct device	*dev;
	resource_size_t  remap_size;             

};
static struct ax_adc ax_dev[DEVICE_NUM];
static int  ax_device_num = 0;


static unsigned char * malloc_reserved_mem(long size)
{


	unsigned char *p = kmalloc(size, GFP_KERNEL);
	unsigned char *tmp = p;
	unsigned int i, n;
	if(NULL == p)
	{
		printk("Error : malloc_reserved_mem kmalloc failed!\n");
		return NULL;
	}

	n = size/(ADC_BUFF_SIZE) + 1;

	if(0 == size % (ADC_BUFF_SIZE)) {
		-- n;
	}

	for(i =0; i < n; ++i)
	{
		SetPageReserved(virt_to_page(tmp));
		tmp += ADC_BUFF_SIZE;
	}

	return p;
}


static int ax_adc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	int imi = iminor(inode);
	int ima = imajor(inode);
	int i = 0;
	for(i=0;i<ax_device_num;i++)
	{
		if((ax_dev[i].major == ima) && (ax_dev[i].mijor == imi))
			break;

		if(i>=ax_device_num-1)
			return 0;			
	}

	unsigned long offset 	= vma->vm_pgoff << PAGE_SHIFT;
	unsigned long physics 	= ((unsigned long)ax_dev[i].ax_mem_msg_buf) - PAGE_OFFSET;
	unsigned long mypfn 	= physics >> PAGE_SHIFT;
	unsigned long vmsize	= vma->vm_end-vma->vm_start;
	unsigned long psize 	= ADC_MEM_BUFF_SIZE - offset; 
	if(vmsize > psize)
		return -ENXIO;

//	vma->vm_page_prot=pgprot_noncached(vma->vm_page_prot); 

	if(remap_pfn_range(vma,vma->vm_start,mypfn,vmsize,vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static long ax_adc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;


	if(_IOC_TYPE(cmd) != AX_ADC_MAGIC) return - EINVAL;
	if(_IOC_NR(cmd) > AX_ADC_MAX_NR) return - EINVAL;

	switch(cmd){
		case COMM_GET_CURPAGENUM:
			if(arg>=0 && arg <ax_device_num)
			return ax_dev[arg].ax_mem_curpage_num;
		default :
			ret = - EINVAL;
	}
	
	return ret;
}


static struct file_operations ax_adc_fops = {	
	.owner  		= THIS_MODULE,
	.unlocked_ioctl = ax_adc_ioctl,
	.mmap 			= ax_adc_mmap,
};

static irqreturn_t ax_adc_interrupt(int irq, void *dev_id)
{
	int i=0;
	int index = 0;

	for(i =0;i<ax_device_num;i++)
	{
		if(ax_dev[i].irq == irq)
		{
			index = i;
			break;
		}
		
		if(i>=ax_device_num-1)
		{
			printk(KERN_ALERT "ax_adc_interrupt irq error\n");				
			return IRQ_HANDLED;
		}
	}
	

	int count = 0;

	mutex_lock(&ax_dev[index].mutex);		

	int state = readl(ax_dev[index].base_address  + 0x3 * 4);
	ax_dev[index].ax_bufState = (state>>3) & 0x7;
	count = readl(ax_dev[index].base_address + 0x5 * 4);

	if(ax_dev[index].ax_mem_msg_buf==NULL)
	{
		printk(KERN_ALERT "ax_adc_interrupt ax_mem_msg_buf=NULL error\n");	
	}else{
		if(ax_dev[index].ax_mem_curpage_num<0)
		{
			printk(KERN_ALERT "ax_adc curpage error\n");	
		}else
		if(ax_dev[index].ax_mem_curpage_num< ADC_MEM_PAGE_NUM )   //物理内存数据拷贝到共享内存区域
		{

			if(ax_dev[index].ax_bufState)
			{
				memcpy(ax_dev[index].ax_mem_msg_buf+ax_dev[index].ax_mem_curpage_num*ADC_BUFF_SIZE,ax_dev[index].mem_vraddr2,ADC_BUFF_SIZE);

			}else{
				memcpy(ax_dev[index].ax_mem_msg_buf+ax_dev[index].ax_mem_curpage_num*ADC_BUFF_SIZE,ax_dev[index].mem_vraddr1,ADC_BUFF_SIZE);
			}

			ax_dev[index].ax_mem_curpage_num++;

			if(ax_dev[index].ax_mem_curpage_num>= ADC_MEM_PAGE_NUM)
				ax_dev[index].ax_mem_curpage_num = 0;
		}
	
	}

	writel(1, ax_dev[index].base_address  + 0x4 * 4);

	udelay(1);

	writel(0, ax_dev[index].base_address  + 0x4 * 4);

	mutex_unlock(&ax_dev[index].mutex);

	return IRQ_HANDLED;
}

static int ax_adc_probe(struct platform_device *pdev)
{
	struct ax_adc   ax;
	struct resource 	*res;
	int					err, i;

	struct device *dev;
	
	dev_t devt;

	for(i=0;i<DEVICE_NUM;i++)
	{

		memset(ax.devname,0,16);
		snprintf(ax.devname, sizeof(ax.devname)-1, "ax_adc%d", i+1);

		ax.major = register_chrdev(0, ax.devname, &ax_adc_fops);

		
		ax.cls = class_create(THIS_MODULE, ax.devname);
		ax.mijor = i;
		ax.index = i;
		dev = device_create(ax.cls, &pdev->dev, MKDEV(ax.major, ax.mijor), NULL, ax.devname);
		if (IS_ERR(dev)) {
			class_destroy(ax.cls);
			unregister_chrdev(ax.major, ax.devname);

			if(i>=DEVICE_NUM-1)
			{
				return 0;
			}
		
		}else
		{

			ax_device_num++;			
			break;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL)
	{
		printk(KERN_ALERT "ax_adc_probe res error!\n");
		return -ENOENT;
	}


	ax.base_address = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ax.base_address))
		return PTR_ERR(ax.base_address);



	ax.remap_size = resource_size(res);

	ax.irq = platform_get_irq(pdev,0);
	if (ax.irq <= 0)
		return -ENXIO;

	mutex_init(&ax.mutex);

	ax.dev = &pdev->dev;
	ax.ax_bufState			= 0;
	ax.ax_mem_msg_buf 		= NULL;
	ax.ax_mem_curpage_num 	= 0;

	ax.mem_vraddr1 = dma_alloc_writecombine(ax.dev, ADC_BUFF_SIZE, &ax.mem_phyaddr1, GFP_KERNEL);

	if (!ax.mem_vraddr1)
		goto fail1;

	ax.mem_vraddr2 = dma_alloc_writecombine(ax.dev, ADC_BUFF_SIZE, &ax.mem_phyaddr2, GFP_KERNEL);

	if (!ax.mem_vraddr2)
		goto fail2;	

	writel(ax.mem_phyaddr1, ax.base_address + 0x13 * 4);
	writel(ax.mem_phyaddr2, ax.base_address + 0x14 * 4);	
	writel(ADC_BUFF_SIZE, ax.base_address + 0x11 * 4);
	writel(0, ax.base_address + 0x12 * 4);
	writel(1, ax.base_address + 0x12 * 4);

	ax.ax_mem_msg_buf = malloc_reserved_mem(ADC_MEM_BUFF_SIZE);
	if(ax.ax_mem_msg_buf==NULL)
	{
		printk(KERN_ALERT "ax_adc_probe malloc error\n");

		goto fail3;
	}


	err = request_threaded_irq(ax.irq, NULL,
				ax_adc_interrupt,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				ax.devname, &ax);				   
	if (err) {
		printk(KERN_ALERT "ax_adc_probe irq	error=%d\n", err);

		goto fail3;
	}

	memcpy(&ax_dev[i], &ax, sizeof(struct ax_adc));
	
	platform_set_drvdata(pdev, &ax_dev[i]);	

	return 0;

fail3:
	if(ax.ax_mem_msg_buf!=NULL)
	{
		kfree(ax.ax_mem_msg_buf);
		ax.ax_mem_msg_buf = NULL;
	}	
	free_irq(ax.irq, &ax);
	dma_free_writecombine(ax.dev, ADC_BUFF_SIZE, ax.mem_vraddr2, ax.mem_phyaddr2);
fail2:
	dma_free_writecombine(ax.dev, ADC_BUFF_SIZE, ax.mem_vraddr1, ax.mem_phyaddr1);
fail1:
	device_destroy(ax.cls, MKDEV(ax.major, ax.mijor));	
	class_destroy(ax.cls);
	unregister_chrdev(ax.major, ax.devname);

	return -ENOMEM;

}

static int ax_adc_remove(struct platform_device *pdev)
{
	int i = 0;

	for(i=0;i<ax_device_num;i++)
	{
		device_destroy(ax_dev[i].cls, MKDEV(ax_dev[i].major, ax_dev[i].mijor));	
		class_destroy(ax_dev[i].cls);
		unregister_chrdev(ax_dev[i].major, ax_dev[i].devname);
		
		
		if(ax_dev[i].ax_mem_msg_buf!=NULL)
		{
			kfree(ax_dev[i].ax_mem_msg_buf);
			ax_dev[i].ax_mem_msg_buf = NULL;
		}
		free_irq(ax_dev[i].irq, &ax_dev[i]);
		
		dma_free_writecombine(ax_dev[i].dev, ADC_BUFF_SIZE, ax_dev[i].mem_vraddr1, ax_dev[i].mem_phyaddr1);
		dma_free_writecombine(ax_dev[i].dev, ADC_BUFF_SIZE, ax_dev[i].mem_vraddr2, ax_dev[i].mem_phyaddr2);


	}


	return 0;
}

static int ax_adc_suspend(struct device *dev)
{

	return 0;
}

static int ax_adc_resume(struct device *dev)
{
 
	return 0;
}

static const struct dev_pm_ops ax_adc_pm_ops = {
	.suspend = ax_adc_suspend,
	.resume  = ax_adc_resume,
};


MODULE_DEVICE_TABLE(platform, ax_adc_driver_ids);

static const struct of_device_id ax_adc_of_match[] = {
	{.compatible = "alinx,ax-adc" },
	{ }
};
MODULE_DEVICE_TABLE(of, ax_adc_of_match);


static struct platform_driver ax_adc_driver = {
	.probe = ax_adc_probe,
	.remove	= ax_adc_remove,
	.driver = {
		.owner   		= THIS_MODULE,
		.name	 		= "ax-adc",
		.pm    			= &ax_adc_pm_ops,
		.of_match_table	= ax_adc_of_match,		
	},
};

module_platform_driver(ax_adc_driver);

MODULE_AUTHOR("Guowc");
MODULE_DESCRIPTION("Alinx ADC");
MODULE_LICENSE("GPL v2");
