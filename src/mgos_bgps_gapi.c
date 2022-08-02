#include "mgos.h"
#include "mgos_bgps_gapi.h"
#include "frozen.h"
#if MG_ENABLE_MQTT
#include "mgos_mqtt.h"
#endif

#ifdef MGOS_HAVE_MJS
#include "mjs.h"
#endif /* MGOS_HAVE_MJS */

#define MG_BGPS_GAPI_GOOGLE_BASE_URL "https://www.googleapis.com/geolocation/v1/geolocate"

static char *s_api_url = NULL;

static bool s_requesting_pos = false;

static int s_update_timer_id = MGOS_INVALID_TIMER_ID;

static bool s_is_position_set = false;
static struct mgos_bgps_position s_position;

int mg_wifi_scan_result_to_json(struct json_out *out, va_list *ap) {
  struct mgos_wifi_scan_result *aps = va_arg(*ap, struct mgos_wifi_scan_result *);
  int aps_len = va_arg(*ap, int);

  int count = json_printf(out, "[");

  for (int i = 0; i < aps_len; i++) {
    if (i > 0) count += json_printf(out, ", ");

    count += json_printf(out, "{signalToNoiseRatio: 0, age: 0,signalToNoiseRatio: 0, channel: %d, signalStrength: %2d, macAddress: \"%02x:%02x:%02x:%02x:%02x:%02x\"}",
      aps[i].channel, aps[i].rssi,
      aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2], aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5]);
  }

  count += json_printf(out, "]");
  return count;
}

static void mg_bgps_gapi_http_cb(struct mg_connection *conn, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;

  switch (ev) {
    case MG_EV_CONNECT:
      if ((*(int *) ev_data) != 0) {
        LOG(LL_ERROR,("Error %d connecting to Google Geolocation API", (*(int *) ev_data)));
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
          "{location: {lat: %f, lng: %f}, accuracy: %f}",
           &s_position.location.latitude,
           &s_position.location.longitude,
           &s_position.accuracy);
        s_is_position_set = true;
      } else {
        LOG(LL_ERROR,("Response error %d (response: '%s')", hm->resp_code, hm->body.p));
      }
      // force the connection to be closed to avoid
      // memory saturation in case of multiple requests
      conn->flags |= MG_F_SEND_AND_CLOSE;
      break;
    case MG_EV_CLOSE:
      s_requesting_pos = false;
      break;
  }
}

static bool mg_bgps_gapi_start_invoke_api(int aps_len, struct mgos_wifi_scan_result *aps) {
  bool success = false;

  char *request_body = json_asprintf("{considerIp: false, wifiAccessPoints: %M}",
    mg_wifi_scan_result_to_json, aps, aps_len);

  success = mg_connect_http(mgos_get_mgr(), mg_bgps_gapi_http_cb,
    NULL, s_api_url, "Content-Type: application/json\r\n", request_body);

  if (!success) {
    LOG(LL_ERROR,("POST request to %s failed (body: '%s')", s_api_url, request_body));
  }

  free(request_body);
  return success;
}

static void mg_bgps_gapi_wifi_scan_cb(int n, struct mgos_wifi_scan_result *res, void *arg) {
  if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED) {
    if (mg_bgps_gapi_start_invoke_api(n, res)) {
      return;
    }
  }
  s_requesting_pos = false;
  (void) arg;
}

static bool mg_bgps_gapi_start_get_position() {
  if (!s_requesting_pos) {
    s_requesting_pos = true;
    mgos_wifi_scan(mg_bgps_gapi_wifi_scan_cb, NULL);
    return true;
  }
  return false;
}

static void mg_bgps_gapi_update_timer_cb(void *arg) {
  mg_bgps_gapi_start_get_position();
  (void) arg;
}

bool mgos_bgps_get_position(struct mgos_bgps_position *position) {
  if (!position) return false;
  // initialize output
  if (s_is_position_set) {
    position->location.latitude = s_position.location.latitude;
    position->location.longitude = s_position.location.longitude;
    position->accuracy = s_position.accuracy;
  } else {
    position->location.latitude = 0.0;
    position->location.longitude = 0.0;
    position->accuracy = 0.0;
  }
  
  return s_is_position_set;
}

static void mg_bgps_gapi_start_polling_pos() {
  // Tyr to update immediately the position
  mg_bgps_gapi_start_get_position();
  // Try to start the update timer
  if (mgos_sys_config_get_bgps_gapi_update_enable()) {
    if (mgos_sys_config_get_bgps_gapi_update_interval() > 0) {
      s_update_timer_id = mgos_set_timer(mgos_sys_config_get_bgps_gapi_update_interval(),
        MGOS_TIMER_REPEAT, mg_bgps_gapi_update_timer_cb, NULL);
    } else {
      LOG(LL_ERROR,("Invalid update timer's interval (%d ms)",
        mgos_sys_config_get_bgps_gapi_update_interval()));
    }
  }
}

static void mg_bgps_gapi_stop_polling_pos() {
  // Stop the update timer
  if (s_update_timer_id != MGOS_INVALID_TIMER_ID) {
    mgos_clear_timer(s_update_timer_id);
    s_update_timer_id = MGOS_INVALID_TIMER_ID;
  }
}

#if MG_ENABLE_MQTT
static void mg_bgps_gapi_mqtt_ev_handler(int ev, void *ev_data, void *userdata) {
  if (ev == MG_EV_MQTT_CONNACK) {
    mg_bgps_gapi_start_polling_pos();
  } else if (ev == MG_EV_MQTT_DISCONNECT) {
    mg_bgps_gapi_stop_polling_pos();
  }
  (void) ev_data;
  (void) userdata;
}
#elif
static void mg_bgps_gapi_net_ev_handler(int ev, void *evd, void *arg) {
  switch(ev) {
    case MGOS_NET_EV_IP_ACQUIRED:
      LOG(LL_INFO,("MGOS_NET_EV_IP_ACQUIRED"));
      mg_bgps_gapi_start_polling_pos();
      break;
    case MGOS_NET_EV_DISCONNECTED:
      LOG(LL_INFO,("MGOS_NET_EV_DISCONNECTED"));
      mg_bgps_gapi_stop_polling_pos();
      break;
  }

  (void) evd;
  (void) arg;
}
#endif

#ifdef MGOS_HAVE_MJS

#endif /* MGOS_HAVE_MJS */

bool mgos_bgps_gapi_init() {
  // Initializa position
  s_position.location.latitude = 0.0;
  s_position.location.longitude = 0.0;
  s_position.accuracy = 0.0;

  // Initialize the Google Geolocate API URL
  // (e.g.: https://www.googleapis.com/geolocation/v1/geolocate?key=YOUR_API_KEY)
  if (mgos_sys_config_get_bgps_gapi_api_key() != NULL) {
    // allocate mem buffer for the full API's URL
    s_api_url = calloc((strlen(mgos_sys_config_get_bgps_gapi_api_key()) +
      strlen(MG_BGPS_GAPI_GOOGLE_BASE_URL) + strlen("?key=") + 1), sizeof(char));  
    // set the mem buffer value with the full API's URL
    sprintf(s_api_url, "%s?key=%s", MG_BGPS_GAPI_GOOGLE_BASE_URL, mgos_sys_config_get_bgps_gapi_api_key());
    LOG(LL_DEBUG,("Google Geolocate API URL: %s", s_api_url));
  } else {
    LOG(LL_ERROR,("Invalid empty Google API key"));
  }

  if (s_api_url) {
    #if MG_ENABLE_MQTT
    if (!mgos_event_add_handler(MG_EV_MQTT_CONNACK, mg_bgps_gapi_mqtt_ev_handler, NULL)) {
      LOG(LL_ERROR,("Unable to start updating position as soon as the MQTT connection is ready"));
    } else {
      if (!mgos_event_add_handler(MG_EV_MQTT_DISCONNECT, mg_bgps_gapi_mqtt_ev_handler, NULL)) {
        LOG(LL_WARN, ("Unable to stop updating position if MQTT connection is down"));
      }
    }
    #elif
    mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, mg_bgps_gapi_net_ev_handler, NULL);
    #endif
  } else {
    LOG(LL_ERROR,("Invalid empty Google Geolocate API URL"));
  }

  return true;
}