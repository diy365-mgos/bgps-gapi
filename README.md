# bGPS Google Geolocation API Library
## Overview
This Mongoose OS library is a specialized [bGPS Library](https://github.com/diy365-mgos/bgps) implementation that uses [Google Geolocation API](https://developers.google.com/maps/documentation/geolocation/overview).
## GET STARTED
### Before you begin
Before you start using this library, you need a Google API key. Please, review the Google's [authentication requirements](https://developers.google.com/maps/documentation/geolocation/get-api-key) and the [API usage and billing](https://developers.google.com/maps/documentation/geolocation/usage-and-billing) information (you need to enable billing on your project).
### Firmware
Add these configurations to your `mos.yml` file.
```yaml
config_schema: 
  - ["bgps.gapi.api_key", "YOUR_GOOGLE_API_KEY"]
  - ["bgps.gapi.update.enable", true]

libs:
  - location: https://github.com/mongoose-os-libs/boards
  - location: https://github.com/mongoose-os-libs/wifi
  - location: https://github.com/diy365-mgos/bgps-gapi
```
Copy and paste this firmware code into your `main.c` file.
```c
#include "mgos.h"
#include "mgos_bgps.h"

static void mg_gps_position_changed(int ev, void *ev_data, void *userdata) {
  struct mgos_bgps_position_changed *data = (struct mgos_bgps_position_changed *)ev_data;
  LOG(LL_INFO, ("GPS: lat %3f, lng %3f (accuracy %2f)",
    data->cur_pos.location.latitude,
    data->cur_pos.location.longitude,
    data->cur_pos.accuracy));

  (void) ev;
  (void) userdata;
}

enum mgos_app_init_result mgos_app_init(void) {
  mgos_event_add_handler(MGOS_EV_BGPS_POSITION_CHANGED, mg_gps_position_changed, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
```
Connect the board to the WiFi network using the `mos.exe` tool (see the [Configure WiFi](https://mongoose-os.com/docs/mongoose-os/quickstart/setup.md#7-configure-wifi) official Mongoose OS guide).
```
mos wifi <network_name> <password>
```
## Configuration
The library adds to the `bgps` configuration entry a `gapi` sub-entry which contains configuration settings.
```javascript
"bgps": {
  "gapi": {
    "api_key": "",        // Your Google's API key
    "update": {           // Geolocation update config
      "enable": false     // Enable the geolocation update
      "interval": 30000   // Update interval (in milliseconds)
    }
  }
}
```
## To Do
- Implement javascript APIs for [Mongoose OS MJS](https://github.com/mongoose-os-libs/mjs).