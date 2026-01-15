/* bcm2835.c
// C and C++ support for Broadcom BCM 2835 as used in Raspberry Pi
// http://elinux.org/RPi_Low-level_peripherals
// http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
//
// Author: Mike McCauley
// Copyright (C) 2011-2013 Mike McCauley
// $Id: bcm2835.c,v 1.27 2019/07/22 23:04:24 mikem Exp mikem $
// Rpf port kernel space 2019/10/01.
//    this contradics many rules of kernel programming, so just enjoy it or leave it
//       but if you want to get some tast of bare metal it is just right ;)
*/

#include "bcm2835_gpio.h"
#include <stdint.h>

#ifdef BCM2835_MODULE_BUILD
#include <stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <asm/div64.h>
#include <asm-generic/barrier.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>    // Using kthreads alternative to timer 
#include <linux/delay.h>	  // for delay
#include <linux/compat.h>
#include <linux/compat_time.h>
#else
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#endif


uint32_t *bcm2835_peripherals_base = (uint32_t *)BCM2835_PERI_BASE;
uint32_t bcm2835_peripherals_size = BCM2835_PERI_SIZE;

/* Virtual memory address of the mapped peripherals block
 */
uint32_t *bcm2835_peripherals = (uint32_t *)MAP_FAILED;

/* And the register bases within the peripherals block
 */
volatile uint32_t *bcm2835_gpio        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_pwm         = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_clk         = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_pads        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_spi0        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_bsc0        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_bsc1        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_st	       = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_aux	       = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_spi1        = (uint32_t *)MAP_FAILED;
volatile uint32_t *bcm2835_irq         = (uint32_t *)MAP_FAILED;


/* This variable allows us to test on hardware other than RPi.
// It prevents access to the kernel memory, and does not do any peripheral access
// Instead it prints out what it _would_ do if debug were 0
 */
static uint8_t debug = 0;

/* RPI 4 has different pullup registers - we need to know if we have that type */

static uint8_t pud_type_rpi4 = 0;

/* RPI 4 has different pullup operation - make backwards compat */

static uint8_t pud_compat_setting = BCM2835_GPIO_PUD_OFF;

/* I2C The time needed to transmit one byte. In microseconds.
 */
static int i2c_byte_wait_us = 0;

static struct resource *request;	// reservation "handle"
#ifdef BCM2835_MODULE_BUILD
//static enum hrtimer_restart function_timer(struct hrtimer *);
//static struct hrtimer htimer;
//static ktime_t kt_periode;
static struct task_struct *task;            // The pointer to the thread task

static tCapture capture_data[CAPTURE_VALUES];
static int32_t start;		// -1 not yet started, >=0 index for start
static u_int32_t trigger_mask;	// mask to identify trigger
static u_int32_t pre_trigger = CAPTURE_VALUES/4;	// pre triggger
DEFINE_SPINLOCK(lock);
void init_capture(void) {
	//pos = -1;
	//htimer.function = NULL;
	task = NULL;
	//                          m  u  n
	//kt_periode = ktime_set(0,  10000000ll); //seconds,nanoseconds
	//hrtimer_init (&htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
}

void start_timer(void) {
	//if (htimer) {
	//htimer.function = function_timer;
	//hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
	start = -1;
	if (task)
		return;
	task = kthread_run(capture, NULL, "logic capture");  // Start 
	if(IS_ERR(task)){                                    
		printk(KERN_ALERT "start_timer: failed to create the task");
	}
}

void stop_timer(void) {
	if (task) 
		kthread_stop(task);                      // Stop the LED flashing thread
  
	//if (htimer.function)
	//	hrtimer_cancel(& htimer);
	//htimer.function = NULL;
}

void set_capture_mask(uint32_t mask) {
	trigger_mask = mask;
}

void set_pre_trigger(uint32_t pre) {
	pre_trigger = pre;
}

void bcm2835_irq_disable(uint32_t irq) {
	uint32_t irqClr;
	volatile uint32_t* paddr_irqdis;
	if (irq >= GPIO_IRQ2_BASE) {
		paddr_irqdis = bcm2835_irq + SHIFT4(BCM2835_IRQ_DIS2);
		irq -= GPIO_IRQ2_BASE;
	}
	else {
		paddr_irqdis = bcm2835_irq + SHIFT4(BCM2835_IRQ_DIS1);
	}
	irqClr = 1u << irq;
	printk(KERN_INFO "bcm2835_irq_disable: disable irq %x %x", (uint32_t)paddr_irqdis, irqClr);
	bcm2835_peri_write(paddr_irqdis, irqClr);	
}

void start_capture(void) {	
	start_timer();
}

u_int32_t capture_size(void) {
	return CAPTURE_VALUES;
}

/* return 0 if not ready 
 *   the number of elements and filled data area otherwise 
 */ 
u_int32_t capture_ready(tCapture *data) {
	int32_t i,j;
	uint32_t lastTime;
	uint64_t timeHigh;
	//tCapture *in,*out;
	//printk(KERN_INFO "Checking ready pos %x start %x.\n", pos, start);
	if (start < 0) {
		return 0;
	}
	j = start;
	lastTime = 0;
	timeHigh = 0l;
	//printk(KERN_INFO "Filling data start %x.\n", start);
	for (i = 0; i < CAPTURE_VALUES; ++i) {
		data[i] = capture_data[j];
		if (++j >= CAPTURE_VALUES) {
			j = 0;
		}		
	}
	return CAPTURE_VALUES;
}

/* capture is free running when it is started 
 *   if the trigger is recognized the capture runs til the buffer is filled
 *     to allow some pre-trigger observation
 */ 
int capture(void *arg) {
	volatile uint32_t val_ren;
	volatile uint32_t val_fen;
	volatile uint32_t* paddr_gpeds0;
	volatile uint32_t* paddr_gplev0;
	volatile uint32_t* paddr_stClo;
	//uint64_t time;
	uint32_t value_eds;
	int32_t pos;			// -1 no capture, >= 0 actual write position 0...VALUES-1
	tCapture *capt;
	
	val_ren = bcm2835_gpio_ren_multi_get();
	val_fen = bcm2835_gpio_fen_multi_get();
	//printk(KERN_INFO "capture: ren %x fen %x", val_ren, val_fen);
	bcm2835_gpio_fsel(4, BCM2835_GPIO_FSEL_INPT);
	// this is used for testing timing 
	bcm2835_pwm_set_clock(BCM2835_PWM_CLOCK_DIVIDER_128);	// 150kHz can't be done inside lock as it uses delay
	bcm2835_pwm_set_mode(0, 1, 1);
	bcm2835_pwm_set_range(0, 10);	// result is 42kHz signal 0x34 samples/period = 52 => 2.184M samples/s !!!
	bcm2835_pwm_set_data(0, 1);		// 10%
	bcm2835_gpio_fsel(18, BCM2835_GPIO_FSEL_ALT5);
	// disable gpio interrupts we wont restore this ... (you have to reboot to use gpio irq again)
	//bcm2835_irq_disable(GPIO_IRQ0);
	//bcm2835_irq_disable(GPIO_IRQ1);
	//bcm2835_irq_disable(GPIO_IRQ2);
	//bcm2835_irq_disable(GPIO_IRQ3);	// seems as these are not used, these may get programmed if gpio irq function is requested 
										//   other option woud be to just use interrupts ...
	bcm2835_gpio_ren_multi_set(0xffffffff);	// enable rising edge sense
	bcm2835_gpio_fen_multi_set(0xffffffff);	// enable falling edge sense	
	bcm2835_gpio_set_eds_multi(0xffffffff);
	start = -1;
	pos = 0;
	set_current_state(TASK_RUNNING);
	paddr_gplev0 = bcm2835_gpio + SHIFT4(BCM2835_GPLEV0);
	paddr_gpeds0 = bcm2835_gpio + SHIFT4(BCM2835_GPEDS0);
	paddr_stClo = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
    
	while(!kthread_should_stop()) {           	// Returns true when kthread_stop() is called
		capt = &capture_data[pos];
		spin_lock_irq(&lock);					// this greatly improves reliability
		rmb();	// Kernel barrier functions
		capt->time = ioread32(paddr_stClo);		// use just low for performance (and we shoud not reach the max of ~11h). Are timer and gpio the same device for barrier ? to reconstruct timing
		rmb();
		capt->level = ioread32(paddr_gplev0);
		rmb();
		value_eds = ioread32(paddr_gpeds0);
		rmb();
		if (value_eds != 0) {
			wmb();		
			iowrite32(value_eds, paddr_gpeds0);		// just clear set bits so we keep those set inbetween for next read
			wmb();
		}
		spin_unlock_irq(&lock);
		//printk(KERN_INFO "Capture read tm %llx lev %x eds %x mask %x pos %d start %d.\n", time, value_lev, value_eds, trigger_mask, pos, start);
		capt->edge = value_eds;			// edge
		if (++pos >= CAPTURE_VALUES) {
			pos = 0;	// wrap
		}
		//if (pos % 0x100 == 0) {
		//	printk(KERN_INFO "Capture running %x.\n", pos);
		//}
		if (start >= 0) {	// triggered capture until start is reached
			if (pos == start) {
				pos = -1;	// stop
				printk(KERN_INFO "Capture complete start %x.\n", start);
				break;
			}
		}
		else {	// waiting for trigger
			// *  TODO : timeout, and incompletly filled prebuff
			if (trigger_mask == 0) {		// trigger 0 for immediate start
				start = 0;
			}
			else if (value_eds & trigger_mask) {
				start = pos - pre_trigger;		// determine how much to keep of previous values
				if (start < 0) {
					start += CAPTURE_VALUES;
				}
				//printk(KERN_INFO "Start triggered pos %x start %x val %x.\n", pos, start, value_eds);
			}
		}
	
		//msleep(1);  // 1 -> dif 30000 = .003s?, commentd ~1MHZ
	}
	bcm2835_gpio_ren_multi_set(val_ren);
	bcm2835_gpio_fen_multi_set(val_fen);
	printk(KERN_INFO "capture: Thread has run to completion");
	task = NULL;	// prevent using destroyed strucure
	return 0;
}
#endif 
/* SPI bit order. BCM2835 SPI0 only supports MSBFIRST, so we instead
 * have a software based bit reversal, based on a contribution by Damiano Benedetti
 */
static uint8_t bcm2835_spi_bit_order = BCM2835_SPI_BIT_ORDER_MSBFIRST;
static uint8_t bcm2835_byte_reverse_table[] =
{
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
    0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
    0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
    0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
    0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
    0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
    0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
    0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
    0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
    0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
    0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
    0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
    0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
    0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
    0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
    0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
    0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};

static uint8_t bcm2835_correct_order(uint8_t b)
{
    if (bcm2835_spi_bit_order == BCM2835_SPI_BIT_ORDER_LSBFIRST)
	return bcm2835_byte_reverse_table[b];
    else
	return b;
}

/*
// Low level register access functions
*/

/* Function to return the pointers to the hardware register bases */
uint32_t* bcm2835_regbase(uint8_t regbase)
{
    switch (regbase)
    {
	case BCM2835_REGBASE_ST:
	    return (uint32_t *)bcm2835_st;
	case BCM2835_REGBASE_GPIO:
	    return (uint32_t *)bcm2835_gpio;
	case BCM2835_REGBASE_PWM:
	    return (uint32_t *)bcm2835_pwm;
	case BCM2835_REGBASE_CLK:
	    return (uint32_t *)bcm2835_clk;
	case BCM2835_REGBASE_PADS:
	    return (uint32_t *)bcm2835_pads;
	case BCM2835_REGBASE_SPI0:
	    return (uint32_t *)bcm2835_spi0;
	case BCM2835_REGBASE_BSC0:
	    return (uint32_t *)bcm2835_bsc0;
	case BCM2835_REGBASE_BSC1:
	    return (uint32_t *)bcm2835_st;
	case BCM2835_REGBASE_AUX:
	    return (uint32_t *)bcm2835_aux;
	case BCM2835_REGBASE_SPI1:
	    return (uint32_t *)bcm2835_spi1;

    }
    return (uint32_t *)MAP_FAILED;
}

void  bcm2835_set_debug(uint8_t d)
{
    debug = d;
}

unsigned int bcm2835_version(void)
{
    return BCM2835_VERSION;
}

/* Read with memory barriers from peripheral
 *
 */
uint32_t bcm2835_peri_read(volatile uint32_t* paddr)
{
    uint32_t ret;
    if (debug)
    {
#ifdef BCM2835_MODULE_BUILD
		printk(KERN_INFO "bcm2835_peri_read  paddr %p\n", (void *) paddr);
#else 
		printf("bcm2835_peri_read  paddr %p\n", (void *) paddr);
#endif 
		return 0;
    }
    else
    {
       #ifdef BCM2835_MODULE_BUILD
	   spin_lock_irq(&lock);							// this greatly improves reliability       
       rmb();	// Kernel barrier functions
       ret = ioread32(paddr);
       rmb();
	   spin_unlock_irq(&lock);
       #else
       __sync_synchronize();
       ret = *paddr;
       __sync_synchronize();
       #endif
	   //printk(KERN_INFO "bcm2835_peri_read: paddr %x %x\n", (u_int32_t)paddr - (u_int32_t)bcm2835_peripherals, ret);
       return ret;
    }
}

/* read from peripheral without the read barrier
 * This can only be used if more reads to THE SAME peripheral
 * will follow.  The sequence must terminate with memory barrier
 * before any read or write to another peripheral can occur.
 * The MB can be explicit, or one of the barrier read/write calls.
 */
uint32_t bcm2835_peri_read_nb(volatile uint32_t* paddr)
{
    if (debug)
    {
#ifdef BCM2835_MODULE_BUILD
	printk(KERN_INFO "bcm2835_peri_read_nb  paddr %p\n", paddr);
#else
	printf("bcm2835_peri_read_nb  paddr %p\n", paddr);
#endif
	return 0;
    }
    else
    {
	uint32_t ret;
	#ifdef BCM2835_MODULE_BUILD
       ret = ioread32(paddr);
    #else
		ret = *paddr;
	#endif
	//printk(KERN_INFO "bcm2835_peri_read_nb: paddr %x %x\n", (u_int32_t)paddr - (u_int32_t)bcm2835_peripherals, ret);
	return ret;
    }
}

/* Write with memory barriers to peripheral
 */

void bcm2835_peri_write(volatile uint32_t* paddr, uint32_t value)
{
    if (debug)
    {
#ifdef BCM2835_MODULE_BUILD
	printk(KERN_INFO "bcm2835_peri_write paddr %p, value %08X\n", paddr, value);
#else 
	printf("bcm2835_peri_write paddr %p, value %08X\n", paddr, value);
#endif
    }
    else
    {
		//printk(KERN_INFO "bcm2835_peri_write: paddr %x %x\n", (u_int32_t) paddr - (u_int32_t)bcm2835_peripherals, value);
        #ifdef BCM2835_MODULE_BUILD
		spin_lock_irq(&lock);			// this greatly improves reliability        
        wmb();	// Kernel barrier functions
		iowrite32(value, paddr);
        wmb();
		spin_unlock_irq(&lock);
		#else
        __sync_synchronize();
        *paddr = value;
        __sync_synchronize();
        #endif
    }
}

/* write to peripheral without the write barrier */
void bcm2835_peri_write_nb(volatile uint32_t* paddr, uint32_t value)
{
    if (debug)
    {
#ifdef BCM2835_MODULE_BUILD        
	printk(KERN_INFO "bcm2835_peri_write_nb paddr %p, value %08X\n",
                paddr, value);
#else 
	printf("bcm2835_peri_write_nb paddr %p, value %08X\n",
                paddr, value);
#endif 
    }
    else
    {
		//printk(KERN_INFO "bcm2835_peri_write_nb: paddr %x %x\n", (u_int32_t) paddr  - (u_int32_t)bcm2835_peripherals, value);
        #ifdef BCM2835_MODULE_BUILD
		iowrite32(value, paddr);
		#else
		*paddr = value;
		#endif
    }
}

/* Set/clear only the bits in value covered by the mask
 * This is not atomic - can be interrupted.
 */
void bcm2835_peri_set_bits(volatile uint32_t* paddr, uint32_t value, uint32_t mask)
{
    uint32_t v = bcm2835_peri_read(paddr);
    v = (v & ~mask) | (value & mask);
    bcm2835_peri_write(paddr, v);
}

/*
// Low level convenience functions
*/

/* Function select
// pin is a BCM2835 GPIO pin number NOT RPi pin number
//      There are 6 control registers, each control the functions of a block
//      of 10 pins.
//      Each control register has 10 sets of 3 bits per GPIO pin:
//
//      000 = GPIO Pin X is an input
//      001 = GPIO Pin X is an output
//      100 = GPIO Pin X takes alternate function 0
//      101 = GPIO Pin X takes alternate function 1
//      110 = GPIO Pin X takes alternate function 2
//      111 = GPIO Pin X takes alternate function 3
//      011 = GPIO Pin X takes alternate function 4
//      010 = GPIO Pin X takes alternate function 5
//
// So the 3 bits for port X are:
//      X / 10 + ((X % 10) * 3)
*/
void bcm2835_gpio_fsel(uint8_t pin, uint8_t mode)
{
    /* Function selects are 10 pins per 32 bit word, 3 bits per pin */
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT10(BCM2835_GPFSEL0, pin);
    uint8_t   shift = (pin % 10) * 3;
    uint32_t  mask = BCM2835_GPIO_FSEL_MASK << shift;
    uint32_t  value = mode << shift;
    bcm2835_peri_set_bits(paddr, value, mask);
}
/* Set output pin */
void bcm2835_gpio_set(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPSET0, pin);
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, 1 << shift);
}

/* Clear output pin */
void bcm2835_gpio_clr(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPCLR0, pin);
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, 1 << shift);
}

/* Set all output pins in the mask */
void bcm2835_gpio_set_multi(uint32_t mask)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPSET0);
    bcm2835_peri_write(paddr, mask);
}

/* Clear all output pins in the mask */
void bcm2835_gpio_clr_multi(uint32_t mask)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPCLR0);
    bcm2835_peri_write(paddr, mask);
}

/* Read input pin */
uint8_t bcm2835_gpio_lev(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPLEV0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = bcm2835_peri_read(paddr);
    return (value & (1 << shift)) ? HIGH : LOW;
}

/* See if an event detection bit is set
// Sigh cant support interrupts yet
*/
uint8_t bcm2835_gpio_eds(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPEDS0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = bcm2835_peri_read(paddr);
    return (value & (1 << shift)) ? HIGH : LOW;
}

uint32_t bcm2835_gpio_eds_multi(uint32_t mask)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPEDS0);
    uint32_t value = bcm2835_peri_read(paddr);
    return (value & mask);
}

/* Write a 1 to clear the bit in EDS */
void bcm2835_gpio_set_eds(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPEDS0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_write(paddr, value);
}

void bcm2835_gpio_set_eds_multi(uint32_t mask)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPEDS0);
    bcm2835_peri_write(paddr, mask);
}

/* Rising edge detect enable */
void bcm2835_gpio_ren(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPREN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_ren(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPREN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}
void bcm2835_gpio_ren_multi(uint32_t value)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPREN0);
    value |= bcm2835_peri_read(paddr);
    bcm2835_peri_write(paddr, value);
}
void bcm2835_gpio_ren_multi_set(uint32_t value)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPREN0);
    bcm2835_peri_write(paddr, value);
}
uint32_t bcm2835_gpio_ren_multi_get() {
	volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPREN0);
    return bcm2835_peri_read(paddr);
}

/* Falling edge detect enable */
void bcm2835_gpio_fen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPFEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_fen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPFEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}
void bcm2835_gpio_fen_multi(uint32_t value)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPFEN0);
    value |= bcm2835_peri_read(paddr);
    bcm2835_peri_write(paddr, value);
}
void bcm2835_gpio_fen_multi_set(uint32_t value)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPFEN0);
    bcm2835_peri_write(paddr, value);
}
uint32_t bcm2835_gpio_fen_multi_get() {
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPFEN0);
    return bcm2835_peri_read(paddr);
}

/* High detect enable */
void bcm2835_gpio_hen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPHEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_hen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPHEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

/* Low detect enable */
void bcm2835_gpio_len(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPLEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_len(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPLEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

/* Async rising edge detect enable */
void bcm2835_gpio_aren(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPAREN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_aren(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPAREN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

/* Async falling edge detect enable */
void bcm2835_gpio_afen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPAFEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_afen(uint8_t pin)
{
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPAFEN0, pin);
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

/* Set pullup/down */
void bcm2835_gpio_pud(uint8_t pud)
{
    if( pud_type_rpi4 )
    {
        pud_compat_setting = pud;
    }
    else {
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT4(BCM2835_GPPUD);
    bcm2835_peri_write(paddr, pud);
}
}

/* Pullup/down clock
// Clocks the value of pud into the GPIO pin
*/
void bcm2835_gpio_pudclk(uint8_t pin, uint8_t on)
{
    if( pud_type_rpi4 )
    {
        if( on )
            bcm2835_gpio_set_pud( pin, pud_compat_setting);
    }
    else
    {
    volatile uint32_t* paddr = bcm2835_gpio + SHIFT32(BCM2835_GPPUDCLK0, pin);
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, (on ? 1 : 0) << shift);
}
}

/* Read GPIO pad behaviour for groups of GPIOs */
uint32_t bcm2835_gpio_pad(uint8_t group)
{
  volatile uint32_t* paddr;
  if (bcm2835_pads == MAP_FAILED)
    return 0;

  paddr = bcm2835_pads + SHIFT4(BCM2835_PADS_GPIO_0_27) + group;
  return bcm2835_peri_read(paddr);
}

/* Set GPIO pad behaviour for groups of GPIOs
// powerup value for all pads is
// BCM2835_PAD_SLEW_RATE_UNLIMITED | BCM2835_PAD_HYSTERESIS_ENABLED | BCM2835_PAD_DRIVE_8mA
*/
void bcm2835_gpio_set_pad(uint8_t group, uint32_t control)
{
  volatile uint32_t* paddr;
  if (bcm2835_pads == MAP_FAILED)
    return;

  paddr = bcm2835_pads + SHIFT4(BCM2835_PADS_GPIO_0_27) + group;
  bcm2835_peri_write(paddr, control | BCM2835_PAD_PASSWRD);
}

/* Some convenient arduino-like functions
// milliseconds
*/
void bcm2835_delay(unsigned int millis)
{
	// repimplement by hrtimer needed for pmw,i2c
	#ifdef BCM2835_MODULE_BUILD
    msleep(millis);
	//compat_sys_nanosleep(&rqtp,NULL);
	#else 
    struct timespec sleeper;

    sleeper.tv_sec  = (time_t)(millis / 1000);
    sleeper.tv_nsec = (long)(millis % 1000) * 1000000;
    nanosleep(&sleeper, NULL);
    #endif 
}

/* microseconds */
void bcm2835_delayMicroseconds(uint64_t micros)
{
	// repimplement by hrtimer needed for pmw,i2c
    struct timespec t1;
    uint64_t        start;

    if (debug)
    {
	/* Cant access sytem timers in debug mode */
        #ifdef BCM2835_MODULE_BUILD        
	printk(KERN_INFO "bcm2835_delayMicroseconds %lld\n", (long long int) micros);
#else 
	printf("bcm2835_delayMicroseconds %lld\n", (long long int) micros);
#endif 
	return;
    }

	#ifdef BCM2835_MODULE_BUILD
    usleep_range(micros, micros);
	#else 
    /* Calling nanosleep() takes at least 100-200 us, so use it for
    // long waits and use a busy wait on the System Timer for the rest.
    */
    start =  bcm2835_st_read();

    /* Not allowed to access timer registers (result is not as precise)*/
    if (start==0)
    {
	t1.tv_sec = 0;
	t1.tv_nsec = 1000 * (long)(micros);
	nanosleep(&t1, NULL);
	return;
    }

    if (micros > 450)
    {
	t1.tv_sec = 0;
	t1.tv_nsec = 1000 * (long)(micros - 200);
	nanosleep(&t1, NULL);
    }

    bcm2835_st_delay(start, micros);
    #endif
}

/*
// Higher level convenience functions
*/

/* Set the state of an output */
void bcm2835_gpio_write(uint8_t pin, uint8_t on)
{
    if (on)
	bcm2835_gpio_set(pin);
    else
	bcm2835_gpio_clr(pin);
}

/* Set the state of a all 32 outputs in the mask to on or off */
void bcm2835_gpio_write_multi(uint32_t mask, uint8_t on)
{
    if (on)
	bcm2835_gpio_set_multi(mask);
    else
	bcm2835_gpio_clr_multi(mask);
}

/* Set the state of a all 32 outputs in the mask to the values in value */
void bcm2835_gpio_write_mask(uint32_t value, uint32_t mask)
{
    bcm2835_gpio_set_multi(value & mask);
    bcm2835_gpio_clr_multi((~value) & mask);
}

/* Set the pullup/down resistor for a pin
//
// The GPIO Pull-up/down Clock Registers control the actuation of internal pull-downs on
// the respective GPIO pins. These registers must be used in conjunction with the GPPUD
// register to effect GPIO Pull-up/down changes. The following sequence of events is
// required:
// 1. Write to GPPUD to set the required control signal (i.e. Pull-up or Pull-Down or neither
// to remove the current Pull-up/down)
// 2. Wait 150 cycles ? this provides the required set-up time for the control signal
// 3. Write to GPPUDCLK0/1 to clock the control signal into the GPIO pads you wish to
// modify ? NOTE only the pads which receive a clock will be modified, all others will
// retain their previous state.
// 4. Wait 150 cycles ? this provides the required hold time for the control signal
// 5. Write to GPPUD to remove the control signal
// 6. Write to GPPUDCLK0/1 to remove the clock
//
// RPi has P1-03 and P1-05 with 1k8 pullup resistor
//
// RPI 4 uses a different PUD method - no clock

*/
void bcm2835_gpio_set_pud(uint8_t pin, uint8_t pud)
{
    volatile uint32_t* paddr;
	uint32_t bits;
	uint32_t pull;
    if( pud_type_rpi4 )
    {
        int shiftbits = (pin & 0xf) << 1;

        switch (pud)
        {
           case BCM2835_GPIO_PUD_OFF:  pull = 0; break;
           case BCM2835_GPIO_PUD_UP:   pull = 1; break;
           case BCM2835_GPIO_PUD_DOWN: pull = 2; break;
           default: return;
        }

        paddr = bcm2835_gpio + SHIFT16(BCM2835_GPPUPPDN0, pin);

        bits = bcm2835_peri_read_nb( paddr );
        bits &= ~(3 << shiftbits);
        bits |= (pull << shiftbits);

        bcm2835_peri_write_nb( paddr, bits );

    } else
    {
    bcm2835_gpio_pud(pud);
    delayMicroseconds(10);
    bcm2835_gpio_pudclk(pin, 1);
    delayMicroseconds(10);
    bcm2835_gpio_pud(BCM2835_GPIO_PUD_OFF);
    bcm2835_gpio_pudclk(pin, 0);
}

}


uint8_t bcm2835_gpio_get_pud(uint8_t pin)
{
    uint32_t bits;
    uint8_t ret = BCM2835_GPIO_PUD_ERROR;

    if( pud_type_rpi4 )
    {
        volatile uint32_t* paddr = bcm2835_gpio + SHIFT16(BCM2835_GPPUPPDN0, pin);
        bits = (bcm2835_peri_read_nb( paddr ) >> ((pin & 0xf)<<1)) & 0x3;

        switch (bits)
        {
            case 0: ret = BCM2835_GPIO_PUD_OFF; break;
            case 1: ret = BCM2835_GPIO_PUD_UP; break;
            case 2: ret = BCM2835_GPIO_PUD_DOWN; break;
            default: ret = BCM2835_GPIO_PUD_ERROR;
        }
    }

    return ret;
}


int bcm2835_spi_begin(void)
{
    volatile uint32_t* paddr;

    if (bcm2835_spi0 == MAP_FAILED)
      return 0; /* bcm2835_init() failed, or not root */

    /* Set the SPI0 pins to the Alt 0 function to enable SPI0 access on them */
    bcm2835_gpio_fsel(RPI_GPIO_P1_26, BCM2835_GPIO_FSEL_ALT0); /* CE1 */
    bcm2835_gpio_fsel(RPI_GPIO_P1_24, BCM2835_GPIO_FSEL_ALT0); /* CE0 */
    bcm2835_gpio_fsel(RPI_GPIO_P1_21, BCM2835_GPIO_FSEL_ALT0); /* MISO */
    bcm2835_gpio_fsel(RPI_GPIO_P1_19, BCM2835_GPIO_FSEL_ALT0); /* MOSI */
    bcm2835_gpio_fsel(RPI_GPIO_P1_23, BCM2835_GPIO_FSEL_ALT0); /* CLK */

    /* Set the SPI CS register to the some sensible defaults */
    paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    bcm2835_peri_write(paddr, 0); /* All 0s */

    /* Clear TX and RX fifos */
    bcm2835_peri_write_nb(paddr, BCM2835_SPI0_CS_CLEAR);

    return 1; // OK
}

void bcm2835_spi_end(void)
{
    /* Set all the SPI0 pins back to input */
    bcm2835_gpio_fsel(RPI_GPIO_P1_26, BCM2835_GPIO_FSEL_INPT); /* CE1 */
    bcm2835_gpio_fsel(RPI_GPIO_P1_24, BCM2835_GPIO_FSEL_INPT); /* CE0 */
    bcm2835_gpio_fsel(RPI_GPIO_P1_21, BCM2835_GPIO_FSEL_INPT); /* MISO */
    bcm2835_gpio_fsel(RPI_GPIO_P1_19, BCM2835_GPIO_FSEL_INPT); /* MOSI */
    bcm2835_gpio_fsel(RPI_GPIO_P1_23, BCM2835_GPIO_FSEL_INPT); /* CLK */
}

void bcm2835_spi_setBitOrder(uint8_t order)
{
    bcm2835_spi_bit_order = order;
}

/* defaults to 0, which means a divider of 65536.
// The divisor must be a power of 2. Odd numbers
// rounded down. The maximum SPI clock rate is
// of the APB clock
*/
void bcm2835_spi_setClockDivider(uint16_t divider)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CLK);
    bcm2835_peri_write(paddr, divider);
}

void bcm2835_spi_set_speed_hz(uint32_t speed_hz)
{
	uint16_t divider = (uint16_t) ((uint32_t) BCM2835_CORE_CLK_HZ / speed_hz);
	divider &= 0xFFFE;
	bcm2835_spi_setClockDivider(divider);
}

void bcm2835_spi_setDataMode(uint8_t mode)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    /* Mask in the CPO and CPHA bits of CS */
    bcm2835_peri_set_bits(paddr, mode << 2, BCM2835_SPI0_CS_CPOL | BCM2835_SPI0_CS_CPHA);
}

/* Writes (and reads) a single byte to SPI */
uint8_t bcm2835_spi_transfer(uint8_t value)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    volatile uint32_t* fifo = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_FIFO);
    uint32_t ret;

    /* This is Polled transfer as per section 10.6.1
    // BUG ALERT: what happens if we get interupted in this section, and someone else
    // accesses a different peripheral?
    // Clear TX and RX fifos
    */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_CLEAR, BCM2835_SPI0_CS_CLEAR);

    /* Set TA = 1 */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_TA, BCM2835_SPI0_CS_TA);

    /* Maybe wait for TXD */
    while (!(bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_TXD))
	;

    /* Write to FIFO, no barrier */
    bcm2835_peri_write_nb(fifo, bcm2835_correct_order(value));

    /* Wait for DONE to be set */
    while (!(bcm2835_peri_read_nb(paddr) & BCM2835_SPI0_CS_DONE))
	;

    /* Read any byte that was sent back by the slave while we sere sending to it */
    ret = bcm2835_correct_order(bcm2835_peri_read_nb(fifo));

    /* Set TA = 0, and also set the barrier */
    bcm2835_peri_set_bits(paddr, 0, BCM2835_SPI0_CS_TA);

    return ret;
}

/* Writes a single byte to SPI use for standalone test */
void bcm2835_spi_transfer_nowait(uint8_t value)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    volatile uint32_t* fifo = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_FIFO);
    uint32_t ret;

    /* This is Polled transfer as per section 10.6.1
    // BUG ALERT: what happens if we get interupted in this section, and someone else
    // accesses a different peripheral?
    // Clear TX and RX fifos
    */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_CLEAR, BCM2835_SPI0_CS_CLEAR);

    /* Set TA = 1 */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_TA, BCM2835_SPI0_CS_TA);
    
    /* Write to FIFO, no barrier */
    bcm2835_peri_write_nb(fifo, bcm2835_correct_order(value));
    
}

/* Writes (and reads) an number of bytes to SPI */
void bcm2835_spi_transfernb(char* tbuf, char* rbuf, uint32_t len)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    volatile uint32_t* fifo = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_FIFO);
    uint32_t TXCnt=0;
    uint32_t RXCnt=0;

    /* This is Polled transfer as per section 10.6.1
    // BUG ALERT: what happens if we get interupted in this section, and someone else
    // accesses a different peripheral?
    */

    /* Clear TX and RX fifos */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_CLEAR, BCM2835_SPI0_CS_CLEAR);

    /* Set TA = 1 */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_TA, BCM2835_SPI0_CS_TA);

    /* Use the FIFO's to reduce the interbyte times */
    while((TXCnt < len)||(RXCnt < len))
    {
        /* TX fifo not full, so add some more bytes */
        while(((bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_TXD))&&(TXCnt < len ))
        {
	    bcm2835_peri_write_nb(fifo, bcm2835_correct_order(tbuf[TXCnt]));
	    TXCnt++;
        }
        /* Rx fifo not empty, so get the next received bytes */
        while(((bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_RXD))&&( RXCnt < len ))
        {
	    rbuf[RXCnt] = bcm2835_correct_order(bcm2835_peri_read_nb(fifo));
	    RXCnt++;
        }
    }
    /* Wait for DONE to be set */
    while (!(bcm2835_peri_read_nb(paddr) & BCM2835_SPI0_CS_DONE))
	;

    /* Set TA = 0, and also set the barrier */
    bcm2835_peri_set_bits(paddr, 0, BCM2835_SPI0_CS_TA);
}

/* Writes an number of bytes to SPI */
void bcm2835_spi_writenb(const char* tbuf, uint32_t len)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    volatile uint32_t* fifo = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_FIFO);
    uint32_t i;

    /* This is Polled transfer as per section 10.6.1
    // BUG ALERT: what happens if we get interupted in this section, and someone else
    // accesses a different peripheral?
    // Answer: an ISR is required to issue the required memory barriers.
    */

    /* Clear TX and RX fifos */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_CLEAR, BCM2835_SPI0_CS_CLEAR);

    /* Set TA = 1 */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_TA, BCM2835_SPI0_CS_TA);

    for (i = 0; i < len; i++)
    {
	/* Maybe wait for TXD */
	while (!(bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_TXD))
	    ;

	/* Write to FIFO, no barrier */
	bcm2835_peri_write_nb(fifo, bcm2835_correct_order(tbuf[i]));

	/* Read from FIFO to prevent stalling */
	while (bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_RXD)
	    (void) bcm2835_peri_read_nb(fifo);
    }

    /* Wait for DONE to be set */
    while (!(bcm2835_peri_read_nb(paddr) & BCM2835_SPI0_CS_DONE)) {
	while (bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_RXD)
		(void) bcm2835_peri_read_nb(fifo);
    };

    /* Set TA = 0, and also set the barrier */
    bcm2835_peri_set_bits(paddr, 0, BCM2835_SPI0_CS_TA);
}

/* Writes (and reads) an number of bytes to SPI
// Read bytes are copied over onto the transmit buffer
*/
void bcm2835_spi_transfern(char* buf, uint32_t len)
{
    bcm2835_spi_transfernb(buf, buf, len);
}

void bcm2835_spi_chipSelect(uint8_t cs)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    /* Mask in the CS bits of CS */
    bcm2835_peri_set_bits(paddr, cs, BCM2835_SPI0_CS_CS);
}

void bcm2835_spi_setChipSelectPolarity(uint8_t cs, uint8_t active)
{
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    uint8_t shift = 21 + cs;
    /* Mask in the appropriate CSPOLn bit */
    bcm2835_peri_set_bits(paddr, active << shift, 1 << shift);
}

void bcm2835_spi_write(uint16_t data)
{
#if 0
	char buf[2];

	buf[0] = data >> 8;
	buf[1] = data & 0xFF;

	bcm2835_spi_transfern(buf, 2);
#else
    volatile uint32_t* paddr = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_CS);
    volatile uint32_t* fifo = bcm2835_spi0 + SHIFT4(BCM2835_SPI0_FIFO);

    /* Clear TX and RX fifos */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_CLEAR, BCM2835_SPI0_CS_CLEAR);

    /* Set TA = 1 */
    bcm2835_peri_set_bits(paddr, BCM2835_SPI0_CS_TA, BCM2835_SPI0_CS_TA);

	/* Maybe wait for TXD */
	while (!(bcm2835_peri_read(paddr) & BCM2835_SPI0_CS_TXD))
	    ;

	/* Write to FIFO */
	bcm2835_peri_write_nb(fifo,  (uint32_t) data >> 8);
	bcm2835_peri_write_nb(fifo,  data & 0xFF);


    /* Wait for DONE to be set */
    while (!(bcm2835_peri_read_nb(paddr) & BCM2835_SPI0_CS_DONE))
	;

    /* Set TA = 0, and also set the barrier */
    bcm2835_peri_set_bits(paddr, 0, BCM2835_SPI0_CS_TA);
#endif
}

int bcm2835_aux_spi_begin(void)
{
    volatile uint32_t* enable = bcm2835_aux + SHIFT4(BCM2835_AUX_ENABLE);
    volatile uint32_t* cntl0 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL0);
    volatile uint32_t* cntl1 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL1);

    if (bcm2835_spi1 == MAP_FAILED)
      return 0; /* bcm2835_init() failed, or not root */

    /* Set the SPI pins to the Alt 4 function to enable SPI1 access on them */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_36, BCM2835_GPIO_FSEL_ALT4);	/* SPI1_CE2_N */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_35, BCM2835_GPIO_FSEL_ALT4);	/* SPI1_MISO */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_38, BCM2835_GPIO_FSEL_ALT4);	/* SPI1_MOSI */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_40, BCM2835_GPIO_FSEL_ALT4);	/* SPI1_SCLK */

	bcm2835_aux_spi_setClockDivider(bcm2835_aux_spi_CalcClockDivider(1000000));	// Default 1MHz SPI

	bcm2835_peri_write(enable, BCM2835_AUX_ENABLE_SPI0);
	bcm2835_peri_write(cntl1, 0);
	bcm2835_peri_write(cntl0, BCM2835_AUX_SPI_CNTL0_CLEARFIFO);

    return 1; /* OK */
}

void bcm2835_aux_spi_end(void)
{
	/* Set all the SPI1 pins back to input */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_36, BCM2835_GPIO_FSEL_INPT);	/* SPI1_CE2_N */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_35, BCM2835_GPIO_FSEL_INPT);	/* SPI1_MISO */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_38, BCM2835_GPIO_FSEL_INPT);	/* SPI1_MOSI */
	bcm2835_gpio_fsel(RPI_V2_GPIO_P1_40, BCM2835_GPIO_FSEL_INPT);	/* SPI1_SCLK */
}

#ifdef BCM2835_MODULE_BUILD
#else
#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#endif

uint16_t bcm2835_aux_spi_CalcClockDivider(uint32_t speed_hz)
{
	uint16_t divider;

	if (speed_hz < (uint32_t) BCM2835_AUX_SPI_CLOCK_MIN) {
		speed_hz = (uint32_t) BCM2835_AUX_SPI_CLOCK_MIN;
	} else if (speed_hz > (uint32_t) BCM2835_AUX_SPI_CLOCK_MAX) {
		speed_hz = (uint32_t) BCM2835_AUX_SPI_CLOCK_MAX;
	}

	divider = (uint16_t) DIV_ROUND_UP(BCM2835_CORE_CLK_HZ, 2 * speed_hz) - 1;

	if (divider > (uint16_t) BCM2835_AUX_SPI_CNTL0_SPEED_MAX) {
		return (uint16_t) BCM2835_AUX_SPI_CNTL0_SPEED_MAX;
	}

	return divider;
}

static uint32_t spi1_speed;

void bcm2835_aux_spi_setClockDivider(uint16_t divider)
{
		spi1_speed = (uint32_t) divider;
}

void bcm2835_aux_spi_write(uint16_t data)
{
    volatile uint32_t* cntl0 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL0);
    volatile uint32_t* cntl1 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL1);
    volatile uint32_t* stat = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_STAT);
    volatile uint32_t* io = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_IO);

	uint32_t _cntl0 = (spi1_speed << BCM2835_AUX_SPI_CNTL0_SPEED_SHIFT);
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_CS2_N;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_ENABLE;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_MSBF_OUT;
	_cntl0 |= 16; // Shift length

	bcm2835_peri_write(cntl0, _cntl0);
	bcm2835_peri_write(cntl1, BCM2835_AUX_SPI_CNTL1_MSBF_IN);

	while (bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_TX_FULL)
		;

	bcm2835_peri_write(io, (uint32_t) data << 16);
}

void bcm2835_aux_spi_writenb(const char *tbuf, uint32_t len) {
    volatile uint32_t* cntl0 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL0);
    volatile uint32_t* cntl1 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL1);
    volatile uint32_t* stat = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_STAT);
    volatile uint32_t* txhold = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_TXHOLD);
    volatile uint32_t* io = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_IO);

	char *tx = (char *) tbuf;
	uint32_t tx_len = len;
	uint32_t count;
	uint32_t data;
	uint32_t i;
	uint8_t byte;

	uint32_t _cntl0 = (spi1_speed << BCM2835_AUX_SPI_CNTL0_SPEED_SHIFT);
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_CS2_N;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_ENABLE;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_MSBF_OUT;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_VAR_WIDTH;

	bcm2835_peri_write(cntl0, _cntl0);
	bcm2835_peri_write(cntl1, BCM2835_AUX_SPI_CNTL1_MSBF_IN);

	while (tx_len > 0) {

		while (bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_TX_FULL)
			;

		count = MIN(tx_len, 3);
		data = 0;

		for (i = 0; i < count; i++) {
			byte = (tx != NULL) ? (uint8_t) *tx++ : (uint8_t) 0;
			data |= byte << (8 * (2 - i));
		}

		data |= (count * 8) << 24;
		tx_len -= count;

		if (tx_len != 0) {
			bcm2835_peri_write(txhold, data);
		} else {
			bcm2835_peri_write(io, data);
		}

		while (bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_BUSY)
			;

		(void) bcm2835_peri_read(io);
	}
}

void bcm2835_aux_spi_transfernb(const char *tbuf, char *rbuf, uint32_t len) {
    volatile uint32_t* cntl0 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL0);
    volatile uint32_t* cntl1 = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_CNTL1);
    volatile uint32_t* stat = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_STAT);
    volatile uint32_t* txhold = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_TXHOLD);
    volatile uint32_t* io = bcm2835_spi1 + SHIFT4(BCM2835_AUX_SPI_IO);

	char *tx = (char *)tbuf;
	char *rx = (char *)rbuf;
	uint32_t tx_len = len;
	uint32_t rx_len = len;
	uint32_t count;
	uint32_t data;
	uint32_t i;
	uint8_t byte;

	uint32_t _cntl0 = (spi1_speed << BCM2835_AUX_SPI_CNTL0_SPEED_SHIFT);
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_CS2_N;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_ENABLE;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_MSBF_OUT;
	_cntl0 |= BCM2835_AUX_SPI_CNTL0_VAR_WIDTH;

	bcm2835_peri_write(cntl0, _cntl0);
	bcm2835_peri_write(cntl1, BCM2835_AUX_SPI_CNTL1_MSBF_IN);

	while ((tx_len > 0) || (rx_len > 0)) {

		while (!(bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_TX_FULL) && (tx_len > 0)) {
			count = MIN(tx_len, 3);
			data = 0;

			for (i = 0; i < count; i++) {
				byte = (tx != NULL) ? (uint8_t) *tx++ : (uint8_t) 0;
				data |= byte << (8 * (2 - i));
			}

			data |= (count * 8) << 24;
			tx_len -= count;

			if (tx_len != 0) {
				bcm2835_peri_write(txhold, data);
			} else {
				bcm2835_peri_write(io, data);
			}

		}

		while (!(bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_RX_EMPTY) && (rx_len > 0)) {
			count = MIN(rx_len, 3);
			data = bcm2835_peri_read(io);

			if (rbuf != NULL) {
				switch (count) {
				case 3:
					*rx++ = (char)((data >> 16) & 0xFF);
					/*@fallthrough@*/
					/* no break */
				case 2:
					*rx++ = (char)((data >> 8) & 0xFF);
					/*@fallthrough@*/
					/* no break */
				case 1:
					*rx++ = (char)((data >> 0) & 0xFF);
				}
			}

			rx_len -= count;
		}

		while (!(bcm2835_peri_read(stat) & BCM2835_AUX_SPI_STAT_BUSY) && (rx_len > 0)) {
			count = MIN(rx_len, 3);
			data = bcm2835_peri_read(io);

			if (rbuf != NULL) {
				switch (count) {
				case 3:
					*rx++ = (char)((data >> 16) & 0xFF);
					/*@fallthrough@*/
					/* no break */
				case 2:
					*rx++ = (char)((data >> 8) & 0xFF);
					/*@fallthrough@*/
					/* no break */
				case 1:
					*rx++ = (char)((data >> 0) & 0xFF);
				}
			}

			rx_len -= count;
		}
	}
}

void bcm2835_aux_spi_transfern(char *buf, uint32_t len) {
	bcm2835_aux_spi_transfernb(buf, buf, len);
}

int bcm2835_i2c_begin(void)
{
    uint16_t cdiv;
    volatile uint32_t* paddr;

    if (   bcm2835_bsc0 == MAP_FAILED
	|| bcm2835_bsc1 == MAP_FAILED)
      return 0; /* bcm2835_init() failed, or not root */

#ifdef I2C_V1
    paddr = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DIV);
    /* Set the I2C/BSC0 pins to the Alt 0 function to enable I2C access on them */
    bcm2835_gpio_fsel(RPI_GPIO_P1_03, BCM2835_GPIO_FSEL_ALT0); /* SDA */
    bcm2835_gpio_fsel(RPI_GPIO_P1_05, BCM2835_GPIO_FSEL_ALT0); /* SCL */
#else
    paddr = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DIV);
    /* Set the I2C/BSC1 pins to the Alt 0 function to enable I2C access on them */
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_03, BCM2835_GPIO_FSEL_ALT0); /* SDA */
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_05, BCM2835_GPIO_FSEL_ALT0); /* SCL */
#endif

    /* Read the clock divider register */
    cdiv = bcm2835_peri_read(paddr);
    /* Calculate time for transmitting one byte
    // 1000000 = micros seconds in a second
    // 9 = Clocks per byte : 8 bits + ACK
    */
    i2c_byte_wait_us = 10; // @@@ remove lib fkt ((float)cdiv / BCM2835_CORE_CLK_HZ) * 1000000 * 9;

    return 1;
}

void bcm2835_i2c_end(void)
{
#ifdef I2C_V1
    /* Set all the I2C/BSC0 pins back to input */
    bcm2835_gpio_fsel(RPI_GPIO_P1_03, BCM2835_GPIO_FSEL_INPT); /* SDA */
    bcm2835_gpio_fsel(RPI_GPIO_P1_05, BCM2835_GPIO_FSEL_INPT); /* SCL */
#else
    /* Set all the I2C/BSC1 pins back to input */
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_03, BCM2835_GPIO_FSEL_INPT); /* SDA */
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_05, BCM2835_GPIO_FSEL_INPT); /* SCL */
#endif
}

void bcm2835_i2c_setSlaveAddress(uint8_t addr)
{
    /* Set I2C Device Address */
#ifdef I2C_V1
    volatile uint32_t* paddr = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_A);
#else
    volatile uint32_t* paddr = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_A);
#endif
    bcm2835_peri_write(paddr, addr);
}

/* defaults to 0x5dc, should result in a 166.666 kHz I2C clock frequency.
// The divisor must be a power of 2. Odd numbers
// rounded down.
*/
void bcm2835_i2c_setClockDivider(uint16_t divider)
{
#ifdef I2C_V1
    volatile uint32_t* paddr = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DIV);
#else
    volatile uint32_t* paddr = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DIV);
#endif
    bcm2835_peri_write(paddr, divider);
    /* Calculate time for transmitting one byte
    // 1000000 = micros seconds in a second
    // 9 = Clocks per byte : 8 bits + ACK
    */
    i2c_byte_wait_us = 10; // @@@ remove lib fkt ((float)divider / BCM2835_CORE_CLK_HZ) * 1000000 * 9;
}

/* set I2C clock divider by means of a baudrate number */
void bcm2835_i2c_set_baudrate(uint32_t baudrate)
{
	uint32_t divider;
	/* use 0xFFFE mask to limit a max value and round down any odd number */
	divider = (BCM2835_CORE_CLK_HZ / baudrate) & 0xFFFE;
	bcm2835_i2c_setClockDivider( (uint16_t)divider );
}

/* Writes an number of bytes to I2C */
uint8_t bcm2835_i2c_write(const char * buf, uint32_t len)
{
#ifdef I2C_V1
    volatile uint32_t* dlen    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_C);
#else
    volatile uint32_t* dlen    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_C);
#endif

    uint32_t remaining = len;
    uint32_t i = 0;
    uint8_t reason = BCM2835_I2C_REASON_OK;

    /* Clear FIFO */
    bcm2835_peri_set_bits(control, BCM2835_BSC_C_CLEAR_1 , BCM2835_BSC_C_CLEAR_1 );
    /* Clear Status */
    bcm2835_peri_write(status, BCM2835_BSC_S_CLKT | BCM2835_BSC_S_ERR | BCM2835_BSC_S_DONE);
    /* Set Data Length */
    bcm2835_peri_write(dlen, len);
    /* pre populate FIFO with max buffer */
    while( remaining && ( i < BCM2835_BSC_FIFO_SIZE ) )
    {
        bcm2835_peri_write_nb(fifo, buf[i]);
        i++;
        remaining--;
    }

    /* Enable device and start transfer */
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST);

    /* Transfer is over when BCM2835_BSC_S_DONE */
    while(!(bcm2835_peri_read(status) & BCM2835_BSC_S_DONE ))
    {
        while ( remaining && (bcm2835_peri_read(status) & BCM2835_BSC_S_TXD ))
    	{
	    /* Write to FIFO */
	    bcm2835_peri_write(fifo, buf[i]);
	    i++;
	    remaining--;
    	}
    }

    /* Received a NACK */
    if (bcm2835_peri_read(status) & BCM2835_BSC_S_ERR)
    {
	reason = BCM2835_I2C_REASON_ERROR_NACK;
    }

    /* Received Clock Stretch Timeout */
    else if (bcm2835_peri_read(status) & BCM2835_BSC_S_CLKT)
    {
	reason = BCM2835_I2C_REASON_ERROR_CLKT;
    }

    /* Not all data is sent */
    else if (remaining)
    {
	reason = BCM2835_I2C_REASON_ERROR_DATA;
    }

    bcm2835_peri_set_bits(control, BCM2835_BSC_S_DONE , BCM2835_BSC_S_DONE);

    return reason;
}

/* Read an number of bytes from I2C */
uint8_t bcm2835_i2c_read(char* buf, uint32_t len)
{
#ifdef I2C_V1
    volatile uint32_t* dlen    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_C);
#else
    volatile uint32_t* dlen    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_C);
#endif

    uint32_t remaining = len;
    uint32_t i = 0;
    uint8_t reason = BCM2835_I2C_REASON_OK;

    /* Clear FIFO */
    bcm2835_peri_set_bits(control, BCM2835_BSC_C_CLEAR_1 , BCM2835_BSC_C_CLEAR_1 );
    /* Clear Status */
    bcm2835_peri_write_nb(status, BCM2835_BSC_S_CLKT | BCM2835_BSC_S_ERR | BCM2835_BSC_S_DONE);
    /* Set Data Length */
    bcm2835_peri_write_nb(dlen, len);
    /* Start read */
    bcm2835_peri_write_nb(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST | BCM2835_BSC_C_READ);

    /* wait for transfer to complete */
    while (!(bcm2835_peri_read_nb(status) & BCM2835_BSC_S_DONE))
    {
        /* we must empty the FIFO as it is populated and not use any delay */
        while (remaining && bcm2835_peri_read_nb(status) & BCM2835_BSC_S_RXD)
    	{
	    /* Read from FIFO, no barrier */
	    buf[i] = bcm2835_peri_read_nb(fifo);
	    i++;
	    remaining--;
    	}
    }

    /* transfer has finished - grab any remaining stuff in FIFO */
    while (remaining && (bcm2835_peri_read_nb(status) & BCM2835_BSC_S_RXD))
    {
        /* Read from FIFO, no barrier */
        buf[i] = bcm2835_peri_read_nb(fifo);
        i++;
        remaining--;
    }

    /* Received a NACK */
    if (bcm2835_peri_read(status) & BCM2835_BSC_S_ERR)
    {
	reason = BCM2835_I2C_REASON_ERROR_NACK;
    }

    /* Received Clock Stretch Timeout */
    else if (bcm2835_peri_read(status) & BCM2835_BSC_S_CLKT)
    {
	reason = BCM2835_I2C_REASON_ERROR_CLKT;
    }

    /* Not all data is received */
    else if (remaining)
    {
	reason = BCM2835_I2C_REASON_ERROR_DATA;
    }

    bcm2835_peri_set_bits(control, BCM2835_BSC_S_DONE , BCM2835_BSC_S_DONE);

    return reason;
}

/* Read an number of bytes from I2C sending a repeated start after writing
// the required register. Only works if your device supports this mode
*/
uint8_t bcm2835_i2c_read_register_rs(char* regaddr, char* buf, uint32_t len)
{
#ifdef I2C_V1
    volatile uint32_t* dlen    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_C);
#else
    volatile uint32_t* dlen    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_C);
#endif
	uint32_t remaining = len;
    uint32_t i = 0;
    uint8_t reason = BCM2835_I2C_REASON_OK;

    /* Clear FIFO */
    bcm2835_peri_set_bits(control, BCM2835_BSC_C_CLEAR_1 , BCM2835_BSC_C_CLEAR_1 );
    /* Clear Status */
    bcm2835_peri_write(status, BCM2835_BSC_S_CLKT | BCM2835_BSC_S_ERR | BCM2835_BSC_S_DONE);
    /* Set Data Length */
    bcm2835_peri_write(dlen, 1);
    /* Enable device and start transfer */
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN);
    bcm2835_peri_write(fifo, regaddr[0]);
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST);

    /* poll for transfer has started */
    while ( !( bcm2835_peri_read(status) & BCM2835_BSC_S_TA ) )
    {
        /* Linux may cause us to miss entire transfer stage */
        if(bcm2835_peri_read(status) & BCM2835_BSC_S_DONE)
            break;
    }

    /* Send a repeated start with read bit set in address */
    bcm2835_peri_write(dlen, len);
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST  | BCM2835_BSC_C_READ );

    /* Wait for write to complete and first byte back. */
    bcm2835_delayMicroseconds(i2c_byte_wait_us * 3);

    /* wait for transfer to complete */
    while (!(bcm2835_peri_read(status) & BCM2835_BSC_S_DONE))
    {
        /* we must empty the FIFO as it is populated and not use any delay */
        while (remaining && bcm2835_peri_read(status) & BCM2835_BSC_S_RXD)
    	{
	    /* Read from FIFO */
	    buf[i] = bcm2835_peri_read(fifo);
	    i++;
	    remaining--;
    	}
    }

    /* transfer has finished - grab any remaining stuff in FIFO */
    while (remaining && (bcm2835_peri_read(status) & BCM2835_BSC_S_RXD))
    {
        /* Read from FIFO */
        buf[i] = bcm2835_peri_read(fifo);
        i++;
        remaining--;
    }

    /* Received a NACK */
    if (bcm2835_peri_read(status) & BCM2835_BSC_S_ERR)
    {
		reason = BCM2835_I2C_REASON_ERROR_NACK;
    }

    /* Received Clock Stretch Timeout */
    else if (bcm2835_peri_read(status) & BCM2835_BSC_S_CLKT)
    {
	reason = BCM2835_I2C_REASON_ERROR_CLKT;
    }

    /* Not all data is sent */
    else if (remaining)
    {
	reason = BCM2835_I2C_REASON_ERROR_DATA;
    }

    bcm2835_peri_set_bits(control, BCM2835_BSC_S_DONE , BCM2835_BSC_S_DONE);

    return reason;
}

/* Sending an arbitrary number of bytes before issuing a repeated start
// (with no prior stop) and reading a response. Some devices require this behavior.
*/
uint8_t bcm2835_i2c_write_read_rs(char* cmds, uint32_t cmds_len, char* buf, uint32_t buf_len)
{
#ifdef I2C_V1
    volatile uint32_t* dlen    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc0 + SHIFT4(BCM2835_BSC_C);
#else
    volatile uint32_t* dlen    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_DLEN);
    volatile uint32_t* fifo    = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_FIFO);
    volatile uint32_t* status  = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_S);
    volatile uint32_t* control = bcm2835_bsc1 + SHIFT4(BCM2835_BSC_C);
#endif

    uint32_t remaining = cmds_len;
    uint32_t i = 0;
    uint8_t reason = BCM2835_I2C_REASON_OK;

    /* Clear FIFO */
    bcm2835_peri_set_bits(control, BCM2835_BSC_C_CLEAR_1 , BCM2835_BSC_C_CLEAR_1 );

    /* Clear Status */
    bcm2835_peri_write(status, BCM2835_BSC_S_CLKT | BCM2835_BSC_S_ERR | BCM2835_BSC_S_DONE);

    /* Set Data Length */
    bcm2835_peri_write(dlen, cmds_len);

    /* pre populate FIFO with max buffer */
    while( remaining && ( i < BCM2835_BSC_FIFO_SIZE ) )
    {
        bcm2835_peri_write_nb(fifo, cmds[i]);
        i++;
        remaining--;
    }

    /* Enable device and start transfer */
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST);

    /* poll for transfer has started (way to do repeated start, from BCM2835 datasheet) */
    while ( !( bcm2835_peri_read(status) & BCM2835_BSC_S_TA ) )
    {
        /* Linux may cause us to miss entire transfer stage */
        if(bcm2835_peri_read_nb(status) & BCM2835_BSC_S_DONE)
            break;
    }

    remaining = buf_len;
    i = 0;

    /* Send a repeated start with read bit set in address */
    bcm2835_peri_write(dlen, buf_len);
    bcm2835_peri_write(control, BCM2835_BSC_C_I2CEN | BCM2835_BSC_C_ST  | BCM2835_BSC_C_READ );

    /* Wait for write to complete and first byte back. */
    bcm2835_delayMicroseconds(i2c_byte_wait_us * (cmds_len + 1));

    /* wait for transfer to complete */
    while (!(bcm2835_peri_read_nb(status) & BCM2835_BSC_S_DONE))
    {
        /* we must empty the FIFO as it is populated and not use any delay */
        while (remaining && bcm2835_peri_read(status) & BCM2835_BSC_S_RXD)
    	{
	    /* Read from FIFO, no barrier */
	    buf[i] = bcm2835_peri_read_nb(fifo);
	    i++;
	    remaining--;
    	}
    }

    /* transfer has finished - grab any remaining stuff in FIFO */
    while (remaining && (bcm2835_peri_read(status) & BCM2835_BSC_S_RXD))
    {
        /* Read from FIFO */
        buf[i] = bcm2835_peri_read(fifo);
        i++;
        remaining--;
    }

    /* Received a NACK */
    if (bcm2835_peri_read(status) & BCM2835_BSC_S_ERR)
    {
	reason = BCM2835_I2C_REASON_ERROR_NACK;
    }

    /* Received Clock Stretch Timeout */
    else if (bcm2835_peri_read(status) & BCM2835_BSC_S_CLKT)
    {
	reason = BCM2835_I2C_REASON_ERROR_CLKT;
    }

    /* Not all data is sent */
    else if (remaining)
    {
	reason = BCM2835_I2C_REASON_ERROR_DATA;
    }

    bcm2835_peri_set_bits(control, BCM2835_BSC_S_DONE , BCM2835_BSC_S_DONE);

    return reason;
}

/* Read the System Timer Counter (64-bits) */
uint64_t bcm2835_st_read(void)
{
    volatile uint32_t* paddr;
    uint32_t hi, lo;
    uint64_t st;

    if (bcm2835_st==MAP_FAILED)
	return 0;

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CHI);
    hi = bcm2835_peri_read(paddr);

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
    lo = bcm2835_peri_read(paddr);

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CHI);
    st = bcm2835_peri_read(paddr);

    /* Test for overflow */
    if (st == hi)
    {
        st <<= 32;
        st += lo;
    }
    else
    {
        st <<= 32;
        paddr = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
        st += bcm2835_peri_read(paddr);
    }
    return st;
}

/* Read the System Timer Counter (64-bits) */
uint64_t bcm2835_st_read_nb(void)
{
    volatile uint32_t* paddr;
    uint32_t hi, lo;
    uint64_t st;

    if (bcm2835_st==MAP_FAILED)
	return 0;

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CHI);
    hi = bcm2835_peri_read_nb(paddr);

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
    lo = bcm2835_peri_read_nb(paddr);

    paddr = bcm2835_st + SHIFT4(BCM2835_ST_CHI);
    st = bcm2835_peri_read_nb(paddr);

    /* Test for overflow */
    if (st == hi)
    {
        st <<= 32;
        st += lo;
    }
    else
    {
        st <<= 32;
        paddr = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
        st += bcm2835_peri_read_nb(paddr);
    }
    return st;
}


/* Delays for the specified number of microseconds with offset */
void bcm2835_st_delay(uint64_t offset_micros, uint64_t micros)
{
    uint64_t compare = offset_micros + micros;

    while(bcm2835_st_read() < compare)
	;
}

/* PWM */

void bcm2835_pwm_set_clock(uint32_t divisor)
{
    if (   bcm2835_clk == MAP_FAILED
        || bcm2835_pwm == MAP_FAILED)
      return; /* bcm2835_init() failed or not root */

    /* From Gerts code */
    divisor &= 0xfff;
    /* Stop PWM clock */
    bcm2835_peri_write(bcm2835_clk + BCM2835_PWMCLK_CNTL, BCM2835_PWM_PASSWRD | 0x01);
    bcm2835_delay(110); /* Prevents clock going slow */
    /* Wait for the clock to be not busy */
    while ((bcm2835_peri_read(bcm2835_clk + BCM2835_PWMCLK_CNTL) & 0x80) != 0)
	bcm2835_delay(1);
    /* set the clock divider and enable PWM clock */
    bcm2835_peri_write(bcm2835_clk + BCM2835_PWMCLK_DIV, BCM2835_PWM_PASSWRD | (divisor << 12));
    bcm2835_peri_write(bcm2835_clk + BCM2835_PWMCLK_CNTL, BCM2835_PWM_PASSWRD | 0x11); /* Source=osc and enable */
}

void bcm2835_pwm_set_mode(uint8_t channel, uint8_t markspace, uint8_t enabled)
{
  uint32_t control;
  if (   bcm2835_clk == MAP_FAILED
       || bcm2835_pwm == MAP_FAILED)
    return; /* bcm2835_init() failed or not root */

  control = bcm2835_peri_read(bcm2835_pwm + BCM2835_PWM_CONTROL);

  if (channel == 0)
    {
      if (markspace)
	control |= BCM2835_PWM0_MS_MODE;
      else
	control &= ~BCM2835_PWM0_MS_MODE;
      if (enabled)
	control |= BCM2835_PWM0_ENABLE;
      else
	control &= ~BCM2835_PWM0_ENABLE;
    }
  else if (channel == 1)
    {
      if (markspace)
	control |= BCM2835_PWM1_MS_MODE;
      else
	control &= ~BCM2835_PWM1_MS_MODE;
      if (enabled)
	control |= BCM2835_PWM1_ENABLE;
      else
	control &= ~BCM2835_PWM1_ENABLE;
    }

  /* If you use the barrier here, wierd things happen, and the commands dont work */
  bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM_CONTROL, control);
  /*  bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM_CONTROL, BCM2835_PWM0_ENABLE | BCM2835_PWM1_ENABLE | BCM2835_PWM0_MS_MODE | BCM2835_PWM1_MS_MODE); */

}

void bcm2835_pwm_set_range(uint8_t channel, uint32_t range)
{
  if (   bcm2835_clk == MAP_FAILED
       || bcm2835_pwm == MAP_FAILED)
    return; /* bcm2835_init() failed or not root */

  if (channel == 0)
      bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM0_RANGE, range);
  else if (channel == 1)
      bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM1_RANGE, range);
}

void bcm2835_pwm_set_data(uint8_t channel, uint32_t data)
{
  if (   bcm2835_clk == MAP_FAILED
       || bcm2835_pwm == MAP_FAILED)
    return; /* bcm2835_init() failed or not root */

  if (channel == 0)
      bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM0_DATA, data);
  else if (channel == 1)
      bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM1_DATA, data);
}

#ifdef BCM2835_MODULE_BUILD

static void *mapmem(const char *msg, size_t size, uint32_t *off)
{
	void *map;
	uint64_t loff = (u_int32_t)off;
	printk(KERN_INFO "bcm2835_init: mapmem %llx len %x\n", loff, size);
    request = request_mem_region(loff, size, msg);	// just tell everyone what we use, and it will fail as there are already drivers that have requested this area ...
    if (request == NULL) {
		printk(KERN_INFO "bcm2835_init: %s request_mem_region failed (as expected)\n", msg);	// just warn and continue
	}
    //void *map = memremap(loff, size, MEMREMAP_WC);			// at least this gives correct values when read
    map = ioremap_nocache(loff, size);	// this gives wrong values
    if (IS_ERR(map)) {
		printk(KERN_ERR "bcm2835_init: %s ioremap_nocache failed!\n", msg);
		map = MAP_FAILED;
		return map;
	}
	printk(KERN_INFO "bcm2835_init: mapmem got %x\n", (u_int32_t)map);
    return map;
}

static void unmapmem(u_int32_t *pmem, size_t size, void *offs)
{
    if (pmem == MAP_FAILED) return;
	printk(KERN_INFO "bcm2835_init: iounmap %x size %x\n", (u_int32_t)pmem, size);
    iounmap(pmem);
    if (request != NULL) {
		u_int64_t loff = (u_int32_t)offs;
		printk(KERN_INFO "bcm2835_init: release_mem_region %llx size %x!\n", loff, size);
		release_mem_region(loff, size);		// this doesnt recognize pointer ...
	}
}
#else

/* Allocate page-aligned memory. */
void *malloc_aligned(size_t size)
{
    void *mem;
    errno = posix_memalign(&mem, BCM2835_PAGE_SIZE, size);
    return (errno ? NULL : mem);
}

/* Map 'size' bytes starting at 'off' in file 'fd' to memory.
// Return mapped address on success, MAP_FAILED otherwise.
// On error print message.
*/
static void *mapmem(const char *msg, size_t size, int fd, off_t off)
{
    void *map = mmap(NULL, size, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, off);
    if (map == MAP_FAILED)
	printf( "bcm2835_init: %s mmap failed: %s\n", msg, strerror(errno));
    return map;
}

static void unmapmem(void **pmem, size_t size)
{
    if (*pmem == MAP_FAILED) return;
    munmap(*pmem, size);
    *pmem = MAP_FAILED;
}
#endif

/* Initialise this library. */
int bcm2835_init(void)
{
    int  ok;

    if (debug)
    {
        bcm2835_peripherals = (uint32_t*)BCM2835_PERI_BASE;

		bcm2835_pads = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_PADS);
		bcm2835_clk  = bcm2835_peripherals + SHIFT4(BCM2835_CLOCK_BASE);
		bcm2835_gpio = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_BASE);
		bcm2835_pwm  = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_PWM);
		bcm2835_spi0 = bcm2835_peripherals + SHIFT4(BCM2835_SPI0_BASE);
		bcm2835_bsc0 = bcm2835_peripherals + SHIFT4(BCM2835_BSC0_BASE);
		bcm2835_bsc1 = bcm2835_peripherals + SHIFT4(BCM2835_BSC1_BASE);
		bcm2835_st   = bcm2835_peripherals + SHIFT4(BCM2835_ST_BASE);
		bcm2835_aux  = bcm2835_peripherals + SHIFT4(BCM2835_AUX_BASE);
		bcm2835_spi1 = bcm2835_peripherals + SHIFT4(BCM2835_SPI1_BASE);

		return 1; /* Success */
    }

    /* Figure out the base and size of the peripheral address block
    // using the device-tree. Required for RPi2/3/4, optional for RPi 1
	// see https://www.linuxjournal.com/article/8110
    */
#ifdef BCM2835_MODULE_BUILD
	struct file *fp;
    u_int32_t buf[4];
    size_t read;
    uint32_t base_address;
    uint32_t peri_size;
    unsigned long long offset = 0l;
    // >= 4.14 of Linux kernel shoud need no segment switch
	//mm_segment_t old_fs = get_fs();
	//set_fs(KERNEL_DS);
    fp = filp_open(BMC2835_RPI2_DT_FILENAME, O_RDONLY, 0);
	if (!IS_ERR(fp)) {
		read = kernel_read(fp, (unsigned char *)buf, sizeof(buf), &offset);
		filp_close(fp, NULL);
		if (read == sizeof(buf)) {
			base_address = be32_to_cpu(buf[1]);
			peri_size = be32_to_cpu(buf[2]);
			if (!base_address) {
				base_address = be32_to_cpu(buf[2]);
				peri_size = be32_to_cpu(buf[3]);
			}
			if ((be32_to_cpu(buf[0]) == 0x7e000000) &&
                    ((base_address == BCM2835_PERI_BASE)
                    || (base_address == BCM2835_RPI2_PERI_BASE)
                    || (base_address == BCM2835_RPI4_PERI_BASE))) {
                bcm2835_peripherals_base = (uint32_t *)base_address;
                bcm2835_peripherals_size = peri_size;
                if (base_address == BCM2835_RPI4_PERI_BASE) {
                    pud_type_rpi4 = 1;
                }
            }
		}
		else {
			printk(KERN_ERR "bcm2835_init: read only %d expected %d", read, sizeof(buf));
			return 0;
		}
	}
	else {
		//set_fs(old_fs);
		printk(KERN_INFO "bcm2835_init: error opening %s", BMC2835_RPI2_DT_FILENAME);
		return 0;
	}
	//set_fs(old_fs);

	printk(KERN_INFO "bcm2835_init: bcm2835_peripherals_base: %x size %x",
					(u_int32_t)bcm2835_peripherals_base, bcm2835_peripherals_size);
    bcm2835_peripherals = (u_int32_t *)mapmem("logic", bcm2835_peripherals_size, bcm2835_peripherals_base);
    if (bcm2835_peripherals == MAP_FAILED) goto exit;
	printk(KERN_INFO "bcm2835_init: bcm2835_peripherals: %x\n", (u_int32_t)bcm2835_peripherals);

#else
    int  memfd;
    int fp;

    fp = open(BMC2835_RPI2_DT_FILENAME , O_RDONLY);
    if (fp > 0)
    {
        unsigned char buf[16];
        uint32_t base_address;
        uint32_t peri_size;
        if (read(fp, buf, sizeof(buf)) >= 8)
        {
            base_address = (buf[4] << 24) |
              (buf[5] << 16) |
              (buf[6] << 8) |
              (buf[7] << 0);

            peri_size = (buf[8] << 24) |
              (buf[9] << 16) |
              (buf[10] << 8) |
              (buf[11] << 0);

            if (!base_address)
            {
                /* looks like RPI 4 */
                base_address = (buf[8] << 24) |
                      (buf[9] << 16) |
                      (buf[10] << 8) |
                      (buf[11] << 0);

                peri_size = (buf[12] << 24) |
                (buf[13] << 16) |
                (buf[14] << 8) |
                (buf[15] << 0);
            }
            /* check for valid known range formats */
            if ((buf[0] == 0x7e) &&
                    (buf[1] == 0x00) &&
                    (buf[2] == 0x00) &&
                    (buf[3] == 0x00) &&
                    ((base_address == BCM2835_PERI_BASE) || (base_address == BCM2835_RPI2_PERI_BASE) || (base_address == BCM2835_RPI4_PERI_BASE)))
            {
                bcm2835_peripherals_base = (uint32_t *)base_address;
                bcm2835_peripherals_size = peri_size;
                if( base_address == BCM2835_RPI4_PERI_BASE )
                {
                    pud_type_rpi4 = 1;
                }
            }

        }

	close(fp);
    }
    /* else we are prob on RPi 1 with BCM2835, and use the hardwired defaults */

    /* Now get ready to map the peripherals block
     * If we are not root, try for the new /dev/gpiomem interface and accept
     * the fact that we can only access GPIO
     * else try for the /dev/mem interface and get access to everything
     */
    memfd = -1;
    ok = 0;
    if (geteuid() == 0)
    {
      /* Open the master /dev/mem device */
      if ((memfd = open("/dev/mem", O_RDWR | O_SYNC) ) < 0)
	  {
	    printf( "bcm2835_init: Unable to open /dev/mem: %s\n",
		  strerror(errno)) ;
	     goto exit;
	  }

      /* Base of the peripherals block is mapped to VM */
      bcm2835_peripherals = mapmem("gpio", bcm2835_peripherals_size, memfd, (uint32_t)bcm2835_peripherals_base);
      if (bcm2835_peripherals == MAP_FAILED) goto exit;
#endif
      /* Now compute the base addresses of various peripherals,
      // which are at fixed offsets within the mapped peripherals block
      // Caution: bcm2835_peripherals is uint32_t*, so divide offsets by 4
      */
      bcm2835_gpio = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_BASE);
	  //printk(KERN_INFO "bcm2835_init: bcm2835_gpio %x\n", (u_int32_t) bcm2835_gpio - (u_int32_t)bcm2835_peripherals);
      bcm2835_pwm  = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_PWM);
      bcm2835_clk  = bcm2835_peripherals + SHIFT4(BCM2835_CLOCK_BASE);
      bcm2835_pads = bcm2835_peripherals + SHIFT4(BCM2835_GPIO_PADS);
      bcm2835_spi0 = bcm2835_peripherals + SHIFT4(BCM2835_SPI0_BASE);
      bcm2835_bsc0 = bcm2835_peripherals + SHIFT4(BCM2835_BSC0_BASE); /* I2C */
      bcm2835_bsc1 = bcm2835_peripherals + SHIFT4(BCM2835_BSC1_BASE); /* I2C */
      bcm2835_st   = bcm2835_peripherals + SHIFT4(BCM2835_ST_BASE);
      bcm2835_aux  = bcm2835_peripherals + SHIFT4(BCM2835_AUX_BASE);
      bcm2835_spi1 = bcm2835_peripherals + SHIFT4(BCM2835_SPI1_BASE);
      bcm2835_irq  = bcm2835_peripherals + SHIFT4(BCM2835_IRQ_BASE);

      ok = 1;
#ifdef BCM2835_MODULE_BUILD
exit:
#else
    }
    else
    {
      /* Not root, try /dev/gpiomem */
      /* Open the master /dev/mem device */
		if ((memfd = open("/dev/gpiomem", O_RDWR | O_SYNC) ) < 0)
		{
		  printf("bcm2835_init: Unable to open /dev/gpiomem: %s\n",
			  strerror(errno)) ;
		  goto exit;
		}

      /* Base of the peripherals block is mapped to VM */
      bcm2835_peripherals_base = 0;
      bcm2835_peripherals = mapmem("gpio", bcm2835_peripherals_size, memfd, (off_t)bcm2835_peripherals_base);
      if (bcm2835_peripherals == MAP_FAILED) goto exit;
      bcm2835_gpio = bcm2835_peripherals;
      ok = 1;
    }

exit:
    if (memfd >= 0)
        close(memfd);
#endif

	//printk(KERN_INFO "bcm2835_init: ok %x\n", ok);
    if (!ok)
	bcm2835_close();

    return ok;
}

/* Close this library and deallocate everything */
int bcm2835_close(void)
{
    if (debug) return 1; /* Success */

#ifdef BCM2835_MODULE_BUILD
    unmapmem(bcm2835_peripherals, bcm2835_peripherals_size, bcm2835_peripherals_base);
#else
    unmapmem((void **)&bcm2835_peripherals, bcm2835_peripherals_size);
#endif
    bcm2835_peripherals = MAP_FAILED;
    bcm2835_gpio = MAP_FAILED;
    bcm2835_pwm  = MAP_FAILED;
    bcm2835_clk  = MAP_FAILED;
    bcm2835_pads = MAP_FAILED;
    bcm2835_spi0 = MAP_FAILED;
    bcm2835_bsc0 = MAP_FAILED;
    bcm2835_bsc1 = MAP_FAILED;
    bcm2835_st   = MAP_FAILED;
    bcm2835_aux  = MAP_FAILED;
    bcm2835_spi1 = MAP_FAILED;
    return 1; /* Success */
}


