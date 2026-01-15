#Logic
This is a logic-analysator style program for RasPi only.

It is important that you add:

  dtoverlay=gpio-no-irq

to /boot/config.txt and reboot, to keep your pi happy.

The udev setup is not required for raspian buster.

To build the auto-tools precedure should look like:
<pre>
   autoreconf -fis
   ./configure --prefix=/usr
   make CXXFLAGS="-mtune=native -march=native -O3"
   ./src/logic
</pre>
