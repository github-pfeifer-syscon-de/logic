/*
 * Copyright (C) 2019 rpf
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <thread>
#include <future>
#include <chrono>
#include <iostream>
#include <sys/mman.h>

#include "UserCapture.hpp"
#include "bcm2835_gpio.h"

UserCapture::UserCapture() 
: m_timer_rate(TIMER_RATE_MHz)
{
	m_init = bcm2835_init();
	
	// a raspi claims that the resolution for the basic clock is 1ns which is quite fine
	//   the coarse function will give 1ms which is truly coarse, 
	// as always is there nothing inbetween? fast as a timer read and 1MHz woud be fine 	
	//struct timespec rem;
    //clock_getres(CLOCK_REALTIME, &rem); std::cout << "Real " << rem.tv_nsec << "ns" << std::endl;
    //clock_getres(CLOCK_REALTIME_COARSE, &rem); std::cout << "Real coarse " << rem.tv_nsec << "ns" << std::endl;
    //clock_getres(CLOCK_MONOTONIC, &rem); std::cout << "Mono " << rem.tv_nsec << "ns" << std::endl;
    //clock_getres(CLOCK_MONOTONIC_COARSE, &rem); std::cout << "Mono coarse " << rem.tv_nsec << "ns" << std::endl;
    //clock_getres(CLOCK_MONOTONIC_RAW, &rem); std::cout << "Mono raw " << rem.tv_nsec << "ns" << std::endl;
    //clock_getres(CLOCK_BOOTTIME, &rem); std::cout << "Boot " << rem.tv_nsec << "ns" << std::endl;	
}



UserCapture::~UserCapture() 
{
	bcm2835_close();
}

bool 
UserCapture::getInit() 
{
	return m_init;
}


void 
UserCapture::convertToOutput(int32_t start, int32_t *times, uint32_t *value, uint32_t *edge) 
{
	int j = start;
	int64_t time_h = 0l;
	int32_t tm_last = times[j];
	int32_t diff;
	for (int i = 0; i < BUF_LEN; ++i) {	// bring into order 
		diff = times[j] - tm_last;
		//std::cout << "times[" << j << "]=" << times[j]  << " last " << tm_last << " diff " << diff << std::endl;
		if (diff < 0) {
			if (bcm2835_st != MAP_FAILED) {
				time_h += 0x100000000;
			}
			else {
				time_h += 1000000000;	// the nano timer wraps on decimal
			}
			//std::cout << "time wrap " << j << std::endl;
		}
		m_times[i] = times[j] + time_h;
		m_value[i] = value[j];
		m_edge[i] = edge[j];
		tm_last = times[j];
		++j;
		if (j >= BUF_LEN) {
			j = 0;
		 }
	}
}

bool 
UserCapture::run(Glib::Dispatcher &notification) 
{
	volatile uint32_t val_ren;
	volatile uint32_t val_fen;
	volatile uint32_t* paddr_gpeds0;
	volatile uint32_t* paddr_gplev0;
	volatile uint32_t* paddr_gpren0;
	volatile uint32_t* paddr_gpfen0;
	volatile uint32_t* paddr_stClo;
	//uint64_t time;
	uint32_t value_eds;
	uint32_t value_level;
	int32_t pos,avail,start;
	bool timeout;
	uint32_t triggerReady,triggerActive,triggerRemain;
    int32_t times[BUF_LEN];
    uint32_t value[BUF_LEN];
    uint32_t edge[BUF_LEN];
	
	if (!m_init 
	 || bcm2835_gpio == MAP_FAILED) {
		return false;
	}

	paddr_gplev0 = bcm2835_gpio + SHIFT4(BCM2835_GPLEV0);
	paddr_gpeds0 = bcm2835_gpio + SHIFT4(BCM2835_GPEDS0);
	paddr_stClo = bcm2835_st + SHIFT4(BCM2835_ST_CLO);
	if (m_edge_enabled) {
		val_ren = bcm2835_gpio_ren_multi_get();
		val_fen = bcm2835_gpio_fen_multi_get();

		// enable as much edges as possible as detection will check values by &
		//   and we are intrested in as much edge detection as possible
		uint32_t triggNone = ~(m_triggerRiseEdge | m_triggerFallEdge);
		
		paddr_gpren0 = bcm2835_gpio + SHIFT4(BCM2835_GPREN0);
		paddr_gpfen0 = bcm2835_gpio + SHIFT4(BCM2835_GPFEN0);
		__sync_synchronize();
		*paddr_gpren0 = (m_triggerRiseEdge | triggNone);	// enable rising edge sense
		*paddr_gpfen0 = (m_triggerFallEdge | triggNone);	// enable falling edge sense	 
		*paddr_gpeds0 = 0xffffffff;							// clear all
		__sync_synchronize();
	}
	//setupPmw();
	//bool spi_fail = setupSpi();		// just for testing, requires root
	//if (!spi_fail) {
	//	sendSpi(0x55);
	//}
	m_active = true;
	start = -1;
	pos = 0;
	avail = 0;
	triggerReady = 0;
	triggerActive = 0;
    
    struct timespec prev = {.tv_sec=0,.tv_nsec=0};
    struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);	// get start to align following times    
	m_timer_rate = (m_time > 0 && bcm2835_st != MAP_FAILED) 
					? TIMER_RATE_MHz
					: TIMER_RATE_GHz;
			
	while(m_active) {
		if (m_time > 0) {
			if (bcm2835_st != MAP_FAILED) {
				__sync_synchronize();			// User barrier functions
				times[pos] = *paddr_stClo;		// use just low for performance (and we shoud not reach the max of ~1h). Are timer and gpio the same device for barrier ? to reconstruct timing		
				__sync_synchronize();
			}
			else {
				// this woud be nice but do we have some architecture mixup ?
				//   add -march=armv8-a didnt help either ...
				//unsigned long result;
				//asm volatile ("mrs %0, cntpct_el0" : "=r" (result));
				//times[pos] = result;
				// this will is somewhat time consuming
				clock_gettime(CLOCK_MONOTONIC, &prev);	// this costs some time, even if it is most accurate ...
				times[pos] = prev.tv_nsec;			// use lowest 32 bits and adjust afterwards
			}
		}
		else {									// as user we have no access to timer the nano time is slower, so just use our target time
			times[pos] = next.tv_nsec;			// use lowest 32 bits and adjust afterwards
		}
		value_level = *paddr_gplev0;
		value[pos] = value_level; 
		__sync_synchronize();
		if (m_edge_enabled) {
			value_eds = *paddr_gpeds0;
			if (  value_eds != 0) {				// prevent writing if we have no changes test shoud be internal the io access is not
				*paddr_gpeds0 = value_eds;		// by writing the value we prevent resetting state we did not "see" in previous statement (occured inbetween)
			}
			__sync_synchronize();
		}
		else {
			value_eds = 0;
		}
		edge[pos] = value_eds;			// edge sense
		if (++pos >= BUF_LEN) {
			pos = 0;	// wrap
		}
		if (start >= 0) {
			if (pos == start) {		// is triggered -> capture until start is reached
				//std::cout << "Capture complete start " << start << std::endl;
				break;
			}
		}
		else {	// waiting for trigger
			avail = max(avail, pos);	// keep track of available 
			if (m_mask == 0) { // trigger 0 -> immediate start
				start = pos;
			}
			else if (!m_edge_enabled) {			// or if in "safe" mode
				triggerRemain = ~triggerReady & m_triggerRiseEdge;
				if (triggerRemain != triggerReady) {					
			      	triggerReady |= ~value_level & triggerRemain;	// for a rising edge we first need 0 
				}
				triggerRemain = ~triggerReady & m_triggerFallEdge;
				if (triggerRemain != triggerReady) {					
			      	triggerReady |= value_level & triggerRemain;	// for a falling edge we first need 1 
				}
				triggerRemain = ~triggerActive & triggerReady & m_triggerRiseEdge;
				if (triggerRemain != triggerActive) {
					triggerActive |= value_level & triggerRemain;	// for a rising edge we need a 1 
				}					
				triggerRemain = ~triggerActive & triggerReady & m_triggerFallEdge;
				if (triggerRemain != triggerActive) {
					triggerActive |= ~value_level & triggerRemain;	// for a falling edge we need a 0 
				}
				std::cout << "value " << std::hex << value_level 
					      << " ready " << std::hex << triggerReady 
			              << " active " << std::hex << triggerActive << std::endl;
					
				if (triggerActive & m_mask) {
					start = pos;
				}
			}
			else if (value_eds & m_mask) {
				m_ext_start = min(m_preTrigger, avail);
				start = pos - m_ext_start;		// determine how much to keep of previous values, do not overshoot if we not yet collected m_pre_trigger
				if (start < 0) {
					start += BUF_LEN;
				}
				if (m_time > 10000) {		// for shorter times avoid overhead
					notification.emit();
				}
				//std::cout << "pos " << pos << " start " << m_start << " pre " << m_preTrigger << " avail " << avail << std::endl;
			}
			if (start >= 0
			 && m_edge_enabled) {		// as we may have used a partial mask on init
				__sync_synchronize();
				*paddr_gpren0 = 0xffffffff;
				*paddr_gpfen0 = 0xffffffff;
				__sync_synchronize();
			}				
			//std::cout << "start " << tmstart.tv_sec << " next " << next.tv_sec << std::endl;
		}	
		if (m_time > 1) {
			next.tv_nsec += m_time;
			if (next.tv_nsec >= 1000000000) {	// this shoud be fine if adding times < 1s
				next.tv_nsec -= 1000000000;
				++next.tv_sec;
			}			
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);	// for next activation use absolute, to reduce the effect of capture runtime
		}
		else {
			next.tv_nsec += 500;		// this is about the speed we reach on a raspi 4, for others i cant tell
		}
	}
	if (m_edge_enabled) {
		bcm2835_gpio_ren_multi_set(val_ren);	// restore
		bcm2835_gpio_fen_multi_set(val_fen);
	}
	
	convertToOutput(start, times, value, edge);
	//if (!spi_fail)
	//	bcm2835_spi_end();	// do this in case spi was used

	return m_active;
}

void 
UserCapture::setupPmw() 
{
	// this is used for testing timing (requires root access)
	bcm2835_pwm_set_clock(BCM2835_PWM_CLOCK_DIVIDER_128);	// 150kHz 
	bcm2835_pwm_set_mode(0, 1, 1);
	bcm2835_pwm_set_range(0, 10);	// result is 42kHz signal 
	bcm2835_pwm_set_data(0, 5);		// 50% duty cycle
	bcm2835_gpio_fsel(18, BCM2835_GPIO_FSEL_ALT5);
}

bool
UserCapture::setupSpi() 
{
	// just to test some output (requires root access)
	if (!bcm2835_spi_begin())  {
		printf("bcm2835_spi_begin failed. Are you running as root??\n");
		return true;
    }
    return false;
}

bool
UserCapture::sendSpi(uint8_t send_data) 
{
	// just to test some output (requires root access)
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);      // The default
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);                   // The default
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_4096);  // The default
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);                      // The default
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);      // the default
    
    bcm2835_spi_transfer_nowait(send_data);
    return true;
}

/* capture is free running when it is started 
 *   if the trigger is recognized the capture runs til the buffer is filled
 *     to allow some pre-trigger observation
 */ 
bool 
UserCapture::start(Glib::Dispatcher &notification) 
{
	return run(notification);
	//runAsm();
}

long long 
UserCapture::getTimerRate() {
	return m_timer_rate;
}

