#!/usr/bin/perl
use Device::SerialPort
Device::SerialPort->new("/dev/ttyACM0")->pulse_dtr_on(100);
