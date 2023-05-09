#!/bin/bash

/opt/lira/ttybus/tty_bus -s /dev/ttyGPSbus -d
/opt/lira/ttybus/tty_attach -s /dev/ttyGPSbus -g -d /dev/ttyAMA0
/opt/lira/ttybus/tty_fake -s /dev/ttyGPSbus -d /dev/ttyGPSfake0
/opt/lira/ttybus/tty_fake -s /dev/ttyGPSbus -d /dev/ttyGPSfake1
/opt/lira/ttybus/tty_fake -s /dev/ttyGPSbus -d /dev/ttyGPSfake2
