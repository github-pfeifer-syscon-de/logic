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
 
#include "DeviceCapture.hpp"
 
#include <cstring>
#include <sys/ioctl.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
//#include <sys/io.h>
 
 
DeviceCapture::DeviceCapture() 
{
}

DeviceCapture::~DeviceCapture() 
{ 
}


bool 
DeviceCapture::start(Glib::Dispatcher &notification) 
{
	read_device();
	return m_active;
}

ssize_t 
DeviceCapture::readline(int f) {
	char line[120];
	int reads;
    uint32_t n,rdlen;
    uint32_t pos, level, edgs;
    uint32_t time;
    int32_t diff;
    rdlen = 0;

	// as we created some strange logic in device driver reflect this here
	//   no FILE * nor readline will work for this ...
	uint32_t lastTime = 0;
	int64_t timeHigh = 0ll;
	while (true) {
		reads = read(f, line, sizeof(line)-1);
		if (reads <= 0) {
			break;
		}
		line[reads] = '\0';
		n = sscanf(line, "0x%ux 0x%ux %d 0x%ux 0x%ux", &pos, &time, &diff, &level, &edgs);
		if (n >= 5) {
			//printf("Read 0x%x 0x%x %d 0x%x 0x%x\n", pos, time, diff, level, edgs);
			if (time < lastTime) {		// reconstruct monotonic time (as for performance we capture only the lower 32 timer bits) 
				timeHigh += 0x0000000100000000;
			}
			if (pos < BUF_LEN) {
				m_value[pos] = level;
				m_edge[pos] = edgs;
				m_times[pos] = timeHigh | (int64_t)time;
			}
			if (pos == BUF_LEN-1) {
				break;
			}
			lastTime = time;
		}
		else {
			printf("Error reading \"%s\" n %d\n", line, n);
		}
	}
	return reads;
}

void 
DeviceCapture::write_device(char *cmd) {
	printf("write_device \"%s\"\n", cmd);
	int f = open("/dev/logicdev", O_WRONLY);
	if (f > 0) {
		write(f, cmd, strlen(cmd)+1);				
		close(f);
	}
	
}


void
DeviceCapture::read_device_data() {
    ssize_t read;		
	int f = open("/dev/logicdev", O_RDONLY);
	if (f > 0) {
		while (m_active) {		// wait file to become ready
			read = readline(f);
			if (read > 0) {
				break;
			}
			else {
				delay(100000000ll);	// delay for not blocking pi
			}
		}    
		close(f);
	}
	
}

int
DeviceCapture::read_device()
{
   	char cmd[32];
	sprintf(cmd, "p0x%x", m_preTrigger);	// pre trigger values
	write_device(cmd);
	sprintf(cmd, "m0x%x", m_mask);	// trigger mask (any change)
	write_device(cmd);
	sprintf(cmd, "s");
	write_device(cmd);
	
	read_device_data();

   return 0;
}

long long 
DeviceCapture::getTimerRate() 
{
	return TIMER_RATE;
}

