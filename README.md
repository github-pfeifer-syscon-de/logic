#Logic
This is a logic-analysator style program for RasPi only.
I tested this only for 32-Bit Linux (this might be somewhat outdated).

It is important that you add:

  dtoverlay=gpio-no-irq

to /boot/config.txt and reboot, to keep your pi happy.

The udev setup is not required for raspian buster.
Install the prerequesits (as root):
<pre>
   apt-get install gtkmm-3.0
</pre>
To build the meson procedure should look like:
<pre>
   meson setup build -Dprefix=/usr
   cd build
   meson compile
   ./logic
</pre>
