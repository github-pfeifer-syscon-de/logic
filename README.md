#Logic
This is a logic-analysator style program for RasPi only.
I tested this only for 32-Bit Linux. 

It is important that you add:

  dtoverlay=gpio-no-irq

to /boot/config.txt and reboot, to keep your pi happy.

The udev setup is not required for raspian buster.

To build the auto-tools procedure should look like:
<pre>
   autoreconf -fis
   ./configure --prefix=/usr
   make CXXFLAGS="-mtune=native -march=native -O3"
   ./src/logic
</pre>
