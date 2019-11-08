# Wifi Notes

**Be aware -- this fix disables higher WiFi speeds and limits connection to 54 Mbps.**

## Flaky `iwlwifi` 802.11n Support

The Intel integrated Wifi driver (`iwlwifi`) has issues. One of the major issues concerns 802.11n.
See:
* http://cachestocaches.com/2016/1/disabling-ubuntus-broken-wi-fi-driver/
* http://zeroset.mnim.org/2014/04/22/unstable-wifi-connection-on-ubuntu-14-04-trusty-tahr-ctrl-event-disconnected-reason4-locally_generated1/

To fix this, add the following line to `/etc/modprobe.d/iwlwifi.conf` and reboot:
```
options iwlwifi 11n_disable=1
```