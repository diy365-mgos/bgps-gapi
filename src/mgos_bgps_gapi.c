#include "mgos.h"
#include "mgos_bgps_gapi.h"
#include "frozen.h"

#ifdef MGOS_HAVE_MJS
#include "mjs.h"
#endif /* MGOS_HAVE_MJS */

static char *s_api_url = NULL;

static bool s_position_ok = false;
static float s_latitude = 0.0;
static float s_longitude = 0.0;
static int s_accuracy = 0;

int mg_wifi_scan_result_to_json(struct json_out *out, va_list *ap) {
  struct mgos_wifi_scan_result *aps = va_arg(*ap, struct mgos_wifi_scan_result *);
  int aps_len = va_arg(*ap, int);

  int count = json_printf(out, "[");

  for (int i = 0; i < aps_len; i++) {
    if (i > 0) count += json_printf(out, ",");

    count += json_printf(out, "{ \
      signalToNoiseRatio: 0, \
      age: 0, \
      signalToNoiseRatio: 0, \
      channel: %d, \
      signalStrength: %2d, \
      macAddress: \"%02x:%02x:%02x:%02x:%02x:%02x\" }",
      aps[i].channel,
      aps[i].rssi,
      aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2], aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5]);
  }

  count += json_printf(out, "]");
  return count;
}

static void mg_bgps_gapi_http_cb(struct mg_connection *c, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;

  switch (ev) {
    case MG_EV_CONNECT:
      if ((*(int *) ev_data) != 0) {
        /* Error connecting */
        LOG(LL_ERROR,("Error connecting..."));
        s_position_ok = false;
      }
      break;
    case MG_EV_HTTP_REPLY:
      if (hm->resp_code == 200) {
        /* 
          A successful geolocation request will return a JSON-formatted response
          defining a location and radius.

            location: The user’s estimated latitude and longitude, in degrees.
                      Contains one lat and one lng subfield.
            accuracy: The accuracy of the estimated location, in meters.
                      This represents the radius of a circle around the given location.

          {
            "location": {
              "lat": 37.421875199999995,
              "lng": -122.0851173
            },
            "accuracy": 120
          } 
        */
        json_scanf(hm->body.p, hm->body.len,
          "{location: {lat: %f, lng: %f}, accuracy: %d}",
           &s_latitude, &s_longitude, &s_accuracy);
        LOG(LL_INFO,("%s", hm->body.p));
        s_position_ok = true;
      } else {
        LOG(LL_ERROR,("%s", hm->body.p));
        s_position_ok = false;
      }
      break;
  }
}

static bool mg_bgps_gapi_start_get_position(int aps_len, struct mgos_wifi_scan_result *aps) {
  bool success = false;
  if (s_api_url) {
    char *request_body = json_asprintf("considerIp: false, wifiAccessPoints: %M",
      mg_wifi_scan_result_to_json, aps, aps_len);

    if (request_body) {
      success = mg_connect_http(mgos_get_mgr(), mg_bgps_gapi_http_cb, NULL, s_api_url,
        "Content-Type=application/json", request_body);
      
      if (!success) {
        LOG(LL_ERROR,("Failed POST %s", s_api_url));
      }

      free(request_body);
    }
  }

  return success;
}

static void mg_bgps_gapi_wifi_scan_cb(int n, struct mgos_wifi_scan_result *res, void *arg) {
  if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED) {
    if (!mg_bgps_gapi_start_get_position(n, res)) {
      s_position_ok = false;
    }
  } else {
    s_position_ok = false;
  }
  (void) arg;
}

static void mg_bgps_gapi_timer_cb(void *arg) {
  mgos_wifi_scan(mg_bgps_gapi_wifi_scan_cb, NULL);
  (void) arg;
}

bool mgos_bgps_get_position(float *latitude, float *longitude, int *accuracy) {
  // initialize output
  if (latitude) *latitude = (s_position_ok ? s_latitude : 0.0);
  if (longitude) *longitude = (s_position_ok ? s_longitude : 0.0);
  if (accuracy) *accuracy = (s_position_ok ? s_accuracy : 0);
  return s_position_ok;
}

#ifdef MGOS_HAVE_MJS

#endif /* MGOS_HAVE_MJS */

bool mgos_bgps_gapi_init() {

  // Initialize the Google API URL
  // like https://www.googleapis.com/geolocation/v1/geolocate?key=YOUR_API_KEY
  if (mgos_sys_config_get_gps_gapi_api_key() != NULL &&
      mgos_sys_config_get_gps_gapi_url() != NULL) {
    size_t len = strlen(mgos_sys_config_get_gps_gapi_api_key()) +
      strlen(mgos_sys_config_get_gps_gapi_url()) + strlen("?key=") + 1;
    s_api_url = calloc(len, sizeof(char));
    
    sprintf(s_api_url, "%s?key=%s", 
      mgos_sys_config_get_gps_gapi_url(),
      mgos_sys_config_get_gps_gapi_api_key());

    LOG(LL_INFO,("Google Geolocate API URL: %s", s_api_url));
  } else {
    LOG(LL_ERROR,("Invalid empty API-KEY and/or API-URL"));
  }

  // Start the polling interval
  if (mgos_sys_config_get_gps_gapi_update_interval() > 0) {
    mgos_set_timer(mgos_sys_config_get_gps_gapi_update_interval(),
      MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW,
      mg_bgps_gapi_timer_cb, NULL);
  } else {
    LOG(LL_WARN,("Update interval not set"));
  }

  return true;
}